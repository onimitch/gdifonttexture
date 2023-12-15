#ifndef __GdiFontTxDefines__
#define __GdiFontTxDefines__

#include <atlbase.h>
#include "includes/d3d8/includes/d3d8.h"
#include "includes/d3d8/includes/d3dx8core.h"
#pragma comment(lib, "includes/d3d8/lib/d3dx8.lib")
#include <gdiplus.h>
#pragma comment(lib, "Gdiplus.lib")
#include <stdint.h>

struct GdiCharRange_t
{
    INT First;
    INT Length;
};

struct GdiRegion_t
{
    uint32_t FontColor;
    uint32_t OutlineColor;
    GdiCharRange_t* Ranges = nullptr;
    int32_t RangesLength;
};

struct GdiFontData_t
{
    int32_t BoxHeight;
    int32_t BoxWidth;
    float_t FontHeight;
    float_t OutlineWidth;
    int32_t FontFlags;
    uint32_t FontColor;
    uint32_t OutlineColor;
    uint32_t GradientStyle;
    uint32_t GradientColor;
    const char* FontFamily = nullptr;
    const char* FontText = nullptr;
    GdiRegion_t* Regions = nullptr;
    int32_t RegionsLength;
};

struct GdiRectData_t
{
    int32_t Width;
    int32_t Height;
    int32_t Diameter;
    uint32_t OutlineColor;
    uint32_t OutlineWidth;
    uint32_t FillColor;
    uint32_t GradientStyle;
    uint32_t GradientColor;
};

struct GdiFontReturn_t
{
    int32_t Width;
    int32_t Height;
    IDirect3DTexture8* Texture;

    GdiFontReturn_t()
        : Width(0)
        , Height(0)
        , Texture(nullptr)
    {}
};

#endif