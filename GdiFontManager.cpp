#include "GdiFontManager.h"
#include <filesystem>
#include <locale>

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid)
{
    UINT num  = 0; // number of image encoders
    UINT size = 0; // size of the image encoder array in bytes

    Gdiplus::ImageCodecInfo* pImageCodecInfo = NULL;

    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0)
        return -1; // Failure

    pImageCodecInfo = (Gdiplus::ImageCodecInfo*)(malloc(size));
    if (pImageCodecInfo == NULL)
        return -1; // Failure

    Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);

    for (UINT j = 0; j < num; ++j)
    {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0)
        {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j; // Success
        }
    }

    free(pImageCodecInfo);
    return -1; // Failure
}

GdiFontManager::GdiFontManager(IDirect3DDevice8* pDevice, uint32_t pLogManager)
    : m_Device(pDevice)
    , m_CanvasWidth(2048)
    , m_CanvasHeight(2048)
    , m_SaveToHardDrive(false)
{
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&m_GDIToken, &gdiplusStartupInput, NULL);

    // Create bitmap in memory..
    m_Size     = m_CanvasWidth * m_CanvasHeight * 4;
    m_RawImage = malloc(m_Size + 108);
    m_Pixels   = (uint8_t*)m_RawImage + 108;
    memset(m_RawImage, 0, m_Size + 108);
    auto p_Header              = (BITMAPV4HEADER*)m_RawImage;
    p_Header->bV4Size          = sizeof(BITMAPV4HEADER);
    p_Header->bV4Width         = m_CanvasWidth;
    p_Header->bV4Height        = m_CanvasHeight;
    p_Header->bV4Planes        = 1;
    p_Header->bV4BitCount      = 32;
    p_Header->bV4V4Compression = BI_BITFIELDS;
    p_Header->bV4RedMask       = 0x00FF0000;
    p_Header->bV4GreenMask     = 0x0000FF00;
    p_Header->bV4BlueMask      = 0x000000FF;
    p_Header->bV4AlphaMask     = 0xFF000000;

    // Create gdiplus objects using bitmap in memory..
    this->m_CanvasStride = m_CanvasWidth * 4;
    this->m_Bitmap       = new Gdiplus::Bitmap(m_CanvasWidth, m_CanvasHeight, m_CanvasStride, PixelFormat32bppARGB, (BYTE*)m_Pixels);
    this->m_Graphics     = new Gdiplus::Graphics(this->m_Bitmap);
    m_Graphics->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    m_Graphics->SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    m_Graphics->SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    m_Graphics->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    m_Graphics->SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    m_Graphics->SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    setlocale(LC_ALL, "");

    if (pLogManager != 0)
    {
        m_LogManager = reinterpret_cast<ILogManager*>(pLogManager);
    }
}

GdiFontManager::~GdiFontManager()
{
    delete this->m_Graphics;
    delete this->m_Bitmap;
    free(m_RawImage);
    Gdiplus::GdiplusShutdown(m_GDIToken);
}

