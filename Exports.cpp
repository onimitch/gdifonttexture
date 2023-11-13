#include "GdiFontManager.h"
#include <string>

int CALLBACK EnumFontFamExProc(const LOGFONT* lpelfe, const TEXTMETRIC* lpntme, DWORD FontType, LPARAM lParam)
{
    *((LPARAM*)lParam) = 1;
    return 1;
}

extern "C"
{
    extern __declspec(dllexport) GdiFontManager* CreateFontManager(IDirect3DDevice8* pDirect3DDevice)
    {
        return new GdiFontManager(pDirect3DDevice);
    }
    extern __declspec(dllexport) void DestroyFontManager(GdiFontManager* pFontManager)
    {
        delete pFontManager;
    }
    extern __declspec(dllexport) GdiFontReturn_t CreateTexture(GdiFontManager* pFontManager, GdiFontData_t* data)
    {
        return pFontManager->CreateFontTexture(*data);
    }
    extern __declspec(dllexport) bool GetFontAvailable(const char* font)
    {
        LOGFONT lf = {0};
        lf.lfCharSet = DEFAULT_CHARSET;
        swprintf_s(lf.lfFaceName, 32, L"%S", font);
        LPARAM lParam = 0;
        ::EnumFontFamiliesEx(GetDC(nullptr), &lf, EnumFontFamExProc, (LPARAM)&lParam, 0);
        return lParam ? true : false;
    }
    extern __declspec(dllexport) const char* ShiftJIS_To_UTF8(const char* input)
    {
        WCHAR wideBuffer[4096];
        ::MultiByteToWideChar(932, 0, input, -1, wideBuffer, 4096);
        static char buffer[4096];
        WideCharToMultiByte(CP_UTF8, 0, wideBuffer, -1, buffer, 4096, 0, 0);
        return buffer;
    }
    extern __declspec(dllexport) const char* UTF8_To_ShiftJIS(const char* input)
    {
        WCHAR wideBuffer[4096];
        ::MultiByteToWideChar(CP_UTF8, 0, input, -1, wideBuffer, 4096);
        static char buffer[4096];
        WideCharToMultiByte(932, 0, wideBuffer, -1, buffer, 4096, 0, 0);
        return buffer;
    }
}