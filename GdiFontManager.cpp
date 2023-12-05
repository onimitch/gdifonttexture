#include "GdiFontManager.h"
#include <locale>

GdiFontManager::GdiFontManager(IDirect3DDevice8* pDevice)
    : m_Device(pDevice)
    , m_CanvasWidth(2048)
    , m_CanvasHeight(2048)
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
}

GdiFontManager::~GdiFontManager()
{
    delete this->m_Graphics;
    delete this->m_Bitmap;
    free(m_RawImage);
    Gdiplus::GdiplusShutdown(m_GDIToken);
}

GdiFontReturn_t GdiFontManager::CreateFontTexture(GdiFontData_t data)
{
    if (data.BoxHeight == 0)
        data.BoxHeight = m_CanvasHeight;
    if (data.BoxWidth == 0)
        data.BoxWidth = m_CanvasWidth;

    // Attempt to set up font family..
    wchar_t wBuffer[4096];
    ::MultiByteToWideChar(CP_UTF8, 0, data.FontFamily, -1, wBuffer, 4096);
    Gdiplus::FontFamily* pFontFamily = new Gdiplus::FontFamily(wBuffer);
    if (pFontFamily->GetLastStatus() != Gdiplus::Ok)
    {
        delete pFontFamily;
        return GdiFontReturn_t();
    }

    // Attempt to create graphics path..
    ::MultiByteToWideChar(CP_UTF8, 0, data.FontText, -1, wBuffer, 4096);
    auto length = wcslen(wBuffer);
    Gdiplus::Rect pathRect(0, 0, data.BoxWidth, data.BoxHeight);
    Gdiplus::StringFormat fontFormat;
    fontFormat.SetAlignment(Gdiplus::StringAlignment::StringAlignmentNear);
    Gdiplus::GraphicsPath* pPath = new Gdiplus::GraphicsPath();
    pPath->AddString(wBuffer, length, pFontFamily, data.FontFlags, data.FontHeight, pathRect, &fontFormat);
    if (pPath->GetLastStatus() != Gdiplus::Ok)
    {
        delete pPath;
        delete pFontFamily;
        return GdiFontReturn_t();
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

    // Clear necessary space using calculated path size.
    int32_t width  = (int32_t)ceil(box.Width);
    int32_t height = (int32_t)ceil(box.Height);
    this->ClearCanvas(width, height);

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

    // Examine raw pixels to get exact texture size(gdiplus does not calculate pixel perfect size)..
    int32_t firstPx = width - 1;
    int32_t lastPx  = 0;
    uint32_t* px    = (uint32_t*)this->m_Pixels;
    for (auto y = 0; y < height; y++)
    {
        for (auto x = 0; x < firstPx; x++)
        {
            if (px[x])
                firstPx = x;
        }

        for (auto x = (width - 1); x > lastPx; x--)
        {
            if (px[x])
                lastPx = x;
        }

        px += this->m_CanvasStride;
    }
    width = (lastPx - firstPx) + 1;

    // End early if width or height are 0..
    if ((width == 0) || (height == 0))
        return GdiFontReturn_t();

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
    pTexture->LockRect(0, &rect, 0, 0);
    {
        auto copyStride = width * 4;
        uint8_t* dest   = (uint8_t*)rect.pBits;
        uint8_t* src    = this->m_Pixels + (firstPx * 4);
        for (int x = 0; x < height; x++)
        {
            memcpy(dest, src, copyStride);
            dest += copyStride;
            src += this->m_CanvasStride;
        }
    }
    pTexture->UnlockRect(0);

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

GdiFontReturn_t GdiFontManager::CreateRectTexture(GdiRectData_t data)
{
    int width          = data.Width;
    int height         = data.Height;

    Gdiplus::Rect drawRect(0, 0, width, height);
    if (data.OutlineWidth != 0)
    {
        auto inset  = data.OutlineWidth / 2;
        auto shrink  = data.OutlineWidth;
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
    pTexture->LockRect(0, &rect, 0, 0);
    {
        auto copyStride = width * 4;
        uint8_t* dest   = (uint8_t*)rect.pBits;
        uint8_t* src    = this->m_Pixels;
        for (int x = 0; x < height; x++)
        {
            memcpy(dest, src, copyStride);
            dest += copyStride;
            src += this->m_CanvasStride;
        }
    }
    pTexture->UnlockRect(0);

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

Gdiplus::Brush* GdiFontManager::GetBrush(GdiFontData_t data, int width, int height)
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

Gdiplus::Brush* GdiFontManager::GetBrush(GdiRectData_t data, int width, int height)
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