GdiFontReturn_t GdiFontManager::CreateFontTexture(const GdiFontData_t& data)
{
    if (m_LogManager != nullptr)
    {
        m_LogManager->Logf(5, "GdiFontTexture", "CreateFontTexture: %s", data.FontText);
    }

    int32_t boxHeight = data.BoxHeight > 0 ? data.BoxHeight : m_CanvasHeight;
    int32_t boxWidth = data.BoxWidth > 0 ? data.BoxWidth : m_CanvasWidth;

    // Get FontFamily as wchar
    int wchar_buffer_size = ::MultiByteToWideChar(CP_UTF8, 0, data.FontFamily, -1, nullptr, 0);
    wchar_t* wBuffer = new wchar_t[wchar_buffer_size];
    ::MultiByteToWideChar(CP_UTF8, 0, data.FontFamily, -1, wBuffer, wchar_buffer_size);

    // Attempt to set up font family..
    Gdiplus::FontFamily* pFontFamily = new Gdiplus::FontFamily(wBuffer);
    if (pFontFamily->GetLastStatus() != Gdiplus::Ok)
    {
        delete pFontFamily;
        delete[] wBuffer;
        return GdiFontReturn_t();
    }

    delete[] wBuffer;

    // Get FontText as wchar
    wchar_buffer_size = ::MultiByteToWideChar(CP_UTF8, 0, data.FontText, -1, nullptr, 0);
    wBuffer = new wchar_t[wchar_buffer_size];
    ::MultiByteToWideChar(CP_UTF8, 0, data.FontText, -1, wBuffer, wchar_buffer_size);
    auto length = wcslen(wBuffer);

    if (m_LogManager != nullptr)
    {
        m_LogManager->Logf(5, "GdiFontTexture", "wchar_buffer_size: %d, length: %d", wchar_buffer_size, length);
    }

    // Attempt to create graphics path..
    Gdiplus::Rect pathRect(0, 0, boxWidth, boxHeight);
    Gdiplus::StringFormat fontFormat = Gdiplus::StringFormat::GenericTypographic();

    Gdiplus::GraphicsPath* pPath = new Gdiplus::GraphicsPath();
    pPath->AddString(wBuffer, length, pFontFamily, data.FontFlags, data.FontHeight, pathRect, &fontFormat);
    if (pPath->GetLastStatus() != Gdiplus::Ok)
    {
        delete pPath;
        delete pFontFamily;
        return GdiFontReturn_t();
    }

    if (m_LogManager != nullptr)
    {
        m_LogManager->Log(5, "GdiFontTexture", "Prepare outline pen");
    }

    // Prepare outline pen if applicable and get calculated path size from Gdiplus..
    Gdiplus::Pen* pen = nullptr;
    Gdiplus::RectF box{};
    if ((data.OutlineWidth > 0) && ((data.OutlineColor & 0xFF000000) != 0))
    {
        pen = new Gdiplus::Pen(UINT32_TO_COLOR(data.OutlineColor), data.OutlineWidth);
        pPath->GetBounds(&box, nullptr, pen);
    }
    else
    {
        Gdiplus::Pen genericPen(Gdiplus::Color(255, 255, 255, 255), 1.0);
        pPath->GetBounds(&box, nullptr, &genericPen);
    }

    if (m_LogManager != nullptr)
    {
        m_LogManager->Log(5, "GdiFontTexture", "ClearCanvas");
    }

    // Clear necessary space using calculated path size.
    int32_t width  = (int32_t)ceil(box.Width);
    int32_t height = (int32_t)ceil(box.Height);
    this->ClearCanvas(this->m_CanvasWidth, this->m_CanvasHeight);

    m_Graphics->ResetClip();
    Gdiplus::Region clipRegion(box);

    Gdiplus::RectF boxF(0, 0, ceilf(boxWidth), ceilf(boxHeight));
    Gdiplus::Font myFont(pFontFamily, data.FontHeight, data.FontFlags, Gdiplus::UnitPixel);

    // Apply global clip range if provided
    if(data.ClipRange != nullptr && data.ClipRange->Length > 0)
    {
        if(data.ClipRange->Length > length)
        {
            data.ClipRange->Length = length;
        }

        fontFormat.SetMeasurableCharacterRanges(1, (const Gdiplus::CharacterRange*)data.ClipRange);
        Gdiplus::Region globalClipRegion;
        m_Graphics->MeasureCharacterRanges(wBuffer, -1, &myFont, boxF, &fontFormat, 1, &globalClipRegion);

        clipRegion.Intersect(&globalClipRegion);

        //Gdiplus::SolidBrush bgBrush(Gdiplus::Color(1, 255, 255, 255));
        //m_Graphics->FillRegion(&bgBrush, &globalClipRegion);
    }

    // Draw regions first
    for(int regionIndex = 0; regionIndex < data.RegionsLength; ++regionIndex)
    { 
        const auto& regionInfo = data.Regions[regionIndex];
        int rangeCount = regionInfo.RangesLength;
        fontFormat.SetMeasurableCharacterRanges(rangeCount, (const Gdiplus::CharacterRange*)regionInfo.Ranges);
        Gdiplus::Region* pCharRangeRegions = new Gdiplus::Region[rangeCount];

        m_Graphics->MeasureCharacterRanges(wBuffer, -1, &myFont, boxF, &fontFormat, rangeCount, pCharRangeRegions);

        for (int i = 0; i < rangeCount; i++)
        {
            // Draw this character range region in the defined color
            //Gdiplus::SolidBrush bgBrush(UINT32_TO_COLOR(regionInfo.FontColor));
            //m_Graphics->FillRegion(&bgBrush, pCharRangeRegions + i);

            m_Graphics->SetClip(&clipRegion, Gdiplus::CombineModeReplace);
            m_Graphics->SetClip(pCharRangeRegions + i, Gdiplus::CombineModeIntersect);

            // Draw in the defined color
            auto pBrush = new Gdiplus::SolidBrush(UINT32_TO_COLOR(regionInfo.FontColor));
            m_Graphics->FillPath(pBrush, pPath);
            delete pBrush;

            // Exclude this area from the standard render
            clipRegion.Exclude(pCharRangeRegions + i);
        }

        delete[] pCharRangeRegions;
    }

    delete[] wBuffer;

    m_Graphics->ResetClip();

    // Set clip so we don't render on top of excluded regions
    if (data.RegionsLength > 0 || data.ClipRange != nullptr)
    {
        m_Graphics->SetClip(&clipRegion, Gdiplus::CombineModeReplace);
    }

    // Draw outline if applicable..
    if (pen)
    {
        m_Graphics->DrawPath(pen, pPath);
        delete pen;
    }

    // Fill text if font color isn't fully transparent..
    if (((data.FontColor & 0xFF000000) != 0) || ((data.GradientStyle != 0) && ((data.GradientColor & 0xFF000000) != 0)))
    {
        auto pBrush = GetBrush(data, width, height);
        m_Graphics->FillPath(pBrush, pPath);
        delete pBrush;
    }

    // Clean up remaining gdiplus objects..
    delete pPath;
    delete pFontFamily;

    if (m_LogManager != nullptr)
    {
        m_LogManager->Logf(5, "GdiFontTexture", "Examine pixels Width: %d, Height: %d", width, height);
    }

    // Examine raw pixels to get exact texture size(gdiplus does not calculate pixel perfect size)..
    int32_t firstPx = width - 1;
    int32_t lastPx  = 0;
    uint32_t* px    = (uint32_t*)this->m_Pixels;
    // height can sometimes be too small to find the drawn pixels so we double it just to make sure
    int maxHeight   = height * 2;
    for (auto y = 0; y < maxHeight; y++)
    {
        for (auto x = (width - 1); x >= lastPx; x--)
        {
            if (px[x])
            {
                height = y + 1;
                lastPx = x;
            }
        }

        for (auto x = 0; x < width; x++)
        {
            if (px[x])
            {
                height = y + 1;
                if (x < firstPx)
                {
                    firstPx = x;
                }
                break;
            }
        }

        px += this->m_CanvasWidth;
    }
       
    if (m_LogManager != nullptr)
    {
        m_LogManager->Logf(5, "GdiFontTexture", "Examine pixels lastPx: %d, firstPx: %d", lastPx, firstPx);
    }

    width = (lastPx - firstPx) + 1;

    // End early if width or height are 0..
    if ((width <= 0) || (height <= 0))
    {
        return GdiFontReturn_t();
    }

    // Attempt to create texture..
    IDirect3DTexture8* pTexture;
    if (FAILED(::D3DXCreateTexture(this->m_Device, width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &pTexture)))
    {
        return GdiFontReturn_t();
    }
    D3DSURFACE_DESC surfaceDesc;
    if (FAILED(pTexture->GetLevelDesc(0, &surfaceDesc)))
    {
        if (pTexture != nullptr)
            pTexture->Release();

        return GdiFontReturn_t();
    }

    if (m_LogManager != nullptr)
    {
        m_LogManager->Logf(5, "GdiFontTexture", "Copy pixels Width: %d, Height: %d", width, height);
    }

    // Copy rendered font from bitmap to texture..
    D3DLOCKED_RECT rect{};
    auto copyStride = width * 4;
    pTexture->LockRect(0, &rect, 0, 0);
    {
        uint8_t* dest = (uint8_t*)rect.pBits;
        uint8_t* src  = this->m_Pixels + (firstPx * 4);
        for (int x = 0; x < height; x++)
        {
            memcpy(dest, src, copyStride);
            dest += copyStride;
            src += this->m_CanvasStride;
        }
    }
    pTexture->UnlockRect(0);

    // Save physical file if requested
    if (m_SaveToHardDrive)
    {
        BITMAPV4HEADER bmp   = {sizeof(BITMAPV4HEADER)};
        bmp.bV4Width         = width;
        bmp.bV4Height        = height;
        bmp.bV4Planes        = 1;
        bmp.bV4BitCount      = 32;
        bmp.bV4V4Compression = BI_BITFIELDS;
        bmp.bV4RedMask       = 0x00FF0000;
        bmp.bV4GreenMask     = 0x0000FF00;
        bmp.bV4BlueMask      = 0x000000FF;
        bmp.bV4AlphaMask     = 0xFF000000;

        uint8_t* pPixels      = nullptr;
        HBITMAP pBmp          = ::CreateDIBSection(nullptr, (BITMAPINFO*)&bmp, DIB_RGB_COLORS, (void**)&pPixels, nullptr, 0);
        Gdiplus::Bitmap* pRaw = new Gdiplus::Bitmap(width, height, width * 4, PixelFormat32bppARGB, (BYTE*)pPixels);

        uint8_t* src = this->m_Pixels + (firstPx * 4);
        for (int x = 0; x < height; x++)
        {
            memcpy(pPixels, src, copyStride);
            pPixels += copyStride;
            src += this->m_CanvasStride;
        }

        CLSID pngClsid;
        GetEncoderClsid(L"image/png", &pngClsid);
        auto index = 0;
        wchar_t nameBuffer[256];
        swprintf_s(nameBuffer, L"%S\\font_%u.png", m_SavePath, index);
        while (std::filesystem::exists(nameBuffer))
        {
            index++;
            swprintf_s(nameBuffer, L"%S\\font_%u.png", m_SavePath, index);
        }
        pRaw->Save(nameBuffer, &pngClsid, NULL);
        delete pRaw;
        DeleteObject(pBmp);
    }

    // Create return object..
    GdiFontReturn_t ret;
    ret.Width   = width;
    ret.Height  = height;
    ret.Texture = pTexture;
    return ret;
}
Gdiplus::GraphicsPath* CreateRoundedRectPath(Gdiplus::Rect rect, int radius)
{
    Gdiplus::GraphicsPath* pPath = new Gdiplus::GraphicsPath();
    if (radius == 0)
        pPath->AddRectangle(rect);
    else
    {
        int diameter = radius * 2;
        Gdiplus::Rect arc(rect.X, rect.Y, diameter, diameter);
        pPath->AddArc(arc, 180, 90);
        arc.X = (rect.X + rect.Width) - diameter;
        pPath->AddArc(arc, 270, 90);
        arc.Y = (rect.Y + rect.Height) - diameter;
        pPath->AddArc(arc, 0, 90);
        arc.X = rect.X;
        pPath->AddArc(arc, 90, 90);
        pPath->CloseFigure();
    }
    return pPath;
}

GdiFontReturn_t GdiFontManager::CreateRectTexture(const GdiRectData_t& data)
{
    int width  = data.Width;
    int height = data.Height;

    Gdiplus::Rect drawRect(0, 0, width, height);
    if (data.OutlineWidth != 0)
    {
        auto inset  = data.OutlineWidth / 2;
        auto shrink = data.OutlineWidth;
        if (data.OutlineWidth % 2)
        {
            inset += 1;
            shrink++;
        }
        drawRect = Gdiplus::Rect(inset, inset, width - shrink, height - shrink);
    }
    Gdiplus::GraphicsPath* pPath = CreateRoundedRectPath(drawRect, data.Diameter);

    // Clear necessary space
    this->ClearCanvas(width, height);

    // Fill text if font color isn't fully transparent..
    if (((data.FillColor & 0xFF000000) != 0) || ((data.GradientStyle != 0) && ((data.GradientColor & 0xFF000000) != 0)))
    {
        auto pBrush = GetBrush(data, width, height);
        m_Graphics->FillPath(pBrush, pPath);
        delete pBrush;
    }

    // Draw outline if applicable..
    if ((data.OutlineWidth > 0) && ((data.OutlineColor & 0xFF000000) != 0))
    {
        Gdiplus::GraphicsPath* pOutline = CreateRoundedRectPath(drawRect, data.Diameter);
        Gdiplus::Pen pen(UINT32_TO_COLOR(data.OutlineColor), data.OutlineWidth);
        m_Graphics->DrawPath(&pen, pOutline);
        delete pOutline;
    }

    // Clean up remaining gdiplus objects..
    delete pPath;

    // Attempt to create texture..
    IDirect3DTexture8* pTexture;
    if (FAILED(::D3DXCreateTexture(this->m_Device, width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &pTexture)))
    {
        return GdiFontReturn_t();
    }
    D3DSURFACE_DESC surfaceDesc;
    if (FAILED(pTexture->GetLevelDesc(0, &surfaceDesc)))
    {
        if (pTexture != nullptr)
            pTexture->Release();

        return GdiFontReturn_t();
    }

    // Copy rendered font from bitmap to texture..
    D3DLOCKED_RECT rect{};
    auto copyStride = width * 4;
    pTexture->LockRect(0, &rect, 0, 0);
    {
        uint8_t* dest = (uint8_t*)rect.pBits;
        uint8_t* src  = this->m_Pixels;
        for (int x = 0; x < height; x++)
        {
            memcpy(dest, src, copyStride);
            dest += copyStride;
            src += this->m_CanvasStride;
        }
    }
    pTexture->UnlockRect(0);

    // Save physical file if requested
    if (m_SaveToHardDrive)
    {
        BITMAPV4HEADER bmp   = {sizeof(BITMAPV4HEADER)};
        bmp.bV4Width         = width;
        bmp.bV4Height        = height;
        bmp.bV4Planes        = 1;
        bmp.bV4BitCount      = 32;
        bmp.bV4V4Compression = BI_BITFIELDS;
        bmp.bV4RedMask       = 0x00FF0000;
        bmp.bV4GreenMask     = 0x0000FF00;
        bmp.bV4BlueMask      = 0x000000FF;
        bmp.bV4AlphaMask     = 0xFF000000;

        uint8_t* pPixels      = nullptr;
        HBITMAP pBmp          = ::CreateDIBSection(nullptr, (BITMAPINFO*)&bmp, DIB_RGB_COLORS, (void**)&pPixels, nullptr, 0);
        Gdiplus::Bitmap* pRaw = new Gdiplus::Bitmap(width, height, width * 4, PixelFormat32bppARGB, (BYTE*)pPixels);

        uint8_t* src = this->m_Pixels;
        for (int x = 0; x < height; x++)
        {
            memcpy(pPixels, src, copyStride);
            pPixels += copyStride;
            src += this->m_CanvasStride;
        }

        CLSID pngClsid;
        GetEncoderClsid(L"image/png", &pngClsid);
        auto index = 0;
        wchar_t nameBuffer[256];
        swprintf_s(nameBuffer, L"%S\\rect_%u.png", m_SavePath, index);
        while (std::filesystem::exists(nameBuffer))
        {
            index++;
            swprintf_s(nameBuffer, L"%S\\rect_%u.png", m_SavePath, index);
        }
        pRaw->Save(nameBuffer, &pngClsid, NULL);
        delete pRaw;
        DeleteObject(pBmp);
    }

    // Create return object..
    GdiFontReturn_t ret;
    ret.Width   = width;
    ret.Height  = height;
    ret.Texture = pTexture;
    return ret;
}

Gdiplus::Color GdiFontManager::UINT32_TO_COLOR(uint32_t color)
{
    auto alpha = (color & 0xFF000000) >> 24;
    auto red   = (color & 0x00FF0000) >> 16;
    auto green = (color & 0x0000FF00) >> 8;
    auto blue  = (color & 0x000000FF);
    return Gdiplus::Color(alpha, red, green, blue);
}

void GdiFontManager::ClearCanvas(int width, int height)
{
    auto clearStride = width * 4;
    auto pixels      = this->m_Pixels;
    for (int x = 0; x < height; x++)
    {
        memset(pixels, 0, clearStride);
        pixels += this->m_CanvasStride;
    }
}

Gdiplus::Brush* GdiFontManager::GetBrush(const GdiFontData_t& data, int width, int height)
{
    if (data.GradientStyle == 0)
    {
        auto color = UINT32_TO_COLOR(data.FontColor);
        return new Gdiplus::SolidBrush(color);
    }

    Gdiplus::Point start(0, 0);
    Gdiplus::Point end(0, 0);
    switch (data.GradientStyle)
    {
        //Left to right
        case 1:
            end.X = width;
            break;

        //Top-Left to Bottom Right
        case 2:
            end.X = width;
            end.Y = height;
            break;

        //Top to bottom
        case 3:
            end.Y = height;
            break;

        //Top-Right to Bottom Left
        case 4:
            start.X = width;
            end.Y   = height;
            break;

        //Right to Left
        case 5:
            start.X = width;
            break;

        //Bottom-Right to Top Left
        case 6:
            start.X = width;
            start.Y = height;
            break;

        //Bottom to Top
        case 7:
            start.Y = height;
            break;

        //Bottom-Left to Top Right
        case 8:
            start.Y = height;
            end.X   = width;
            break;

        default:
            end.X = width;
            break;
    }

    return new Gdiplus::LinearGradientBrush(start, end, UINT32_TO_COLOR(data.FontColor), UINT32_TO_COLOR(data.GradientColor));
}

Gdiplus::Brush* GdiFontManager::GetBrush(const GdiRectData_t& data, int width, int height)
{
    if (data.GradientStyle == 0)
    {
        auto color = UINT32_TO_COLOR(data.FillColor);
        return new Gdiplus::SolidBrush(color);
    }

    Gdiplus::Point start(0, 0);
    Gdiplus::Point end(0, 0);

    switch (data.GradientStyle)
    {
        //Left to right
        case 1:
            end.X = width;
            break;

        //Top-Left to Bottom Right
        case 2:
            end.X = width;
            end.Y = height;
            break;

        //Top to bottom
        case 3:
            end.Y = height;
            break;

        //Top-Right to Bottom Left
        case 4:
            start.X = width;
            end.Y   = height;
            break;

        //Right to Left
        case 5:
            start.X = width;
            break;

        //Bottom-Right to Top Left
        case 6:
            start.X = width;
            start.Y = height;
            break;

        //Bottom to Top
        case 7:
            start.Y = height;
            break;

        //Bottom-Left to Top Right
        case 8:
            start.Y = height;
            end.X   = width;
            break;

        default:
            end.X = width;
            break;
    }

    return new Gdiplus::LinearGradientBrush(start, end, UINT32_TO_COLOR(data.FillColor), UINT32_TO_COLOR(data.GradientColor));
}

void GdiFontManager::EnableTextureDump(const char* folder)
{
    strcpy_s(m_SavePath, 1024, folder);
    m_SaveToHardDrive = true;
}
void GdiFontManager::DisableTextureDump()
{
    m_SaveToHardDrive = false;
}