// ============================================================================
//  CoreBase · CPU & GPU 图鉴
//  ---------------------------------------------------------------------------
//  这个版本包含：
//    1. 原有首页（品牌选择）
//    2. 参照你给的图片实现的 NVIDIA 显卡库交互页
//    3. 页面切换、卡片入场、悬停、筛选、搜索、侧栏展开等平滑动画
//
//  直接把整个 CPU&GPU.cpp 替换为这份代码即可。
//  注意：源码包含中文，请保存为 UTF-8（带 BOM），否则 VS 可能乱码。
//  依赖的 IDB_PNG1 / IDB_PNG2 / IDB_PNG3、IDI_CPUGPU 仍然沿用原工程资源。
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <wincodec.h>
#include "Resource.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <string>
#include <vector>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

#define IDR_LOGO_NVIDIA IDB_PNG3
#define IDR_LOGO_AMD    IDB_PNG1
#define IDR_LOGO_INTEL  IDB_PNG2

// ============================================================================
//  基本数据
// ============================================================================

static const float DESIGN_W = 1600.0f;
static const float DESIGN_H = 900.0f;

static const float PANEL_L = 96.0f;
static const float PANEL_T = 62.0f;
static const float PANEL_R = 1504.0f;
static const float PANEL_B = 838.0f;
static const float PANEL_RADIUS = 22.0f;

static const float SIDEBAR_R = 394.0f;
static const float SIDE_TOP = 92.0f;
static const float SIDE_BOTTOM = 826.0f;

static const float CONTENT_L = 454.0f;
static const float CONTENT_R = 1468.0f;
static const float GRID_TOP = 194.0f;
static const float CARD_GAP = 24.0f;
static const float CARD_H = 232.0f;
static const float CARD_W = (CONTENT_R - CONTENT_L - CARD_GAP * 2.0f) / 3.0f;

static const float FOOTER_LINE_Y = 774.0f;
static const float SIDEBAR_ITEM_H = 27.0f;
static const float SIDEBAR_GROUP_H = 34.0f;

enum class Page
{
    Home,
    Library
};

struct GpuCard
{
    std::wstring name;
    std::wstring tag;
    std::wstring series;
    int vram = 0;
    int bus = 0;
    int power = 0;
    int variant = 0;
    D2D1_COLOR_F accent{};

    bool visible = true;
    float hover = 0.0f;
    float hoverTarget = 0.0f;
    float hoverVel = 0.0f;
    float presence = 1.0f;
    float presenceTarget = 1.0f;
    float presenceVel = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float tx = 0.0f;
    float ty = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
};

struct SideGroup
{
    std::wstring title;
    std::vector<std::wstring> items;
    bool expanded = false;
    float expand = 0.0f;
    float expandTarget = 0.0f;
    float expandVel = 0.0f;
};

struct SideRow
{
    int group = -1;
    int item = -1;
    bool header = false;
    D2D1_RECT_F rect{};
};

struct BrandCard
{
    std::wstring name;
    std::wstring caption;
    int logo = 0;
    D2D1_COLOR_F accent{};
    D2D1_RECT_F rect{};
};

struct Fonts
{
    IDWriteTextFormat* h1 = nullptr;
    IDWriteTextFormat* subtitle = nullptr;
    IDWriteTextFormat* cardTitle = nullptr;
    IDWriteTextFormat* tag = nullptr;
    IDWriteTextFormat* specLabel = nullptr;
    IDWriteTextFormat* specValue = nullptr;
    IDWriteTextFormat* specUnit = nullptr;
    IDWriteTextFormat* sideGroup = nullptr;
    IDWriteTextFormat* sideItem = nullptr;
    IDWriteTextFormat* search = nullptr;
    IDWriteTextFormat* count = nullptr;
    IDWriteTextFormat* footer = nullptr;
    IDWriteTextFormat* footerBold = nullptr;
    IDWriteTextFormat* homeTitle = nullptr;
    IDWriteTextFormat* homeKicker = nullptr;
    IDWriteTextFormat* brand = nullptr;
    IDWriteTextFormat* brandMeta = nullptr;
    IDWriteTextFormat* homeFooter = nullptr;
    IDWriteTextFormat* toast = nullptr;
    IDWriteTextFormat* emptyTitle = nullptr;
    IDWriteTextFormat* emptyBody = nullptr;
    IDWriteTextFormat* watermark = nullptr;
};

// ============================================================================
//  Direct2D / DirectWrite
// ============================================================================

static HWND g_hwnd = nullptr;
static HINSTANCE g_instance = nullptr;

static ID2D1Factory* g_d2dFactory = nullptr;
static ID2D1HwndRenderTarget* g_rt = nullptr;
static IDWriteFactory* g_dwrite = nullptr;
static IWICImagingFactory* g_wic = nullptr;

static ID2D1SolidColorBrush* g_solid = nullptr;
static ID2D1LinearGradientBrush* g_bgGrad = nullptr;
static ID2D1LinearGradientBrush* g_titleGrad = nullptr;
static ID2D1LinearGradientBrush* g_homeTitleGrad = nullptr;
static ID2D1RadialGradientBrush* g_glowPurple = nullptr;
static ID2D1RadialGradientBrush* g_glowBlue = nullptr;
static ID2D1RadialGradientBrush* g_glowGreen = nullptr;
static ID2D1RadialGradientBrush* g_glowRed = nullptr;

static ID2D1Bitmap* g_logos[3] = { nullptr, nullptr, nullptr };

static Fonts g_fonts;

// 窗口 / DPI / 画布
static UINT g_dpi = 96;
static float g_dpiScale = 1.0f;
static float g_clientW = 1600.0f;
static float g_clientH = 900.0f;
static float g_viewScale = 1.0f;
static D2D1_POINT_2F g_viewOrigin{ 0.0f, 0.0f };
static D2D1_MATRIX_3X2_F g_view = D2D1::Matrix3x2F::Identity();

// 动画
static bool g_timerRunning = false;
static std::chrono::steady_clock::time_point g_lastTick;
static float g_time = 0.0f;
static float g_homeIntro = 0.0f;
static float g_libIntro = 0.0f;
static float g_fanAngle = 0.0f;

// 页面
static Page g_page = Page::Home;
static Page g_navFrom = Page::Home;
static Page g_navTo = Page::Library;
static bool g_navActive = false;
static float g_navT = 0.0f;
static int g_navDirection = 1;

// 鼠标
static POINT g_mousePx{ -1, -1 };
static D2D1_POINT_2F g_mouse{ -1000.0f, -1000.0f };
static bool g_mouseInside = false;
static bool g_mouseTracking = false;
static bool g_handCursor = false;
static HCURSOR g_arrowCursor = nullptr;
static HCURSOR g_handCursorHandle = nullptr;

// 首页
static BrandCard g_brands[3];
static int g_homeHover = -1;
static float g_homeHoverV[3] = { 0.0f, 0.0f, 0.0f };
static float g_homeHoverTarget[3] = { 0.0f, 0.0f, 0.0f };
static float g_homeHoverVel[3] = { 0.0f, 0.0f, 0.0f };

// 显卡数据
static std::vector<GpuCard> g_cards;
static int g_hoverCard = -1;
static int g_selectedCard = 0;
static int g_visibleCount = 0;
static float g_countShown = 0.0f;
static float g_countPulse = 0.0f;
static float g_emptyT = 0.0f;
static float g_emptyTarget = 0.0f;
static float g_emptyVel = 0.0f;
static float g_emptyHoverT = 0.0f;
static D2D1_RECT_F g_emptyResetRect{ 900.0f, 470.0f, 1022.0f, 506.0f };

// 侧栏
static std::vector<SideGroup> g_sideGroups;
static int g_sideHoverRow = -1;
static float g_sideHoverT = 0.0f;
static float g_sideScroll = 0.0f;
static float g_sideScrollTarget = 0.0f;
static float g_sideScrollVel = 0.0f;
static float g_sideScrollMax = 0.0f;
static bool g_sideScrollToBottom = false;
static std::wstring g_seriesFilter;

// 搜索 / 筛选
static std::wstring g_searchText;
static bool g_searchFocused = false;
static float g_searchFocusT = 0.0f;
static float g_searchFocusVel = 0.0f;
static float g_searchHoverT = 0.0f;
static float g_searchHoverVel = 0.0f;
static float g_clearHoverT = 0.0f;
static float g_clearHoverVel = 0.0f;
static bool g_filterOpen = false;
static float g_filterOpenT = 0.0f;
static float g_filterOpenVel = 0.0f;
static int g_filterHover = -1;
static int g_tagFilter = -1;             // -1 全部，0 旗舰，1 高端，2 中端，3 主流
static const wchar_t* g_tagNames[4] = { L"旗舰", L"高端", L"中端", L"主流" };

// 顶栏按钮
static D2D1_RECT_F g_backRect{ 404.0f, 100.0f, 438.0f, 134.0f };
static D2D1_RECT_F g_searchRect{ 1090.0f, 101.0f, 1380.0f, 143.0f };
static D2D1_RECT_F g_filterRect{ 1390.0f, 101.0f, 1468.0f, 143.0f };
static float g_backHoverT = 0.0f;
static float g_backHoverVel = 0.0f;
static float g_filterHoverT = 0.0f;
static float g_filterHoverVel = 0.0f;
static float g_emptyHoverVel = 0.0f;
static float g_sideHoverVel = 0.0f;
static float g_countVel = 0.0f;

// 这些目标值用于 UpdateHoverTargets 和绘制阶段共享
static float g_filterHoverTarget = 0.0f;
static float g_searchHoverTarget = 0.0f;
static float g_clearHoverTarget = 0.0f;
static float g_emptyHoverTarget = 0.0f;

// Toast
static std::wstring g_toastText;
static D2D1_COLOR_F g_toastColor{};
static float g_toastTime = 0.0f;
static bool g_toastVisible = false;

// ============================================================================
//  小工具
// ============================================================================

static inline D2D1_COLOR_F Col(int r, int g, int b, int a = 255)
{
    return D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

static inline D2D1_COLOR_F Alpha(D2D1_COLOR_F c, float a)
{
    c.a *= a;
    return c;
}

static inline float Clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float Lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

static inline float EaseOutCubic(float t)
{
    t = Clamp01(t);
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

static inline float EaseInOutCubic(float t)
{
    t = Clamp01(t);
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

static inline float SmoothStep(float a, float b, float x)
{
    if (a == b) return x >= b ? 1.0f : 0.0f;
    float t = Clamp01((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

static float SmoothDamp(float cur, float target, float& vel, float smoothTime, float dt)
{
    smoothTime = std::max(0.0001f, smoothTime);
    float omega = 2.0f / smoothTime;
    float x = omega * dt;
    float expo = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float change = cur - target;
    float temp = (vel + omega * change) * dt;
    vel = (vel - omega * temp) * expo;
    return target + (change + temp) * expo;
}

static inline bool InRect(const D2D1_RECT_F& r, float x, float y)
{
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

static inline bool InRect(const D2D1_RECT_F& r, const D2D1_POINT_2F& p)
{
    return InRect(r, p.x, p.y);
}

static D2D1_ROUNDED_RECT RR(const D2D1_RECT_F& r, float radius)
{
    D2D1_ROUNDED_RECT rr;
    rr.rect = r;
    rr.radiusX = radius;
    rr.radiusY = radius;
    return rr;
}

static std::wstring Lower(std::wstring s)
{
    for (wchar_t& ch : s)
        ch = (wchar_t)std::towlower(ch);
    return s;
}

static bool ContainsNoCase(const std::wstring& haystack, const std::wstring& needle)
{
    if (needle.empty()) return true;
    return Lower(haystack).find(Lower(needle)) != std::wstring::npos;
}

static D2D1_RECT_F CardSlotRect(int slot)
{
    int col = slot % 3;
    int row = slot / 3;
    float x = CONTENT_L + col * (CARD_W + CARD_GAP);
    float y = GRID_TOP + row * (CARD_H + CARD_GAP);
    return D2D1::RectF(x, y, x + CARD_W, y + CARD_H);
}

static D2D1_RECT_F HomeCardRect(int i)
{
    const float w = 260.0f;
    const float h = 292.0f;
    const float gap = 42.0f;
    const float total = 3.0f * w + 2.0f * gap;
    const float x = (DESIGN_W - total) * 0.5f + i * (w + gap);
    return D2D1::RectF(x, 354.0f, x + w, 354.0f + h);
}

static D2D1_RECT_F SearchClearRect()
{
    return D2D1::RectF(1346.0f, 108.0f, 1374.0f, 136.0f);
}

static D2D1_RECT_F FilterPopupRect()
{
    return D2D1::RectF(1320.0f, 152.0f, 1468.0f, 152.0f + 5.0f * 32.0f + 14.0f);
}

static D2D1_RECT_F FilterPopupRowRect(int row)
{
    D2D1_RECT_F p = FilterPopupRect();
    return D2D1::RectF(p.left + 7.0f, p.top + 7.0f + row * 32.0f, p.right - 7.0f,
        p.top + 7.0f + row * 32.0f + 30.0f);
}

static float CardEnter(int index)
{
    float delay = index * 0.055f;
    float t = Clamp01((g_libIntro - delay) / 0.56f);
    return EaseOutCubic(t);
}

// ============================================================================
//  资源创建
// ============================================================================

static void UpdateViewMetrics()
{
    RECT rc{};
    GetClientRect(g_hwnd, &rc);
    g_dpi = GetDpiForWindow(g_hwnd);
    if (g_dpi == 0) g_dpi = 96;
    g_dpiScale = g_dpi / 96.0f;

    float pxW = (float)std::max<LONG>(1, rc.right);
    float pxH = (float)std::max<LONG>(1, rc.bottom);
    g_clientW = pxW / g_dpiScale;
    g_clientH = pxH / g_dpiScale;

    g_viewScale = std::min(g_clientW / DESIGN_W, g_clientH / DESIGN_H);
    g_viewOrigin.x = (g_clientW - DESIGN_W * g_viewScale) * 0.5f;
    g_viewOrigin.y = (g_clientH - DESIGN_H * g_viewScale) * 0.5f;
    g_view = D2D1::Matrix3x2F::Scale(g_viewScale, g_viewScale) *
        D2D1::Matrix3x2F::Translation(g_viewOrigin.x, g_viewOrigin.y);
}

static IDWriteTextFormat* MakeTextFormat(float size, DWRITE_FONT_WEIGHT weight,
    const wchar_t* family = L"Microsoft YaHei UI")
{
    IDWriteTextFormat* format = nullptr;
    if (!g_dwrite) return nullptr;

    HRESULT hr = g_dwrite->CreateTextFormat(family, nullptr, weight,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", &format);
    if (FAILED(hr)) return nullptr;

    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return format;
}

static void CreateFonts()
{
    g_fonts.h1 = MakeTextFormat(38.0f, DWRITE_FONT_WEIGHT_BOLD);
    g_fonts.subtitle = MakeTextFormat(14.0f, DWRITE_FONT_WEIGHT_NORMAL);
    g_fonts.cardTitle = MakeTextFormat(22.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    g_fonts.tag = MakeTextFormat(12.5f, DWRITE_FONT_WEIGHT_NORMAL);
    g_fonts.specLabel = MakeTextFormat(11.5f, DWRITE_FONT_WEIGHT_NORMAL);
    g_fonts.specValue = MakeTextFormat(16.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    g_fonts.specUnit = MakeTextFormat(10.5f, DWRITE_FONT_WEIGHT_NORMAL);
    g_fonts.sideGroup = MakeTextFormat(14.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    g_fonts.sideItem = MakeTextFormat(14.0f, DWRITE_FONT_WEIGHT_NORMAL);
    g_fonts.search = MakeTextFormat(14.0f, DWRITE_FONT_WEIGHT_NORMAL);
    g_fonts.count = MakeTextFormat(13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    g_fonts.footer = MakeTextFormat(12.5f, DWRITE_FONT_WEIGHT_NORMAL);
    g_fonts.footerBold = MakeTextFormat(13.0f, DWRITE_FONT_WEIGHT_BOLD);
    g_fonts.homeTitle = MakeTextFormat(62.0f, DWRITE_FONT_WEIGHT_BOLD);
    g_fonts.homeKicker = MakeTextFormat(10.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    g_fonts.brand = MakeTextFormat(25.0f, DWRITE_FONT_WEIGHT_BOLD);
    g_fonts.brandMeta = MakeTextFormat(11.5f, DWRITE_FONT_WEIGHT_NORMAL);
    g_fonts.homeFooter = MakeTextFormat(10.5f, DWRITE_FONT_WEIGHT_NORMAL);
    g_fonts.toast = MakeTextFormat(12.5f, DWRITE_FONT_WEIGHT_NORMAL);
    g_fonts.emptyTitle = MakeTextFormat(20.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    g_fonts.emptyBody = MakeTextFormat(12.5f, DWRITE_FONT_WEIGHT_NORMAL);
    g_fonts.watermark = MakeTextFormat(230.0f, DWRITE_FONT_WEIGHT_BOLD);
}

static void DestroyFonts()
{
    IDWriteTextFormat** formats[] = {
        &g_fonts.h1, &g_fonts.subtitle, &g_fonts.cardTitle, &g_fonts.tag,
        &g_fonts.specLabel, &g_fonts.specValue, &g_fonts.specUnit,
        &g_fonts.sideGroup, &g_fonts.sideItem, &g_fonts.search, &g_fonts.count,
        &g_fonts.footer, &g_fonts.footerBold, &g_fonts.homeTitle,
        &g_fonts.homeKicker, &g_fonts.brand, &g_fonts.brandMeta,
        &g_fonts.homeFooter, &g_fonts.toast, &g_fonts.emptyTitle,
        &g_fonts.emptyBody, &g_fonts.watermark
    };
    for (auto* p : formats)
    {
        if (*p) { (*p)->Release(); *p = nullptr; }
    }
}

static ID2D1Bitmap* LoadLogoFromResource(int resId)
{
    if (!g_rt || !g_wic) return nullptr;

    HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(resId), RT_RCDATA);
    if (!hRes) hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(resId), L"PNG");
    if (!hRes) return nullptr;

    HGLOBAL hResData = LoadResource(nullptr, hRes);
    if (!hResData) return nullptr;

    DWORD size = SizeofResource(nullptr, hRes);
    void* pSrc = LockResource(hResData);
    if (!pSrc || size == 0) return nullptr;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hMem) return nullptr;
    void* pMem = GlobalLock(hMem);
    if (!pMem)
    {
        GlobalFree(hMem);
        return nullptr;
    }
    memcpy(pMem, pSrc, size);
    GlobalUnlock(hMem);

    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(hMem, TRUE, &stream)))
    {
        GlobalFree(hMem);
        return nullptr;
    }

    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    ID2D1Bitmap* bmp = nullptr;

    HRESULT hr = g_wic->CreateDecoderFromStream(stream, nullptr,
        WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = g_wic->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr))
    {
        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppPRGBA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut);
    }
    if (SUCCEEDED(hr)) hr = g_rt->CreateBitmapFromWicBitmap(converter, nullptr, &bmp);

    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    return bmp;
}

static void CreateScreenBackgroundBrush()
{
    if (!g_rt) return;
    if (g_bgGrad) { g_bgGrad->Release(); g_bgGrad = nullptr; }

    D2D1_GRADIENT_STOP stops[] = {
        { 0.00f, Col(6, 9, 18) },
        { 0.45f, Col(9, 10, 24) },
        { 0.78f, Col(14, 9, 31) },
        { 1.00f, Col(20, 10, 38) }
    };
    ID2D1GradientStopCollection* collection = nullptr;
    if (FAILED(g_rt->CreateGradientStopCollection(stops, ARRAYSIZE(stops),
        D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &collection)))
        return;

    g_rt->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(0.0f, 0.0f),
            D2D1::Point2F(g_clientW, g_clientH)),
        D2D1::BrushProperties(), collection, &g_bgGrad);
    collection->Release();
}

static ID2D1RadialGradientBrush* CreateGlowBrush(int r, int g, int b, int centerAlpha)
{
    if (!g_rt) return nullptr;

    D2D1_GRADIENT_STOP stops[] = {
        { 0.00f, Col(r, g, b, centerAlpha) },
        { 0.42f, Col(r, g, b, centerAlpha / 3) },
        { 0.74f, Col(r, g, b, centerAlpha / 10) },
        { 1.00f, Col(r, g, b, 0) }
    };
    ID2D1GradientStopCollection* collection = nullptr;
    if (FAILED(g_rt->CreateGradientStopCollection(stops, ARRAYSIZE(stops),
        D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &collection)))
        return nullptr;

    ID2D1RadialGradientBrush* brush = nullptr;
    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES props =
        D2D1::RadialGradientBrushProperties(D2D1::Point2F(0.0f, 0.0f),
            D2D1::Point2F(0.0f, 0.0f), 1.0f, 1.0f);
    g_rt->CreateRadialGradientBrush(props, D2D1::BrushProperties(), collection, &brush);
    collection->Release();
    return brush;
}

static void CreateStaticGradients()
{
    if (!g_rt) return;

    // 标题渐变
    {
        D2D1_GRADIENT_STOP stops[] = {
            { 0.00f, Col(116, 246, 158) },
            { 0.55f, Col(170, 255, 197) },
            { 1.00f, Col(220, 255, 232) }
        };
        ID2D1GradientStopCollection* collection = nullptr;
        g_rt->CreateGradientStopCollection(stops, ARRAYSIZE(stops),
            D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &collection);
        g_rt->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(
                D2D1::Point2F(CONTENT_L, 100.0f),
                D2D1::Point2F(900.0f, 100.0f)),
            D2D1::BrushProperties(), collection, &g_titleGrad);
        if (collection) collection->Release();
    }

    // 首页标题渐变
    {
        D2D1_GRADIENT_STOP stops[] = {
            { 0.00f, Col(118, 235, 255) },
            { 0.52f, Col(205, 232, 255) },
            { 1.00f, Col(215, 177, 255) }
        };
        ID2D1GradientStopCollection* collection = nullptr;
        g_rt->CreateGradientStopCollection(stops, ARRAYSIZE(stops),
            D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &collection);
        g_rt->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(
                D2D1::Point2F(620.0f, 150.0f),
                D2D1::Point2F(980.0f, 220.0f)),
            D2D1::BrushProperties(), collection, &g_homeTitleGrad);
        if (collection) collection->Release();
    }

    g_glowPurple = CreateGlowBrush(164, 70, 255, 128);
    g_glowBlue = CreateGlowBrush(48, 130, 255, 105);
    g_glowGreen = CreateGlowBrush(94, 245, 150, 90);
    g_glowRed = CreateGlowBrush(238, 80, 70, 90);
}

static void CreateDeviceResources()
{
    if (!g_d2dFactory || !g_hwnd) return;

    RECT rc{};
    GetClientRect(g_hwnd, &rc);
    UINT pxW = (UINT)std::max<LONG>(1, rc.right);
    UINT pxH = (UINT)std::max<LONG>(1, rc.bottom);

    g_d2dFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(g_hwnd, D2D1::SizeU(pxW, pxH)),
        &g_rt);
    if (!g_rt) return;

    g_rt->SetDpi((float)g_dpi, (float)g_dpi);
    g_rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    g_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    g_rt->CreateSolidColorBrush(Col(255, 255, 255), &g_solid);
    CreateStaticGradients();
    CreateScreenBackgroundBrush();

    for (int i = 0; i < 3; ++i)
    {
        if (g_logos[i]) { g_logos[i]->Release(); g_logos[i] = nullptr; }
        static const int logoRes[3] = { IDR_LOGO_NVIDIA, IDR_LOGO_AMD, IDR_LOGO_INTEL };
        g_logos[i] = LoadLogoFromResource(logoRes[i]);
    }
}

static void DiscardDeviceResources()
{
    for (int i = 0; i < 3; ++i)
    {
        if (g_logos[i]) { g_logos[i]->Release(); g_logos[i] = nullptr; }
    }
    if (g_titleGrad) { g_titleGrad->Release(); g_titleGrad = nullptr; }
    if (g_homeTitleGrad) { g_homeTitleGrad->Release(); g_homeTitleGrad = nullptr; }
    if (g_glowPurple) { g_glowPurple->Release(); g_glowPurple = nullptr; }
    if (g_glowBlue) { g_glowBlue->Release(); g_glowBlue = nullptr; }
    if (g_glowGreen) { g_glowGreen->Release(); g_glowGreen = nullptr; }
    if (g_glowRed) { g_glowRed->Release(); g_glowRed = nullptr; }
    if (g_bgGrad) { g_bgGrad->Release(); g_bgGrad = nullptr; }
    if (g_solid) { g_solid->Release(); g_solid = nullptr; }
    if (g_rt) { g_rt->Release(); g_rt = nullptr; }
}

// ============================================================================
//  绘图基础
// ============================================================================

static void SetSolid(D2D1_COLOR_F c)
{
    if (g_solid) g_solid->SetColor(c);
}

static void FillRect(const D2D1_RECT_F& r, D2D1_COLOR_F c)
{
    SetSolid(c);
    g_rt->FillRectangle(r, g_solid);
}

static void FillRRect(const D2D1_RECT_F& r, float radius, D2D1_COLOR_F c)
{
    SetSolid(c);
    g_rt->FillRoundedRectangle(RR(r, radius), g_solid);
}

static void FillRRect(float l, float t, float r, float b, float radius, D2D1_COLOR_F c)
{
    FillRRect(D2D1::RectF(l, t, r, b), radius, c);
}

static void StrokeRRect(const D2D1_RECT_F& r, float radius, D2D1_COLOR_F c, float width)
{
    SetSolid(c);
    g_rt->DrawRoundedRectangle(RR(r, radius), g_solid, width);
}

static void StrokeRRect(float l, float t, float r, float b, float radius,
    D2D1_COLOR_F c, float width)
{
    StrokeRRect(D2D1::RectF(l, t, r, b), radius, c, width);
}

static void Line(float x1, float y1, float x2, float y2, D2D1_COLOR_F c, float width)
{
    SetSolid(c);
    g_rt->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), g_solid, width);
}

static void Circle(float cx, float cy, float radius, D2D1_COLOR_F c)
{
    SetSolid(c);
    g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), g_solid);
}

static void Ring(float cx, float cy, float radius, D2D1_COLOR_F c, float width)
{
    SetSolid(c);
    g_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), g_solid, width);
}

static void Text(const std::wstring& s, IDWriteTextFormat* font, const D2D1_RECT_F& r,
    D2D1_COLOR_F c, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
    DWRITE_PARAGRAPH_ALIGNMENT par = DWRITE_PARAGRAPH_ALIGNMENT_NEAR)
{
    if (s.empty() || !font) return;
    font->SetTextAlignment(align);
    font->SetParagraphAlignment(par);
    font->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    SetSolid(c);
    g_rt->DrawText(s.c_str(), (UINT32)s.size(), font, r, g_solid,
        D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

static void TextBrush(const std::wstring& s, IDWriteTextFormat* font,
    const D2D1_RECT_F& r, ID2D1Brush* brush,
    DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING)
{
    if (s.empty() || !font || !brush) return;
    font->SetTextAlignment(align);
    font->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    font->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    g_rt->DrawText(s.c_str(), (UINT32)s.size(), font, r, brush,
        D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

static void PushLayer(float opacity)
{
    D2D1_LAYER_PARAMETERS params = D2D1::LayerParameters(
        D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
        D2D1::IdentityMatrix(), opacity);
    g_rt->PushLayer(params, nullptr);
}

static void PopLayer()
{
    g_rt->PopLayer();
}

static void DrawGlow(ID2D1RadialGradientBrush* brush, float cx, float cy,
    float rx, float ry, float opacity)
{
    if (!brush) return;
    brush->SetOpacity(opacity);
    brush->SetTransform(D2D1::Matrix3x2F::Scale(rx, ry) *
        D2D1::Matrix3x2F::Translation(cx, cy));
    g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry), brush);
    brush->SetTransform(D2D1::Matrix3x2F::Identity());
    brush->SetOpacity(1.0f);
}

static void DrawLogo(int index, const D2D1_RECT_F& box)
{
    if (index < 0 || index > 2 || !g_logos[index])
    {
        SetSolid(g_brands[std::clamp(index, 0, 2)].accent);
        g_rt->FillEllipse(D2D1::Ellipse(
            D2D1::Point2F((box.left + box.right) * 0.5f, (box.top + box.bottom) * 0.5f),
            (box.right - box.left) * 0.42f, (box.bottom - box.top) * 0.42f), g_solid);
        return;
    }

    D2D1_SIZE_F size = g_logos[index]->GetSize();
    if (size.width <= 0.0f || size.height <= 0.0f) return;

    float boxW = box.right - box.left;
    float boxH = box.bottom - box.top;
    float scale = std::min(boxW / size.width, boxH / size.height);
    float w = size.width * scale;
    float h = size.height * scale;
    D2D1_RECT_F dest = D2D1::RectF(
        (box.left + box.right - w) * 0.5f,
        (box.top + box.bottom - h) * 0.5f,
        (box.left + box.right + w) * 0.5f,
        (box.top + box.bottom + h) * 0.5f);
    g_rt->DrawBitmap(g_logos[index], dest, 1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

// ============================================================================
//  Icons
// ============================================================================

static void DrawChevron(float cx, float cy, float size, float angle, D2D1_COLOR_F c, float width)
{
    D2D1_MATRIX_3X2_F saved{};
    g_rt->GetTransform(&saved);
    g_rt->SetTransform(D2D1::Matrix3x2F::Translation(cx, cy) *
        D2D1::Matrix3x2F::Rotation(angle) *
        D2D1::Matrix3x2F::Translation(-cx, -cy) * saved);

    Line(cx - size * 0.5f, cy - size * 0.22f, cx, cy + size * 0.30f, c, width);
    Line(cx, cy + size * 0.30f, cx + size * 0.5f, cy - size * 0.22f, c, width);
    g_rt->SetTransform(saved);
}

static void DrawSearchIcon(float cx, float cy, float size, D2D1_COLOR_F c)
{
    Ring(cx, cy, size * 0.46f, c, 1.5f);
    Line(cx + size * 0.32f, cy + size * 0.32f, cx + size * 0.74f, cy + size * 0.74f, c, 1.8f);
}

static void DrawBookIcon(float x, float y, float size, D2D1_COLOR_F c)
{
    D2D1_COLOR_F fill = Alpha(c, 0.20f);
    FillRRect(x, y + 1.0f, x + size * 0.46f, y + size - 1.0f, 1.5f, fill);
    FillRRect(x + size * 0.54f, y + 1.0f, x + size, y + size - 1.0f, 1.5f, fill);
    Line(x + size * 0.50f, y + 1.0f, x + size * 0.50f, y + size - 1.0f, Alpha(c, 0.55f), 1.0f);
    Line(x + size * 0.20f, y + size - 3.0f, x + size * 0.36f, y + size - 3.0f, c, 1.0f);
    Line(x + size * 0.64f, y + size - 3.0f, x + size * 0.80f, y + size - 3.0f, c, 1.0f);
}

static void DrawMiniGpuIcon(float x, float y, float w, float h, D2D1_COLOR_F c)
{
    StrokeRRect(x, y, x + w, y + h, 2.0f, c, 1.0f);
    Circle(x + w * 0.30f, y + h * 0.50f, h * 0.20f, Alpha(c, 0.65f));
    Circle(x + w * 0.70f, y + h * 0.50f, h * 0.20f, Alpha(c, 0.65f));
    Line(x + w * 0.38f, y + h * 0.18f, x + w * 0.62f, y + h * 0.82f, Alpha(c, 0.45f), 0.8f);
}

static void DrawMemoryIcon(float x, float y, float size, D2D1_COLOR_F c)
{
    D2D1_RECT_F chip = D2D1::RectF(x + 1.0f, y + 2.0f, x + size - 1.0f, y + size - 2.0f);
    StrokeRRect(chip, 1.5f, c, 1.0f);
    for (int i = 0; i < 3; ++i)
    {
        float px = x + 3.0f + i * (size - 6.0f) / 2.0f;
        Line(px, y, px, y + 2.0f, c, 1.0f);
        Line(px, y + size - 2.0f, px, y + size, c, 1.0f);
    }
}

static void DrawBoltIcon(float x, float y, float size, D2D1_COLOR_F c)
{
    SetSolid(c);
    ID2D1PathGeometry* path = nullptr;
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(g_d2dFactory->CreatePathGeometry(&path))) return;
    if (FAILED(path->Open(&sink)))
    {
        path->Release();
        return;
    }
    sink->BeginFigure(D2D1::Point2F(x + size * 0.58f, y), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(x + size * 0.18f, y + size * 0.56f));
    sink->AddLine(D2D1::Point2F(x + size * 0.47f, y + size * 0.56f));
    sink->AddLine(D2D1::Point2F(x + size * 0.36f, y + size));
    sink->AddLine(D2D1::Point2F(x + size * 0.84f, y + size * 0.38f));
    sink->AddLine(D2D1::Point2F(x + size * 0.54f, y + size * 0.38f));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    sink->Release();
    g_rt->FillGeometry(path, g_solid);
    path->Release();
}

static void DrawGridIcon(float x, float y, float size, D2D1_COLOR_F c)
{
    float s = size * 0.40f;
    float g = size * 0.18f;
    FillRRect(x, y, x + s, y + s, 1.2f, c);
    FillRRect(x + s + g, y, x + size, y + s, 1.2f, c);
    FillRRect(x, y + s + g, x + s, y + size, 1.2f, c);
    FillRRect(x + s + g, y + s + g, x + size, y + size, 1.2f, c);
}

static void DrawFilterIcon(float cx, float cy, float size, D2D1_COLOR_F c)
{
    Line(cx - size * 0.50f, cy - size * 0.36f, cx + size * 0.50f, cy - size * 0.36f, c, 1.6f);
    Line(cx - size * 0.34f, cy - size * 0.02f, cx + size * 0.34f, cy - size * 0.02f, c, 1.6f);
    Line(cx - size * 0.17f, cy + size * 0.32f, cx + size * 0.17f, cy + size * 0.32f, c, 1.6f);
}

static void DrawCheck(float cx, float cy, float size, D2D1_COLOR_F c)
{
    Line(cx - size * 0.42f, cy, cx - size * 0.08f, cy + size * 0.34f, c, 1.8f);
    Line(cx - size * 0.08f, cy + size * 0.34f, cx + size * 0.46f, cy - size * 0.36f, c, 1.8f);
}

// ============================================================================
//  数据初始化
// ============================================================================

static void InitData()
{
    g_brands[0] = { L"NVIDIA", L"从 RIVA 到 RTX 50", 0,
        Col(96, 239, 142), HomeCardRect(0) };
    g_brands[1] = { L"AMD", L"Radeon 显卡图鉴", 1,
        Col(238, 80, 70), HomeCardRect(1) };
    g_brands[2] = { L"intel", L"Arc 显卡图鉴", 2,
        Col(75, 146, 245), HomeCardRect(2) };

    g_cards.clear();
    g_cards.push_back({ L"GeForce RTX 5090", L"旗舰", L"RTX 50", 24, 384, 500, 0,
        Col(104, 245, 151) });
    g_cards.push_back({ L"GeForce RTX 5080", L"高端", L"RTX 50", 16, 256, 550, 1,
        Col(164, 178, 196) });
    g_cards.push_back({ L"GeForce RTX 5070 Ti", L"中端", L"RTX 50", 12, 192, 220, 2,
        Col(90, 240, 194) });
    g_cards.push_back({ L"GeForce RTX 5070", L"中端", L"RTX 50", 12, 192, 200, 3,
        Col(152, 166, 186) });
    g_cards.push_back({ L"GeForce RTX 5060 Ti", L"主流", L"RTX 50", 8, 128, 130, 4,
        Col(137, 226, 169) });
    g_cards.push_back({ L"GeForce RTX 5060", L"主流", L"RTX 50", 8, 128, 115, 5,
        Col(154, 191, 175) });

    g_sideGroups.clear();
    g_sideGroups.push_back({ L"早期系列（RIVA 及以前）",
        { L"NV1", L"Riva 128", L"RIVA TNT", L"RIVA TNT2" },
        true, 1.0f, 1.0f, 0.0f });
    g_sideGroups.push_back({ L"GeForce 游戏系列",
        { L"GeForce 256", L"GeForce 2", L"GeForce 3", L"GeForce 4",
          L"FX 5", L"GeForce 6", L"GeForce 7", L"GeForce 8", L"GeForce 9",
          L"GTX 400", L"GTX 600", L"GTX 700", L"GTX 900", L"GTX 16",
          L"RTX 20", L"RTX 30", L"RTX 40", L"RTX 50" },
        true, 1.0f, 1.0f, 0.0f });
    g_sideGroups.push_back({ L"专业工作站",
        { L"RTX PRO 6000", L"RTX 5000 Ada", L"RTX 4000 Ada" },
        false, 0.0f, 0.0f, 0.0f });
    g_sideGroups.push_back({ L"数据中心",
        { L"H100 Tensor Core", L"H200 Tensor Core", L"L40S" },
        false, 0.0f, 0.0f, 0.0f });

    g_visibleCount = (int)g_cards.size();
    g_countShown = (float)g_visibleCount;
    g_visibleCount = 0;
    g_emptyT = 0.0f;
    g_emptyVel = 0.0f;
}

// ============================================================================
//  布局 / 筛选
// ============================================================================

static bool CardMatches(const GpuCard& card)
{
    if (!g_seriesFilter.empty() && card.series != g_seriesFilter)
        return false;

    if (g_tagFilter >= 0 && g_tagFilter < 4 && card.tag != g_tagNames[g_tagFilter])
        return false;

    if (!g_searchText.empty())
    {
        if (!ContainsNoCase(card.name, g_searchText) &&
            !ContainsNoCase(card.tag, g_searchText))
            return false;
    }
    return true;
}

static void SnapCardsToLayout()
{
    int slot = 0;
    for (auto& card : g_cards)
    {
        if (card.visible)
        {
            D2D1_RECT_F r = CardSlotRect(slot++);
            card.x = card.tx = r.left;
            card.y = card.ty = r.top;
        }
        card.vx = card.vy = 0.0f;
        card.hover = card.hoverVel = 0.0f;
        card.presence = card.visible ? 1.0f : 0.0f;
        card.presenceVel = 0.0f;
    }
    g_hoverCard = -1;
}

static void ApplyFilters()
{
    int count = 0;
    for (auto& card : g_cards)
    {
        bool visible = CardMatches(card);
        card.visible = visible;
        card.presenceTarget = visible ? 1.0f : 0.0f;
        if (visible) ++count;
    }

    g_visibleCount = count;
    g_emptyTarget = (count == 0) ? 1.0f : 0.0f;
    g_countPulse = 1.0f;

    if (count == 0) g_hoverCard = -1;
}

static void ResetLibraryState()
{
    g_searchText.clear();
    g_searchFocused = false;
    g_searchFocusT = 0.0f;
    g_searchFocusVel = 0.0f;
    g_filterOpen = false;
    g_filterOpenT = 0.0f;
    g_filterOpenVel = 0.0f;
    g_filterHover = -1;
    g_tagFilter = -1;
    g_seriesFilter.clear();
    g_sideScroll = 0.0f;
    g_sideScrollTarget = 0.0f;
    g_sideScrollVel = 0.0f;
    g_sideScrollToBottom = false;
    g_sideHoverRow = -1;
    g_sideHoverT = 0.0f;
    g_selectedCard = 0;
    g_emptyT = 0.0f;
    g_emptyVel = 0.0f;

    ApplyFilters();
    int slot = 0;
    for (auto& card : g_cards)
    {
        D2D1_RECT_F r = CardSlotRect(slot++);
        card.x = card.tx = r.left;
        card.y = card.ty = r.top;
        card.presence = 1.0f;
        card.presenceTarget = 1.0f;
        card.presenceVel = 0.0f;
    }
    g_countShown = (float)g_visibleCount;
    SnapCardsToLayout();
}

// ============================================================================
//  动画控制
// ============================================================================

static void StartAnimation()
{
    if (!g_hwnd) return;
    if (!g_timerRunning)
    {
        g_timerRunning = true;
        g_lastTick = std::chrono::steady_clock::now();
        SetTimer(g_hwnd, 1, 15, nullptr);
    }
}

static void StopAnimation()
{
    if (!g_hwnd) return;
    if (g_timerRunning)
    {
        KillTimer(g_hwnd, 1);
        g_timerRunning = false;
    }
}

static void ShowToast(const std::wstring& message, D2D1_COLOR_F color)
{
    g_toastText = message;
    g_toastColor = color;
    g_toastTime = 0.0f;
    g_toastVisible = true;
    StartAnimation();
}

static void NavigateTo(Page target)
{
    if (g_navActive || target == g_page) return;

    g_navFrom = g_page;
    g_navTo = target;
    g_navT = 0.0f;
    g_navActive = true;
    g_navDirection = (target == Page::Library) ? 1 : -1;
    g_toastVisible = false;

    if (target == Page::Library)
    {
        g_libIntro = 0.0f;
        ResetLibraryState();
    }
    else
    {
        g_homeIntro = 0.0f;
        g_searchFocused = false;
        g_filterOpen = false;
    }

    StartAnimation();
}

static void BuildSideRows(std::vector<SideRow>& rows, float& contentHeight);

static void UpdateHoverTargets()
{
    g_homeHover = -1;
    g_hoverCard = -1;
    g_sideHoverRow = -1;
    g_filterHover = -1;
    g_filterHoverTarget = 0.0f;
    g_searchHoverTarget = 0.0f;
    g_clearHoverTarget = 0.0f;
    g_emptyHoverTarget = 0.0f;
    g_handCursor = false;

    for (auto& card : g_cards)
        card.hoverTarget = 0.0f;

    if (g_navActive || !g_mouseInside)
        return;

    if (g_page == Page::Home)
    {
        for (int i = 0; i < 3; ++i)
        {
            if (InRect(g_brands[i].rect, g_mouse))
            {
                g_homeHover = i;
                g_handCursor = true;
                break;
            }
        }
        return;
    }

    if (g_filterOpen)
    {
        if (InRect(FilterPopupRect(), g_mouse))
        {
            for (int i = 0; i < 5; ++i)
            {
                if (InRect(FilterPopupRowRect(i), g_mouse))
                {
                    g_filterHover = i;
                    break;
                }
            }
        }
        if (InRect(g_filterRect, g_mouse) || InRect(FilterPopupRect(), g_mouse))
        {
            g_filterHoverTarget = 1.0f;
            g_handCursor = true;
        }
        return;
    }

    if (InRect(g_searchRect, g_mouse)) g_searchHoverTarget = 1.0f;
    if (InRect(g_filterRect, g_mouse)) g_filterHoverTarget = 1.0f;
    if (!g_searchText.empty() && InRect(SearchClearRect(), g_mouse))
        g_clearHoverTarget = 1.0f;
    if (g_visibleCount == 0 && InRect(g_emptyResetRect, g_mouse))
        g_emptyHoverTarget = 1.0f;

    if (InRect(D2D1::RectF(PANEL_L, SIDE_TOP, SIDEBAR_R, SIDE_BOTTOM), g_mouse))
    {
        std::vector<SideRow> rows;
        float contentH = 0.0f;
        BuildSideRows(rows, contentH);
        for (int i = 0; i < (int)rows.size(); ++i)
        {
            const auto& row = rows[i];
            if (row.rect.top < SIDE_TOP || row.rect.bottom > SIDE_BOTTOM) continue;
            if (InRect(row.rect, g_mouse)) { g_sideHoverRow = i; break; }
        }
    }

    for (int i = 0; i < (int)g_cards.size(); ++i)
    {
        const auto& card = g_cards[i];
        if (!card.visible || card.presence < 0.4f) continue;
        D2D1_RECT_F r = D2D1::RectF(card.x, card.y, card.x + CARD_W, card.y + CARD_H);
        if (InRect(r, g_mouse)) { g_hoverCard = i; break; }
    }

    for (int i = 0; i < (int)g_cards.size(); ++i)
        g_cards[i].hoverTarget = (i == g_hoverCard) ? 1.0f : 0.0f;

    g_handCursor =
        (g_homeHover >= 0) ||
        InRect(g_backRect, g_mouse) || InRect(g_searchRect, g_mouse) ||
        InRect(g_filterRect, g_mouse) ||
        (!g_searchText.empty() && InRect(SearchClearRect(), g_mouse)) ||
        g_sideHoverRow >= 0 || g_hoverCard >= 0 ||
        (g_visibleCount == 0 && InRect(g_emptyResetRect, g_mouse));
}

static void BuildSideRows(std::vector<SideRow>& rows, float& contentHeight)
{
    rows.clear();
    float y = SIDE_TOP - g_sideScroll;

    for (int gi = 0; gi < (int)g_sideGroups.size(); ++gi)
    {
        auto& group = g_sideGroups[gi];
        rows.push_back({ gi, -1, true, D2D1::RectF(PANEL_L + 16.0f, y, SIDEBAR_R - 14.0f, y + SIDEBAR_GROUP_H) });
        y += SIDEBAR_GROUP_H;

        float itemsH = group.items.size() * SIDEBAR_ITEM_H * group.expand;
        if (itemsH > 0.4f)
        {
            float clipBottom = y + itemsH;
            for (int ii = 0; ii < (int)group.items.size(); ++ii)
            {
                float top = y + ii * SIDEBAR_ITEM_H;
                float bottom = top + SIDEBAR_ITEM_H;
                if (top + 1.0f >= clipBottom) break;
                rows.push_back({ gi, ii, false,
                    D2D1::RectF(PANEL_L + 28.0f, top, SIDEBAR_R - 14.0f, bottom) });
            }
        }
        y += itemsH;
    }

    contentHeight = (y + g_sideScroll) - SIDE_TOP;
}

static void UpdateCardTargets()
{
    int slot = 0;
    for (auto& card : g_cards)
    {
        if (card.visible)
        {
            D2D1_RECT_F r = CardSlotRect(slot++);
            card.tx = r.left;
            card.ty = r.top;
        }
        else
        {
            card.tx = card.x;
            card.ty = card.y;
        }
    }
}

static void UpdateAnimation(float dt)
{
    if (dt > 0.05f) dt = 0.05f;
    if (dt <= 0.0f) return;

    UpdateHoverTargets();

    g_time += dt;
    if (g_homeIntro < 1.0f) g_homeIntro = Clamp01(g_homeIntro + dt / 0.95f);
    if (g_libIntro < 1.0f) g_libIntro = Clamp01(g_libIntro + dt / 1.05f);

    if (g_navActive)
    {
        g_navT += dt / 0.72f;
        if (g_navT >= 1.0f)
        {
            g_navT = 1.0f;
            g_page = g_navTo;
            g_navActive = false;
        }
    }

    // 首页卡片悬停
    for (int i = 0; i < 3; ++i)
    {
        g_homeHoverTarget[i] = (g_homeHover == i) ? 1.0f : 0.0f;
        g_homeHoverV[i] = SmoothDamp(g_homeHoverV[i], g_homeHoverTarget[i],
            g_homeHoverVel[i], 0.09f, dt);
    }

    // 顶栏悬停
    g_backHoverT = SmoothDamp(g_backHoverT,
        (g_page == Page::Library && !g_navActive && InRect(g_backRect, g_mouse)) ? 1.0f : 0.0f,
        g_backHoverVel, 0.08f, dt);
    g_searchHoverT = SmoothDamp(g_searchHoverT, g_searchHoverTarget, g_searchHoverVel, 0.08f, dt);
    g_clearHoverT = SmoothDamp(g_clearHoverT, g_clearHoverTarget, g_clearHoverVel, 0.07f, dt);
    g_filterHoverT = SmoothDamp(g_filterHoverT, g_filterHoverTarget, g_filterHoverVel, 0.08f, dt);
    g_emptyHoverT = SmoothDamp(g_emptyHoverT, g_emptyHoverTarget, g_emptyHoverVel, 0.09f, dt);

    // 搜索框 focus
    g_searchFocusT = SmoothDamp(g_searchFocusT, g_searchFocused ? 1.0f : 0.0f,
        g_searchFocusVel, 0.09f, dt);

    // 筛选面板
    g_filterOpenT = SmoothDamp(g_filterOpenT, g_filterOpen ? 1.0f : 0.0f,
        g_filterOpenVel, 0.10f, dt);

    // 侧栏展开
    for (auto& group : g_sideGroups)
        group.expand = SmoothDamp(group.expand, group.expandTarget,
            group.expandVel, 0.13f, dt);

    float contentH = 0.0f;
    {
        std::vector<SideRow> rows;
        BuildSideRows(rows, contentH);
    }
    float viewportH = SIDE_BOTTOM - SIDE_TOP;
    g_sideScrollMax = std::max(0.0f, contentH - viewportH);
    if (g_sideScrollToBottom)
        g_sideScrollTarget = g_sideScrollMax;
    g_sideScrollTarget = std::max(0.0f, std::min(g_sideScrollTarget, g_sideScrollMax));
    g_sideScroll = SmoothDamp(g_sideScroll, g_sideScrollTarget, g_sideScrollVel, 0.12f, dt);
    g_sideScroll = std::max(0.0f, std::min(g_sideScroll, g_sideScrollMax));

    g_sideHoverT = SmoothDamp(g_sideHoverT, (g_sideHoverRow >= 0) ? 1.0f : 0.0f,
        g_sideHoverVel, 0.08f, dt);

    // 卡片布局
    UpdateCardTargets();
    for (auto& card : g_cards)
    {
        card.hover = SmoothDamp(card.hover, card.hoverTarget, card.hoverVel, 0.10f, dt);
        card.presence = SmoothDamp(card.presence, card.presenceTarget,
            card.presenceVel, 0.14f, dt);
        card.x = SmoothDamp(card.x, card.tx, card.vx, 0.15f, dt);
        card.y = SmoothDamp(card.y, card.ty, card.vy, 0.15f, dt);
    }

    // 计数 / 空状态
    g_countShown = SmoothDamp(g_countShown, (float)g_visibleCount, g_countVel, 0.20f, dt);
    g_countPulse = std::max(0.0f, g_countPulse - dt * 2.6f);
    g_emptyT = SmoothDamp(g_emptyT, g_emptyTarget, g_emptyVel, 0.18f, dt);

    if (g_hoverCard >= 0)
        g_fanAngle += dt * (1.2f + 2.6f * g_cards[g_hoverCard].hover);
    else
        g_fanAngle += dt * 0.18f;

    // Toast
    if (g_toastVisible)
    {
        g_toastTime += dt;
        if (g_toastTime > 2.65f)
        {
            g_toastVisible = false;
            g_toastTime = 0.0f;
        }
    }
}

static bool Nearly(float a, float b, float eps = 0.002f)
{
    return std::fabs(a - b) < eps;
}

static bool NeedsContinuousFrames()
{
    if (g_navActive || g_homeIntro < 0.999f || g_libIntro < 0.999f) return true;
    if (g_searchFocused) return true;
    if (g_toastVisible) return true;
    for (const auto& group : g_sideGroups)
    {
        if (!Nearly(group.expand, group.expandTarget, 0.004f)) return true;
    }
    if (!Nearly(g_filterOpenT, g_filterOpen ? 1.0f : 0.0f, 0.004f)) return true;
    if (!Nearly(g_searchFocusT, g_searchFocused ? 1.0f : 0.0f, 0.004f)) return true;
    if (!Nearly(g_sideScroll, g_sideScrollTarget, 0.05f)) return true;
    if (!Nearly(g_countShown, (float)g_visibleCount, 0.02f)) return true;
    if (!Nearly(g_emptyT, g_emptyTarget, 0.004f)) return true;
    if (g_countPulse > 0.005f) return true;

    for (int i = 0; i < 3; ++i)
    {
        if (!Nearly(g_homeHoverV[i], g_homeHoverTarget[i], 0.004f)) return true;
    }
    for (auto& card : g_cards)
    {
        if (!Nearly(card.hover, card.hoverTarget, 0.004f)) return true;
        if (!Nearly(card.presence, card.presenceTarget, 0.004f)) return true;
        if (!Nearly(card.x, card.tx, 0.05f) || !Nearly(card.y, card.ty, 0.05f)) return true;
        if (card.hover > 0.02f && card.visible) return true;
    }

    if (!Nearly(g_sideHoverT, (g_sideHoverRow >= 0) ? 1.0f : 0.0f, 0.004f)) return true;
    float backTarget = (!g_navActive && g_page == Page::Library && InRect(g_backRect, g_mouse)) ? 1.0f : 0.0f;
    if (!Nearly(g_backHoverT, backTarget, 0.004f)) return true;
    if (!Nearly(g_searchHoverT, g_searchHoverTarget, 0.004f)) return true;
    if (!Nearly(g_clearHoverT, g_clearHoverTarget, 0.004f)) return true;
    if (!Nearly(g_filterHoverT, g_filterHoverTarget, 0.004f)) return true;
    if (!Nearly(g_emptyHoverT, g_emptyHoverTarget, 0.004f)) return true;
    return false;
}

// 前向声明
static void DrawScene();
static void DrawHome();
static void DrawLibrary();

// ============================================================================
//  背景
// ============================================================================

static void DrawBackground()
{
    g_rt->SetTransform(D2D1::Matrix3x2F::Identity());
    g_rt->Clear(Col(7, 9, 18));

    if (g_bgGrad)
    {
        g_rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, g_clientW, g_clientH), g_bgGrad);
    }

    float w = g_clientW;
    float h = g_clientH;
    float t = g_time;

    DrawGlow(g_glowPurple, w * 0.86f + std::sin(t * 0.16f) * 24.0f,
        h * 0.84f, w * 0.42f, h * 0.36f, 0.80f);
    DrawGlow(g_glowBlue, w * 0.08f + std::cos(t * 0.12f) * 22.0f,
        h * 0.20f, w * 0.34f, h * 0.30f, 0.58f);
    DrawGlow(g_glowGreen, w * 0.58f, h * 1.05f,
        w * 0.32f, h * 0.22f, 0.22f);

    // 科技网格
    float spacing = std::max(54.0f, 72.0f * g_viewScale);
    float alpha = 0.055f;
    for (float x = std::fmod(w * 0.5f, spacing); x < w; x += spacing)
        Line(x, 0.0f, x, h, Col(66, 126, 205, (int)(255 * alpha)), 0.65f);
    for (float y = std::fmod(h * 0.5f, spacing); y < h; y += spacing)
        Line(0.0f, y, w, y, Col(66, 126, 205, (int)(255 * alpha)), 0.65f);

    // 斜向光束
    for (int i = 0; i < 4; ++i)
    {
        float off = (float)i * w * 0.27f;
        Line(off - w * 0.35f, h * 0.98f, off + w * 0.18f, -h * 0.05f,
            Col(94, 89, 255, 8 + i * 2), 1.0f);
    }

    // 星点
    for (int i = 0; i < 58; ++i)
    {
        float sx = std::fmod(std::sin(i * 12.9898f) * 43758.5453f, 1.0f);
        float sy = std::fmod(std::sin(i * 78.233f + 4.31f) * 24634.6345f, 1.0f);
        if (sx < 0.0f) sx += 1.0f;
        if (sy < 0.0f) sy += 1.0f;
        float tw = 0.55f + 0.45f * std::sin(t * (0.8f + (i % 5) * 0.12f) + i * 1.7f);
        float a = (i % 7 == 0) ? 0.26f : 0.12f;
        Circle(sx * w, sy * h, (i % 9 == 0) ? 1.35f : 0.8f,
            Col(174, 207, 255, (int)(255 * a * tw)));
    }

    // 霓虹四角
    auto neonCorner = [&](float x, float y, float dirX, float dirY)
        {
            D2D1_COLOR_F c = Col(192, 73, 255, 230);
            Line(x, y, x + dirX * 42.0f, y, c, 2.0f);
            Line(x, y, x, y + dirY * 42.0f, c, 2.0f);
            Line(x, y, x + dirX * 20.0f, y, Alpha(c, 0.24f), 9.0f);
            Line(x, y, x, y + dirY * 20.0f, Alpha(c, 0.24f), 9.0f);
        };
    float m = 17.0f;
    neonCorner(m, m, 1.0f, 1.0f);
    neonCorner(w - m, m, -1.0f, 1.0f);
    neonCorner(m, h - m, 1.0f, -1.0f);
    neonCorner(w - m, h - m, -1.0f, -1.0f);
}

// ============================================================================
//  首页
// ============================================================================

static void DrawHomeCard(int i)
{
    const BrandCard& brand = g_brands[i];
    D2D1_RECT_F base = brand.rect;
    float hover = g_homeHoverV[i];
    float enter = EaseOutCubic(Clamp01((g_homeIntro - i * 0.08f) / 0.62f));
    float alpha = enter;
    if (alpha <= 0.001f) return;

    D2D1_MATRIX_3X2_F saved{};
    g_rt->GetTransform(&saved);

    float cx = (base.left + base.right) * 0.5f;
    float cy = (base.top + base.bottom) * 0.5f;
    float scale = 1.0f + hover * 0.018f;
    D2D1::Matrix3x2F cardT =
        D2D1::Matrix3x2F::Translation(0.0f, (1.0f - enter) * 26.0f) *
        D2D1::Matrix3x2F::Translation(cx, cy) *
        D2D1::Matrix3x2F::Scale(scale, scale) *
        D2D1::Matrix3x2F::Translation(-cx, -cy) *
        D2D1::Matrix3x2F::Translation(0.0f, -hover * 8.0f);
    g_rt->SetTransform(cardT * saved);

    bool useLayer = alpha < 0.999f;
    if (useLayer) PushLayer(alpha);

    // 卡面阴影
    FillRRect(base.left + 2.0f, base.top + 14.0f, base.right - 2.0f, base.bottom + 16.0f,
        20.0f, Col(0, 0, 0, 90));
    FillRRect(base.left + 8.0f, base.top + 24.0f, base.right - 8.0f, base.bottom + 24.0f,
        18.0f, Col(0, 0, 0, 55));

    // 卡面
    FillRRect(base, 20.0f, Col(16, 21, 37, 242));
    FillRRect(base.left + 1.0f, base.top + 1.0f, base.right - 1.0f, base.top + 100.0f,
        19.0f, Col(28, 38, 60, 38));

    // 悬停光晕
    ID2D1RadialGradientBrush* hoverGlow =
        (i == 0) ? g_glowGreen : ((i == 1) ? g_glowRed : g_glowBlue);
    if (hoverGlow && hover > 0.01f)
    {
        hoverGlow->SetOpacity(0.16f * hover);
        hoverGlow->SetTransform(D2D1::Matrix3x2F::Scale(220.0f, 180.0f) *
            D2D1::Matrix3x2F::Translation(cx, base.top + 130.0f));
        g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, base.top + 130.0f), 220.0f, 180.0f),
            hoverGlow);
        hoverGlow->SetTransform(D2D1::Matrix3x2F::Identity());
        hoverGlow->SetOpacity(1.0f);
    }

    // 顶部细线
    Line(base.left + 24.0f, base.top + 1.0f, base.right - 24.0f, base.top + 1.0f,
        Alpha(brand.accent, 0.42f + hover * 0.25f), 1.0f);

    // logo 区域
    D2D1_RECT_F logoBox = D2D1::RectF(cx - 56.0f, base.top + 56.0f,
        cx + 56.0f, base.top + 164.0f);
    FillRRect(logoBox.left - 8.0f, logoBox.top - 8.0f, logoBox.right + 8.0f,
        logoBox.bottom + 8.0f, 24.0f, Col(9, 13, 23, 160));
    DrawLogo(i, logoBox);

    Text(brand.name, g_fonts.brand,
        D2D1::RectF(base.left, base.top + 183.0f, base.right, base.top + 218.0f),
        Col(241, 246, 255),
        DWRITE_TEXT_ALIGNMENT_CENTER);

    Text(brand.caption, g_fonts.brandMeta,
        D2D1::RectF(base.left, base.top + 220.0f, base.right, base.top + 242.0f),
        Col(131, 140, 161),
        DWRITE_TEXT_ALIGNMENT_CENTER);

    // 分隔线
    Line(base.left + 30.0f, base.top + 252.0f, base.right - 30.0f, base.top + 252.0f,
        Col(255, 255, 255, 16), 1.0f);

    // 底部入口
    D2D1_COLOR_F arrowColor = Alpha(brand.accent, 0.70f + hover * 0.30f);
    const wchar_t* actionText = (i == 0)
        ? (hover > 0.35f ? L"进入图鉴  →" : L"浏览系列  →")
        : (hover > 0.35f ? L"即将上线  →" : L"敬请期待");
    Text(actionText, g_fonts.homeFooter,
        D2D1::RectF(base.left, base.top + 260.0f, base.right, base.top + 282.0f),
        arrowColor, DWRITE_TEXT_ALIGNMENT_CENTER);

    // 边框
    StrokeRRect(base, 20.0f, Alpha(brand.accent, 0.28f + hover * 0.42f), 1.0f + hover * 0.5f);
    if (hover > 0.01f)
    {
        StrokeRRect(D2D1::RectF(base.left - 1.0f, base.top - 1.0f,
            base.right + 1.0f, base.bottom + 1.0f), 21.0f,
            Alpha(brand.accent, 0.06f * hover), 6.0f);
        StrokeRRect(D2D1::RectF(base.left - 1.0f, base.top - 1.0f,
            base.right + 1.0f, base.bottom + 1.0f), 21.0f,
            Alpha(brand.accent, 0.12f * hover), 3.0f);
    }

    // 内角标记
    float k = 15.0f;
    D2D1_COLOR_F corner = Alpha(brand.accent, 0.28f + hover * 0.5f);
    Line(base.left + 9.0f, base.top + 9.0f, base.left + 9.0f + k, base.top + 9.0f, corner, 1.4f);
    Line(base.left + 9.0f, base.top + 9.0f, base.left + 9.0f, base.top + 9.0f + k, corner, 1.4f);
    Line(base.right - 9.0f - k, base.bottom - 9.0f, base.right - 9.0f, base.bottom - 9.0f, corner, 1.4f);
    Line(base.right - 9.0f, base.bottom - 9.0f - k, base.right - 9.0f, base.bottom - 9.0f, corner, 1.4f);

    if (useLayer) PopLayer();
    g_rt->SetTransform(saved);
}

static void DrawHome()
{
    // 淡入的标题
    float titleIn = EaseOutCubic(Clamp01(g_homeIntro / 0.55f));

    Text(L"CORE BASE  /  HARDWARE ATLAS", g_fonts.homeKicker,
        D2D1::RectF(0.0f, 104.0f, DESIGN_W, 126.0f),
        Col(98, 216, 230, (int)(185 * titleIn)),
        DWRITE_TEXT_ALIGNMENT_CENTER);

    // 水印
    Text(L"CORE", g_fonts.watermark,
        D2D1::RectF(0.0f, 92.0f, DESIGN_W, 360.0f),
        Col(120, 170, 255, (int)(9 * titleIn)),
        DWRITE_TEXT_ALIGNMENT_CENTER);

    if (g_homeTitleGrad)
    {
        if (titleIn < 0.999f)
        {
            PushLayer(titleIn);
            TextBrush(L"CoreBase", g_fonts.homeTitle,
                D2D1::RectF(0.0f, 144.0f, DESIGN_W, 232.0f), g_homeTitleGrad,
                DWRITE_TEXT_ALIGNMENT_CENTER);
            PopLayer();
        }
        else
        {
            TextBrush(L"CoreBase", g_fonts.homeTitle,
                D2D1::RectF(0.0f, 144.0f, DESIGN_W, 232.0f), g_homeTitleGrad,
                DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }
    else
    {
        Text(L"CoreBase", g_fonts.homeTitle,
            D2D1::RectF(0.0f, 144.0f, DESIGN_W, 232.0f),
            Col(225, 240, 255, (int)(255 * titleIn)),
            DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    Text(L"CPU 与 GPU 参数规格查询", g_fonts.subtitle,
        D2D1::RectF(0.0f, 240.0f, DESIGN_W, 266.0f),
        Col(157, 168, 190, (int)(220 * titleIn)),
        DWRITE_TEXT_ALIGNMENT_CENTER);

    // 标题下的装饰线
    float lineW = 220.0f * titleIn;
    Line(DESIGN_W * 0.5f - lineW * 0.5f, 288.0f,
        DESIGN_W * 0.5f + lineW * 0.5f, 288.0f,
        Col(101, 226, 242, (int)(120 * titleIn)), 1.0f);
    Circle(DESIGN_W * 0.5f, 288.0f, 2.2f, Col(147, 246, 175, (int)(220 * titleIn)));

    Text(L"选择一个品牌，开始探索硬件宇宙", g_fonts.brandMeta,
        D2D1::RectF(0.0f, 304.0f, DESIGN_W, 324.0f),
        Col(108, 118, 142, (int)(190 * titleIn)),
        DWRITE_TEXT_ALIGNMENT_CENTER);

    for (int i = 0; i < 3; ++i)
        DrawHomeCard(i);

    // 底部
    float footerA = titleIn;
    Line(310.0f, 820.0f, 1290.0f, 820.0f, Col(255, 255, 255, (int)(14 * footerA)), 1.0f);
    Text(L"CORE BASE  /  CPU & GPU DATABASE", g_fonts.homeFooter,
        D2D1::RectF(310.0f, 834.0f, 820.0f, 854.0f),
        Col(104, 115, 138, (int)(200 * footerA)));
    Text(L"v1.0.0", g_fonts.homeFooter,
        D2D1::RectF(1110.0f, 834.0f, 1290.0f, 854.0f),
        Col(104, 115, 138, (int)(200 * footerA)),
        DWRITE_TEXT_ALIGNMENT_TRAILING);

    // 上方左右状态
    Circle(72.0f, 60.0f, 3.2f, Col(96, 239, 142, (int)(220 * footerA)));
    Text(L"SYSTEM ONLINE", g_fonts.homeKicker,
        D2D1::RectF(84.0f, 50.0f, 280.0f, 70.0f),
        Col(116, 136, 156, (int)(200 * footerA)));
    Text(L"2026 EDITION", g_fonts.homeKicker,
        D2D1::RectF(DESIGN_W - 260.0f, 50.0f, DESIGN_W - 72.0f, 70.0f),
        Col(116, 136, 156, (int)(200 * footerA)),
        DWRITE_TEXT_ALIGNMENT_TRAILING);
}

// ============================================================================
//  显卡库：面板与侧栏
// ============================================================================

static void DrawPanelChrome()
{
    D2D1_RECT_F panel = D2D1::RectF(PANEL_L, PANEL_T, PANEL_R, PANEL_B);

    // 外阴影
    for (int i = 4; i >= 1; --i)
    {
        float e = i * 8.0f;
        FillRRect(panel.left - e, panel.top - e + 8.0f, panel.right + e, panel.bottom + e + 12.0f,
            PANEL_RADIUS + e * 0.45f, Col(0, 0, 0, 9 + i * 3));
    }

    FillRRect(panel, PANEL_RADIUS, Col(13, 17, 29, 255));

    // 侧栏底色
    FillRRect(PANEL_L + 1.0f, PANEL_T + 1.0f, SIDEBAR_R, PANEL_B - 1.0f,
        PANEL_RADIUS - 1.0f, Col(19, 24, 41, 235));
    FillRect(D2D1::RectF(220.0f, PANEL_T + 1.0f, SIDEBAR_R, PANEL_B - 1.0f),
        Col(19, 24, 41, 235));
    FillRect(D2D1::RectF(PANEL_L + 20.0f, PANEL_T + 1.0f, SIDEBAR_R, PANEL_T + 110.0f),
        Col(28, 37, 59, 24));

    // 分层渐变 / 高光
    FillRect(D2D1::RectF(CONTENT_L - 20.0f, PANEL_T + 1.0f, PANEL_R - 1.0f, PANEL_T + 170.0f),
        Col(25, 33, 53, 16));

    // 侧栏分隔线
    Line(SIDEBAR_R, PANEL_T + 1.0f, SIDEBAR_R, PANEL_B - 1.0f, Col(255, 255, 255, 14), 1.0f);
    Line(SIDEBAR_R + 1.0f, PANEL_T + 92.0f, SIDEBAR_R + 1.0f, PANEL_B - 64.0f,
        Col(94, 222, 213, 16), 1.0f);

    // 底部横线
    Line(CONTENT_L - 18.0f, FOOTER_LINE_Y, PANEL_R - 18.0f, FOOTER_LINE_Y,
        Col(255, 255, 255, 17), 1.0f);

    // 面板边框
    StrokeRRect(panel, PANEL_RADIUS, Col(49, 56, 82, 225), 1.2f);
    Line(PANEL_L + 34.0f, PANEL_T + 1.0f, PANEL_R - 34.0f, PANEL_T + 1.0f,
        Col(255, 255, 255, 17), 1.0f);
}

static void DrawBackButton()
{
    D2D1_COLOR_F c = Col(129, 139, 162);
    if (g_backHoverT > 0.01f)
    {
        FillRRect(g_backRect, 9.0f, Col(106, 235, 149, (int)(18 * g_backHoverT)));
        StrokeRRect(g_backRect, 9.0f, Col(106, 235, 149, (int)(75 * g_backHoverT)), 1.0f);
        c = Col((int)Lerp(129, 132, g_backHoverT),
            (int)Lerp(139, 240, g_backHoverT),
            (int)Lerp(162, 174, g_backHoverT));
    }
    float cx = (g_backRect.left + g_backRect.right) * 0.5f;
    float cy = (g_backRect.top + g_backRect.bottom) * 0.5f;
    Line(cx + 4.0f, cy - 6.0f, cx - 3.0f, cy, c, 1.8f);
    Line(cx - 3.0f, cy, cx + 4.0f, cy + 6.0f, c, 1.8f);
}

static void DrawSidebar()
{
    std::vector<SideRow> rows;
    float contentH = 0.0f;
    BuildSideRows(rows, contentH);

    g_rt->PushAxisAlignedClip(D2D1::RectF(PANEL_L + 1.0f, SIDE_TOP - 8.0f,
        SIDEBAR_R - 2.0f, SIDE_BOTTOM + 1.0f), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    for (int i = 0; i < (int)rows.size(); ++i)
    {
        const SideRow& row = rows[i];
        if (row.rect.bottom < SIDE_TOP - 8.0f || row.rect.top > SIDE_BOTTOM) continue;

        bool hovered = (g_sideHoverRow == i);
        if (row.header)
        {
            auto& group = g_sideGroups[row.group];
            if (hovered)
            {
                FillRRect(D2D1::RectF(row.rect.left - 2.0f, row.rect.top + 1.0f,
                    row.rect.right, row.rect.bottom - 1.0f), 8.0f, Col(255, 255, 255, 10));
            }

            DrawBookIcon(row.rect.left + 8.0f, row.rect.top + 9.0f, 15.0f,
                Col(95, 181, 224, group.expand > 0.2f ? 220 : 145));

            Text(group.title, g_fonts.sideGroup,
                D2D1::RectF(row.rect.left + 33.0f, row.rect.top + 7.0f,
                    row.rect.right - 28.0f, row.rect.bottom - 3.0f),
                Col(208, 217, 235, group.items.empty() ? 130 : 225));

            float chevronAngle = -90.0f * (1.0f - group.expand);
            DrawChevron(row.rect.right - 18.0f, row.rect.top + SIDEBAR_GROUP_H * 0.5f,
                9.0f, chevronAngle, Col(111, 122, 146), 1.4f);
        }
        else
        {
            auto& group = g_sideGroups[row.group];
            const std::wstring& item = group.items[row.item];
            bool selected = (g_seriesFilter == item);

            if (selected)
            {
                FillRRect(D2D1::RectF(row.rect.left - 1.0f, row.rect.top + 1.0f,
                    row.rect.right, row.rect.bottom - 1.0f), 7.0f, Col(96, 239, 142, 16));
                FillRRect(row.rect.left - 1.0f, row.rect.top + 7.0f,
                    row.rect.left + 1.0f, row.rect.bottom - 7.0f, 1.0f,
                    Col(110, 245, 157, 220));
            }
            else if (hovered)
            {
                FillRRect(D2D1::RectF(row.rect.left - 1.0f, row.rect.top + 1.0f,
                    row.rect.right, row.rect.bottom - 1.0f), 7.0f,
                    Col(255, 255, 255, (int)(8 * std::max(0.35f, g_sideHoverT))));
            }

            D2D1_COLOR_F itemColor = selected
                ? Col(124, 243, 165)
                : (hovered ? Col(224, 233, 248) : Col(190, 201, 219));
            D2D1_COLOR_F iconColor = selected
                ? Col(109, 239, 153, 230)
                : Col(110, 143, 167, hovered ? 220 : 160);

            DrawMiniGpuIcon(row.rect.left + 19.0f, row.rect.top + 8.0f, 15.0f, 10.0f, iconColor);
            Text(item, g_fonts.sideItem,
                D2D1::RectF(row.rect.left + 43.0f, row.rect.top + 4.0f,
                    row.rect.right - 28.0f, row.rect.bottom - 3.0f),
                itemColor);
            DrawChevron(row.rect.right - 18.0f, row.rect.top + SIDEBAR_ITEM_H * 0.5f,
                7.0f, 0.0f, Col(101, 112, 135, 180), 1.1f);
        }
    }

    g_rt->PopAxisAlignedClip();

    // 滚动条
    if (g_sideScrollMax > 0.5f)
    {
        float viewportH = SIDE_BOTTOM - SIDE_TOP;
        float thumbH = std::max(34.0f, viewportH * viewportH / (viewportH + g_sideScrollMax));
        float travel = viewportH - thumbH;
        float thumbY = SIDE_TOP + (g_sideScrollMax > 0.0f ? (g_sideScroll / g_sideScrollMax) * travel : 0.0f);

        FillRRect(SIDEBAR_R - 10.0f, SIDE_TOP + 4.0f, SIDEBAR_R - 6.0f,
            SIDE_BOTTOM - 4.0f, 2.0f, Col(255, 255, 255, 12));
        FillRRect(SIDEBAR_R - 10.0f, thumbY, SIDEBAR_R - 6.0f, thumbY + thumbH,
            2.0f, Col(103, 215, 205, 105));
    }
}

static void DrawHeader()
{
    DrawBackButton();

    if (g_titleGrad)
    {
        TextBrush(L"NVIDIA 显卡库", g_fonts.h1,
            D2D1::RectF(CONTENT_L, 86.0f, CONTENT_R - 280.0f, 142.0f), g_titleGrad);
    }
    else
    {
        Text(L"NVIDIA 显卡库", g_fonts.h1,
            D2D1::RectF(CONTENT_L, 86.0f, CONTENT_R - 280.0f, 142.0f),
            Col(135, 247, 169));
    }

    Text(L"探索 NVIDIA 显卡的精彩历程与技术规格", g_fonts.subtitle,
        D2D1::RectF(CONTENT_L + 1.0f, 146.0f, 1000.0f, 168.0f),
        Col(138, 147, 168));

    // ===== 搜索框 =====
    float focus = g_searchFocusT;
    D2D1_COLOR_F boxFill = Col(
        (int)Lerp(14, 18, focus),
        (int)Lerp(18, 25, focus),
        (int)Lerp(31, 40, focus),
        242);
    FillRRect(g_searchRect, 10.0f, boxFill);

    D2D1_COLOR_F border = Col(
        (int)Lerp(48, 92, focus),
        (int)Lerp(54, 202, focus),
        (int)Lerp(79, 137, focus),
        (int)Lerp(210, 235, focus));
    StrokeRRect(g_searchRect, 10.0f, border, 1.0f + focus * 0.35f);
    if (focus > 0.02f || g_searchHoverT > 0.02f)
    {
        StrokeRRect(D2D1::RectF(g_searchRect.left - 1.0f, g_searchRect.top - 1.0f,
            g_searchRect.right + 1.0f, g_searchRect.bottom + 1.0f), 11.0f,
            Col(96, 239, 142, (int)(11 * std::max(focus, g_searchHoverT * 0.4f))), 4.0f);
    }

    DrawSearchIcon(1113.0f, 122.0f, 16.0f, Col(137, 148, 170));
    Line(1126.0f, 135.0f, 1132.0f, 141.0f, Col(137, 148, 170), 1.0f);

    const float textX = 1141.0f;
    const float textW = 192.0f;
    if (g_searchText.empty())
    {
        Text(L"搜索显卡型号...", g_fonts.search,
            D2D1::RectF(textX, 107.0f, textX + textW, 139.0f),
            Col(102, 112, 134), DWRITE_TEXT_ALIGNMENT_LEADING,
            DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    else
    {
        g_rt->PushAxisAlignedClip(D2D1::RectF(textX - 1.0f, 105.0f,
            textX + textW + 1.0f, 140.0f), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        IDWriteTextLayout* layout = nullptr;
        g_fonts.search->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        g_fonts.search->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        if (SUCCEEDED(g_dwrite->CreateTextLayout(g_searchText.c_str(),
            (UINT32)g_searchText.size(), g_fonts.search, textW, 26.0f, &layout)))
        {
            layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            SetSolid(Col(226, 234, 247));
            g_rt->DrawTextLayout(D2D1::Point2F(textX, 109.0f), layout, g_solid,
                D2D1_DRAW_TEXT_OPTIONS_CLIP);

            if (g_searchFocused && std::fmod(g_time, 1.05f) < 0.58f)
            {
                DWRITE_TEXT_METRICS metrics{};
                layout->GetMetrics(&metrics);
                float caretX = textX + std::min(metrics.widthIncludingTrailingWhitespace, textW);
                Line(caretX + 1.0f, 111.0f, caretX + 1.0f, 133.0f,
                    Col(118, 242, 163), 1.4f);
            }
            layout->Release();
        }
        g_rt->PopAxisAlignedClip();
    }

    if (!g_searchText.empty())
    {
        D2D1_RECT_F clear = SearchClearRect();
        if (g_clearHoverT > 0.01f)
            Circle((clear.left + clear.right) * 0.5f, (clear.top + clear.bottom) * 0.5f,
                10.0f, Col(255, 255, 255, (int)(16 * g_clearHoverT)));
        float cx = (clear.left + clear.right) * 0.5f;
        float cy = (clear.top + clear.bottom) * 0.5f;
        D2D1_COLOR_F cc = g_clearHoverT > 0.4f ? Col(222, 231, 246) : Col(113, 124, 146);
        Line(cx - 4.2f, cy - 4.2f, cx + 4.2f, cy + 4.2f, cc, 1.3f);
        Line(cx + 4.2f, cy - 4.2f, cx - 4.2f, cy + 4.2f, cc, 1.3f);
    }

    // ===== 筛选按钮 =====
    D2D1_COLOR_F filterFill = Col(
        (int)Lerp(20, 26, g_filterHoverT + (g_filterOpen ? 0.2f : 0.0f)),
        (int)Lerp(25, 34, g_filterHoverT + (g_filterOpen ? 0.2f : 0.0f)),
        (int)Lerp(41, 52, g_filterHoverT + (g_filterOpen ? 0.2f : 0.0f)),
        238);
    FillRRect(g_filterRect, 10.0f, filterFill);
    StrokeRRect(g_filterRect, 10.0f,
        Col(53, 61, 89, 225), 1.0f);
    DrawFilterIcon(g_filterRect.left + 16.0f, 121.0f, 13.0f, Col(170, 181, 201));
    Text(L"筛选", g_fonts.search,
        D2D1::RectF(g_filterRect.left + 28.0f, 107.0f, g_filterRect.right - 3.0f, 139.0f),
        Col(206, 216, 233), DWRITE_TEXT_ALIGNMENT_CENTER,
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (g_tagFilter >= 0)
    {
        Circle(g_filterRect.right - 8.0f, g_filterRect.top + 8.0f, 2.6f, Col(109, 244, 158));
    }
}

// ============================================================================
//  显卡卡片
// ============================================================================

static void DrawGpuArt(const D2D1_RECT_F& box, const GpuCard& card, float hover, float active)
{
    float w = box.right - box.left;
    float h = box.bottom - box.top;
    float cx = (box.left + box.right) * 0.5f;
    float cy = (box.top + box.bottom) * 0.5f;
    float scale = std::min(w / 198.0f, h / 110.0f);

    D2D1_MATRIX_3X2_F saved{};
    g_rt->GetTransform(&saved);

    D2D1::Matrix3x2F artT =
        D2D1::Matrix3x2F::Scale(scale, scale) *
        D2D1::Matrix3x2F::Skew(-8.0f, 0.0f) *
        D2D1::Matrix3x2F::Rotation(-4.0f) *
        D2D1::Matrix3x2F::Translation(cx, cy);

    g_rt->SetTransform(artT * saved);

    D2D1_COLOR_F accent = card.accent;

    // 地面阴影
    g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(0.0f, 45.0f), 86.0f, 9.0f),
        [&]() { SetSolid(Col(0, 0, 0, 95)); return g_solid; }());

    // 光晕
    FillRRect(D2D1::RectF(-93.0f, -42.0f, 93.0f, 42.0f), 12.0f,
        Alpha(accent, 0.035f + hover * 0.035f));

    // 主卡体
    D2D1_RECT_F body = D2D1::RectF(-84.0f, -35.0f, 84.0f, 35.0f);
    FillRRect(body, 7.0f, Col(24, 28, 37));
    FillRRect(-82.0f, -33.0f, 82.0f, -11.0f, 5.0f, Col(50, 57, 69, 34));
    FillRRect(-82.0f, 20.0f, 82.0f, 33.0f, 4.0f, Col(8, 10, 15, 75));

    // 卡体边缘高光
    Line(-80.0f, -34.0f, 74.0f, -34.0f, Alpha(accent, 0.30f + active * 0.34f), 1.0f);
    Line(-83.0f, -30.0f, -83.0f, 29.0f, Alpha(accent, 0.34f + active * 0.38f), 1.0f);
    Line(82.0f, -29.0f, 82.0f, 29.0f, Alpha(accent, 0.18f + active * 0.22f), 1.0f);

    // 风扇后的 X 形装饰
    Line(-66.0f, -31.0f, 66.0f, 31.0f, Alpha(accent, 0.30f + active * 0.30f), 1.2f);
    Line(-66.0f, 31.0f, 66.0f, -31.0f, Alpha(accent, 0.30f + active * 0.30f), 1.2f);
    Line(-58.0f, -28.0f, 58.0f, 28.0f, Alpha(accent, 0.08f), 4.0f);

    // 双风扇
    for (int f = 0; f < 2; ++f)
    {
        float fx = (f == 0) ? -41.0f : 41.0f;
        float radius = 27.0f;

        Circle(fx, 0.0f, radius + 2.0f, Col(8, 10, 14));
        Ring(fx, 0.0f, radius + 1.0f, Col(74, 82, 96, 175), 1.2f);
        Ring(fx, 0.0f, radius - 2.5f, Alpha(accent, 0.32f + active * 0.40f), 1.0f);
        Circle(fx, 0.0f, radius - 4.0f, Col(27, 31, 39));

        float spin = g_fanAngle * (f == 0 ? 1.0f : -0.82f);
        for (int b = 0; b < 9; ++b)
        {
            float a = spin + b * (6.2831853f / 9.0f);
            float ca = std::cos(a);
            float sa = std::sin(a);
            float r0 = 8.0f;
            float r1 = radius - 6.0f;
            Line(fx + ca * r0, sa * r0, fx + ca * r1, sa * r1,
                Alpha(accent, 0.13f + active * 0.10f), 3.6f);
            Line(fx + ca * r0, sa * r0, fx + ca * (r0 + 8.0f), sa * (r0 + 8.0f),
                Col(116, 126, 144, 90), 1.6f);
        }

        Circle(fx, 0.0f, 7.5f, Col(18, 21, 27));
        Ring(fx, 0.0f, 7.5f, Alpha(accent, 0.55f), 1.0f);
        Circle(fx, 0.0f, 2.3f, Alpha(accent, 0.75f));
    }

    // 侧面散热鳍片
    for (int i = 0; i < 5; ++i)
    {
        float x = -78.0f + i * 4.0f;
        Line(x, -23.0f, x, 23.0f, Col(100, 108, 121, 45), 1.0f);
    }

    // 边角螺丝
    for (int sx = -1; sx <= 1; sx += 2)
    {
        for (int sy = -1; sy <= 1; sy += 2)
        {
            Circle(sx * 73.0f, sy * 25.0f, 2.2f, Col(0, 0, 0, 120));
            Circle(sx * 73.0f, sy * 25.0f, 1.2f, Col(133, 142, 157, 100));
        }
    }

    // 外轮廓
    StrokeRRect(body, 7.0f, Alpha(accent, 0.26f + active * 0.48f), 1.0f);

    g_rt->SetTransform(saved);
}

static void DrawGpuCard(int index)
{
    GpuCard& card = g_cards[index];
    float enter = CardEnter(index);
    float alpha = EaseOutCubic(Clamp01(card.presence)) * enter;
    if (alpha <= 0.004f) return;

    D2D1_RECT_F base = D2D1::RectF(card.x, card.y, card.x + CARD_W, card.y + CARD_H);
    float hover = card.hover;
    bool selected = (index == g_selectedCard);

    D2D1_MATRIX_3X2_F saved{};
    g_rt->GetTransform(&saved);

    float cx = (base.left + base.right) * 0.5f;
    float cy = (base.top + base.bottom) * 0.5f;
    float scale = 1.0f + hover * 0.010f;
    D2D1::Matrix3x2F cardT =
        D2D1::Matrix3x2F::Translation(0.0f, (1.0f - enter) * 24.0f) *
        D2D1::Matrix3x2F::Translation(cx, cy) *
        D2D1::Matrix3x2F::Scale(scale, scale) *
        D2D1::Matrix3x2F::Translation(-cx, -cy) *
        D2D1::Matrix3x2F::Translation(0.0f, -5.0f * hover);
    g_rt->SetTransform(cardT * saved);

    bool useLayer = alpha < 0.999f;
    if (useLayer) PushLayer(alpha);

    D2D1_COLOR_F accent = card.accent;
    float active = std::max(hover, selected ? 1.0f : 0.0f);

    // 阴影
    FillRRect(base.left + 2.0f, base.top + 10.0f, base.right - 2.0f, base.bottom + 12.0f,
        17.0f, Col(0, 0, 0, 80));
    FillRRect(base.left + 8.0f, base.top + 20.0f, base.right - 8.0f, base.bottom + 18.0f,
        15.0f, Col(0, 0, 0, 36));

    // 卡面
    FillRRect(base, 16.0f, Col(16, 20, 33, 242));
    FillRRect(base.left + 1.0f, base.top + 1.0f, base.right - 1.0f, base.top + 90.0f,
        15.0f, Col(37, 45, 66, 25));

    if (selected || hover > 0.01f)
    {
        // 选中 / 悬停时叠一层非常淡的绿色
        FillRRect(base, 16.0f, Alpha(accent, 0.018f * active));
        if (g_glowGreen)
        {
            g_glowGreen->SetOpacity(0.075f * active);
            g_glowGreen->SetTransform(D2D1::Matrix3x2F::Scale(CARD_W * 0.55f, 160.0f) *
                D2D1::Matrix3x2F::Translation(cx, base.top + 94.0f));
            g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, base.top + 94.0f),
                CARD_W * 0.55f, 160.0f), g_glowGreen);
            g_glowGreen->SetTransform(D2D1::Matrix3x2F::Identity());
            g_glowGreen->SetOpacity(1.0f);
        }
    }

    g_rt->PushAxisAlignedClip(D2D1::RectF(base.left + 1.0f, base.top + 1.0f,
        base.right - 1.0f, base.bottom - 1.0f), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // 标题
    Text(card.name, g_fonts.cardTitle,
        D2D1::RectF(base.left + 20.0f, base.top + 16.0f,
            base.right - 16.0f, base.top + 46.0f),
        Col(244, 247, 255));

    // 标签
    std::wstring tag = card.tag;
    float tagW = 46.0f;
    D2D1_RECT_F tagRect = D2D1::RectF(base.left + 21.0f, base.top + 51.0f,
        base.left + 21.0f + tagW, base.top + 73.0f);
    FillRRect(tagRect, 4.5f, Alpha(accent, 0.12f + active * 0.06f));
    StrokeRRect(tagRect, 4.5f, Alpha(accent, 0.26f + active * 0.20f), 0.8f);
    Text(tag, g_fonts.tag,
        D2D1::RectF(tagRect.left + 6.0f, tagRect.top + 3.0f,
            tagRect.right - 4.0f, tagRect.bottom - 2.0f),
        Alpha(accent, 0.95f));

    // 显卡图形
    DrawGpuArt(D2D1::RectF(base.left + 15.0f, base.top + 78.0f,
        base.left + 212.0f, base.top + 195.0f), card, hover, active);

    // 规格
    float sx = base.left + 228.0f;
    float sy = base.top + 78.0f;
    D2D1_COLOR_F labelColor = Col(143, 154, 177);
    D2D1_COLOR_F valueColor = Col(231, 237, 247);

    const wchar_t* labels[3] = { L"显存", L"位宽", L"功耗" };
    std::wstring values[3] = {
        std::to_wstring(card.vram) + L" GB",
        std::to_wstring(card.bus) + L" bit",
        std::to_wstring(card.power) + L" W"
    };

    for (int row = 0; row < 3; ++row)
    {
        const float rowY = sy + row * 38.0f;
        Text(labels[row], g_fonts.specLabel,
            D2D1::RectF(sx + 20.0f, rowY, sx + 94.0f, rowY + 15.0f), labelColor);

        if (row == 0)
            DrawMemoryIcon(sx, rowY + 13.0f, 12.0f, Col(111, 239, 154, 225));
        else if (row == 2)
            DrawBoltIcon(sx + 1.0f, rowY + 13.0f, 11.0f, Col(111, 239, 154, 225));

        Text(values[row], g_fonts.specValue,
            D2D1::RectF(sx + 20.0f, rowY + 9.0f, sx + 94.0f, rowY + 30.0f),
            valueColor);
    }

    // 底部留白，不再叠加会干扰视觉的小字

    g_rt->PopAxisAlignedClip();

    // 边框光晕
    if (active > 0.01f)
    {
        StrokeRRect(D2D1::RectF(base.left - 1.0f, base.top - 1.0f,
            base.right + 1.0f, base.bottom + 1.0f), 17.0f,
            Alpha(accent, 0.066f * active), 7.0f);
        StrokeRRect(D2D1::RectF(base.left - 1.0f, base.top - 1.0f,
            base.right + 1.0f, base.bottom + 1.0f), 17.0f,
            Alpha(accent, 0.13f * active), 3.5f);
    }
    StrokeRRect(base, 16.0f,
        selected ? Alpha(accent, 0.68f) : Alpha(Col(55, 62, 84), 1.0f),
        1.0f + active * 0.55f);

    // 内角
    D2D1_COLOR_F corner = Alpha(accent, 0.16f + active * 0.62f);
    float k = 14.0f;
    Line(base.left + 9.0f, base.top + 9.0f, base.left + 9.0f + k, base.top + 9.0f, corner, 1.3f);
    Line(base.left + 9.0f, base.top + 9.0f, base.left + 9.0f, base.top + 9.0f + k, corner, 1.3f);
    Line(base.right - 9.0f - k, base.bottom - 9.0f, base.right - 9.0f, base.bottom - 9.0f, corner, 1.3f);
    Line(base.right - 9.0f, base.bottom - 9.0f - k, base.right - 9.0f, base.bottom - 9.0f, corner, 1.3f);

    if (useLayer) PopLayer();
    g_rt->SetTransform(saved);
}

static void DrawEmptyState()
{
    if (g_visibleCount != 0 || g_emptyT < 0.01f) return;

    D2D1_MATRIX_3X2_F saved{};
    g_rt->GetTransform(&saved);
    float a = EaseOutCubic(Clamp01(g_emptyT));
    float cy = 374.0f;

    D2D1::Matrix3x2F t = D2D1::Matrix3x2F::Translation(0.0f, (1.0f - a) * 18.0f);
    g_rt->SetTransform(t * saved);
    PushLayer(a);

    float cx = (CONTENT_L + CONTENT_R) * 0.5f;
    Ring(cx, cy - 36.0f, 40.0f, Col(105, 120, 143, 50), 1.0f);
    Ring(cx, cy - 36.0f, 31.0f, Col(105, 120, 143, 26), 1.0f);
    DrawMiniGpuIcon(cx - 24.0f, cy - 51.0f, 48.0f, 28.0f, Col(137, 155, 178, 185));

    Text(L"没有找到匹配的显卡", g_fonts.emptyTitle,
        D2D1::RectF(cx - 300.0f, cy + 8.0f, cx + 300.0f, cy + 38.0f),
        Col(218, 227, 243), DWRITE_TEXT_ALIGNMENT_CENTER);
    Text(L"试一试更换关键词，或者清除当前筛选条件", g_fonts.emptyBody,
        D2D1::RectF(cx - 300.0f, cy + 44.0f, cx + 300.0f, cy + 66.0f),
        Col(128, 140, 160), DWRITE_TEXT_ALIGNMENT_CENTER);

    bool hover = g_emptyHoverT > 0.35f;
    FillRRect(g_emptyResetRect, 9.0f,
        hover ? Col(107, 238, 152, 26) : Col(107, 238, 152, 9));
    StrokeRRect(g_emptyResetRect, 9.0f,
        hover ? Col(107, 238, 152, 150) : Col(107, 238, 152, 60), 1.0f);
    Text(L"清除筛选", g_fonts.emptyBody,
        g_emptyResetRect, Col(137, 244, 173),
        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    PopLayer();
    g_rt->SetTransform(saved);
}

static void DrawCountBadge()
{
    D2D1_RECT_F badge = D2D1::RectF(1312.0f, 700.0f, 1468.0f, 736.0f);
    float pulse = std::sin(Clamp01(g_countPulse) * 3.14159265f);
    float scale = 1.0f + 0.035f * pulse;

    D2D1_MATRIX_3X2_F saved{};
    g_rt->GetTransform(&saved);
    float cx = (badge.left + badge.right) * 0.5f;
    float cy = (badge.top + badge.bottom) * 0.5f;
    g_rt->SetTransform(D2D1::Matrix3x2F::Translation(cx, cy) *
        D2D1::Matrix3x2F::Scale(scale, scale) *
        D2D1::Matrix3x2F::Translation(-cx, -cy) * saved);

    FillRRect(badge, 8.0f, Col(100, 239, 146, 14 + (int)(10 * pulse)));
    StrokeRRect(badge, 8.0f, Col(102, 239, 148, 85), 0.9f);
    DrawGridIcon(1326.0f, 710.0f, 14.0f, Col(113, 243, 158, 225));

    int shown = (int)std::lround(g_countShown);
    std::wstring countText = L"共 " + std::to_wstring(std::max(0, shown)) + L" 款显卡";
    Text(countText, g_fonts.count,
        D2D1::RectF(1348.0f, 708.0f, 1460.0f, 730.0f),
        Col(129, 243, 169), DWRITE_TEXT_ALIGNMENT_LEADING,
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    g_rt->SetTransform(saved);
}

static void DrawFooter()
{
    DrawLogo(0, D2D1::RectF(CONTENT_L, 792.0f, CONTENT_L + 20.0f, 812.0f));

    Text(L"NVIDIA", g_fonts.footerBold,
        D2D1::RectF(CONTENT_L + 26.0f, 788.0f, CONTENT_L + 100.0f, 814.0f),
        Col(105, 239, 150));

    Line(CONTENT_L + 110.0f, 793.0f, CONTENT_L + 110.0f, 811.0f,
        Col(255, 255, 255, 30), 1.0f);

    Text(L"驱动更大的可能", g_fonts.footer,
        D2D1::RectF(CONTENT_L + 126.0f, 791.0f, CONTENT_L + 320.0f, 814.0f),
        Col(150, 160, 178));

    Text(L"v1.0.0", g_fonts.footer,
        D2D1::RectF(1402.0f, 791.0f, 1468.0f, 814.0f),
        Col(112, 123, 142), DWRITE_TEXT_ALIGNMENT_TRAILING);
}

static void DrawFilterPopup()
{
    if (!g_filterOpen && g_filterOpenT < 0.01f) return;

    float a = EaseOutCubic(Clamp01(g_filterOpenT));
    D2D1_RECT_F popup = FilterPopupRect();

    D2D1_MATRIX_3X2_F saved{};
    g_rt->GetTransform(&saved);
    D2D1::Matrix3x2F t = D2D1::Matrix3x2F::Translation(0.0f, (1.0f - a) * -8.0f) *
        D2D1::Matrix3x2F::Translation(popup.right, popup.top) *
        D2D1::Matrix3x2F::Scale(0.97f + 0.03f * a, 0.97f + 0.03f * a) *
        D2D1::Matrix3x2F::Translation(-popup.right, -popup.top);
    g_rt->SetTransform(t * saved);
    PushLayer(a);

    FillRRect(popup.left + 2.0f, popup.top + 8.0f, popup.right + 2.0f, popup.bottom + 10.0f,
        12.0f, Col(0, 0, 0, 100));
    FillRRect(popup, 12.0f, Col(17, 23, 38, 252));
    StrokeRRect(popup, 12.0f, Col(62, 73, 101, 220), 1.0f);

    for (int i = 0; i < 5; ++i)
    {
        D2D1_RECT_F row = FilterPopupRowRect(i);
        bool selected = (i == 0 && g_tagFilter < 0) ||
            (i > 0 && g_tagFilter == i - 1);
        bool hovered = (g_filterHover == i);

        if (selected)
            FillRRect(row, 7.0f, Col(98, 239, 146, 18));
        else if (hovered)
            FillRRect(row, 7.0f, Col(255, 255, 255, 10));

        std::wstring label = (i == 0) ? L"全部" : g_tagNames[i - 1];
        D2D1_COLOR_F color = selected ? Col(126, 244, 166) : Col(194, 205, 222);
        Text(label, g_fonts.sideItem,
            D2D1::RectF(row.left + 13.0f, row.top + 5.0f, row.right - 28.0f, row.bottom - 1.0f),
            color);

        if (selected)
            DrawCheck(row.right - 17.0f, row.top + 15.0f, 10.0f, Col(118, 243, 162));
        else
            Text(L"·", g_fonts.sideItem,
                D2D1::RectF(row.right - 22.0f, row.top + 2.0f, row.right - 8.0f, row.bottom),
                Col(87, 97, 115));
    }

    PopLayer();
    g_rt->SetTransform(saved);
}

static void DrawLibrary()
{
    DrawPanelChrome();
    DrawHeader();
    DrawSidebar();

    for (int i = 0; i < (int)g_cards.size(); ++i)
        DrawGpuCard(i);

    DrawCountBadge();
    DrawEmptyState();
    DrawFooter();
    DrawFilterPopup();
}

// ============================================================================
//  Toast / 页面合成
// ============================================================================

static void DrawToast()
{
    if (!g_toastVisible) return;

    float a = 0.0f;
    if (g_toastTime < 0.22f) a = EaseOutCubic(g_toastTime / 0.22f);
    else if (g_toastTime < 1.95f) a = 1.0f;
    else a = 1.0f - Clamp01((g_toastTime - 1.95f) / 0.38f);
    if (a <= 0.01f) return;

    D2D1_RECT_F box = D2D1::RectF(DESIGN_W * 0.5f - 185.0f, 744.0f,
        DESIGN_W * 0.5f + 185.0f, 786.0f);

    D2D1_MATRIX_3X2_F saved{};
    g_rt->GetTransform(&saved);
    g_rt->SetTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - a) * 12.0f) * saved);
    PushLayer(a);

    FillRRect(box.left + 3.0f, box.top + 8.0f, box.right + 3.0f, box.bottom + 9.0f,
        11.0f, Col(0, 0, 0, 85));
    FillRRect(box, 11.0f, Col(20, 26, 41, 248));
    StrokeRRect(box, 11.0f, Alpha(g_toastColor, 0.50f), 1.0f);
    Circle(box.left + 22.0f, (box.top + box.bottom) * 0.5f, 3.2f, g_toastColor);
    Text(g_toastText, g_fonts.toast,
        D2D1::RectF(box.left + 36.0f, box.top + 9.0f, box.right - 18.0f, box.bottom),
        Col(220, 228, 242), DWRITE_TEXT_ALIGNMENT_LEADING,
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    PopLayer();
    g_rt->SetTransform(saved);
}

static void DrawPageLayer(Page page, float opacity, float dx, float scale)
{
    if (opacity <= 0.003f) return;

    D2D1_MATRIX_3X2_F saved{};
    g_rt->GetTransform(&saved);

    D2D1::Matrix3x2F pageT =
        D2D1::Matrix3x2F::Translation(dx, 0.0f) *
        D2D1::Matrix3x2F::Translation(DESIGN_W * 0.5f, DESIGN_H * 0.5f) *
        D2D1::Matrix3x2F::Scale(scale, scale) *
        D2D1::Matrix3x2F::Translation(-DESIGN_W * 0.5f, -DESIGN_H * 0.5f);
    g_rt->SetTransform(pageT * saved);

    bool useLayer = opacity < 0.999f;
    if (useLayer) PushLayer(opacity);
    if (page == Page::Home) DrawHome();
    else DrawLibrary();
    if (useLayer) PopLayer();

    g_rt->SetTransform(saved);
}

static void DrawScene()
{
    if (!g_rt || !g_solid) return;

    DrawBackground();

    g_rt->SetTransform(g_view);

    if (g_navActive)
    {
        float p = Clamp01(g_navT);

        if (g_navDirection > 0)
        {
            // 首页 → 显卡库：库面板从右侧滑入，首页做轻微视差后退
            float homeE = EaseInOutCubic(p);
            float panelE = EaseOutCubic(p);
            DrawPageLayer(Page::Home, 1.0f, -56.0f * homeE, 1.0f - 0.012f * homeE);
            DrawPageLayer(Page::Library, 1.0f, (DESIGN_W + 90.0f) * (1.0f - panelE), 1.0f);
        }
        else
        {
            // 显卡库 → 首页：首页从左侧归位，库面板向右滑出
            float homeE = EaseOutCubic(p);
            float panelE = EaseInOutCubic(p);
            DrawPageLayer(Page::Home, 1.0f, -56.0f * (1.0f - homeE), 1.0f);
            DrawPageLayer(Page::Library, 1.0f, (DESIGN_W + 90.0f) * panelE, 1.0f);
        }
    }
    else
    {
        DrawPageLayer(g_page, 1.0f, 0.0f, 1.0f);
    }

    g_rt->SetTransform(g_view);
    DrawToast();
    g_rt->SetTransform(D2D1::Matrix3x2F::Identity());
}

// ============================================================================
//  输入与命中测试
// ============================================================================

static bool ToDesignPoint(int px, int py, D2D1_POINT_2F& out)
{
    float dipX = px / g_dpiScale;
    float dipY = py / g_dpiScale;
    if (g_viewScale <= 0.0f) return false;

    out.x = (dipX - g_viewOrigin.x) / g_viewScale;
    out.y = (dipY - g_viewOrigin.y) / g_viewScale;
    return out.x >= 0.0f && out.x <= DESIGN_W && out.y >= 0.0f && out.y <= DESIGN_H;
}

static void UpdateCursor()
{
    bool wantHand = g_handCursor;
    HCURSOR want = wantHand ? g_handCursorHandle : g_arrowCursor;
    if (GetCursor() != want) SetCursor(want);
}

static void ClearSearchFocus()
{
    g_searchFocused = false;
}

static void AppendSearch(wchar_t ch)
{
    if (ch < 32 || ch == 127) return;
    if (!g_searchFocused) g_searchFocused = true;
    if (g_searchText.size() >= 40) return;
    g_searchText.push_back(ch);
    g_filterOpen = false;
    ApplyFilters();
    StartAnimation();
}

static void BackspaceSearch()
{
    if (!g_searchFocused || g_searchText.empty()) return;
    wchar_t last = g_searchText.back();
    g_searchText.pop_back();
    if (last >= 0xDC00 && last <= 0xDFFF && !g_searchText.empty()) // 代理对
    {
        wchar_t prev = g_searchText.back();
        if (prev >= 0xD800 && prev <= 0xDBFF) g_searchText.pop_back();
    }
    ApplyFilters();
    StartAnimation();
}

static void PasteToSearch()
{
    if (!OpenClipboard(g_hwnd)) return;
    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (handle)
    {
        const wchar_t* text = (const wchar_t*)GlobalLock(handle);
        if (text)
        {
            std::wstring value;
            for (const wchar_t* p = text; *p && value.size() < 40; ++p)
            {
                if (*p >= 32 && *p != 127) value.push_back(*p);
            }
            GlobalUnlock(handle);
            if (!value.empty())
            {
                if (!g_searchFocused) g_searchFocused = true;
                g_searchText += value;
                if (g_searchText.size() > 40) g_searchText.resize(40);
                g_filterOpen = false;
                ApplyFilters();
                StartAnimation();
            }
        }
    }
    CloseClipboard();
}

static void HandleSidebarClick(const SideRow& row)
{
    if (row.group < 0 || row.group >= (int)g_sideGroups.size()) return;

    auto& group = g_sideGroups[row.group];
    if (row.header)
    {
        group.expanded = !group.expanded;
        group.expandTarget = group.expanded ? 1.0f : 0.0f;
        // 展开靠底部的分组时，让列表自动滚到新内容
        if (group.expanded && row.group >= 2)
        {
            g_sideScrollToBottom = true;
            g_sideScrollTarget = 100000.0f;
        }
        else if (!group.expanded)
        {
            g_sideScrollToBottom = false;
        }
        StartAnimation();
        return;
    }

    if (row.item < 0 || row.item >= (int)group.items.size()) return;

    const std::wstring& item = group.items[row.item];
    if (g_seriesFilter == item)
        g_seriesFilter.clear();
    else
        g_seriesFilter = item;

    // 系列切换时清掉容易冲突的搜索 / 标签条件
    g_searchText.clear();
    g_searchFocused = false;
    g_tagFilter = -1;
    ApplyFilters();
    StartAnimation();
}

static void HandleLibraryClick(const D2D1_POINT_2F& p)
{
    if (g_filterOpen)
    {
        if (InRect(FilterPopupRect(), p))
        {
            for (int i = 0; i < 5; ++i)
            {
                if (InRect(FilterPopupRowRect(i), p))
                {
                    g_tagFilter = (i == 0) ? -1 : (i - 1);
                    g_filterOpen = false;
                    ApplyFilters();
                    StartAnimation();
                    return;
                }
            }
            return;
        }
        if (InRect(g_filterRect, p))
        {
            g_filterOpen = false;
            StartAnimation();
            return;
        }
        g_filterOpen = false;
        StartAnimation();
        // 关闭筛选面板后，本次点击继续交给下面的按钮 / 卡片处理
    }

    if (InRect(g_backRect, p))
    {
        NavigateTo(Page::Home);
        return;
    }

    if (InRect(g_searchRect, p))
    {
        if (!g_searchText.empty() && InRect(SearchClearRect(), p))
        {
            g_searchText.clear();
            ApplyFilters();
            g_searchFocused = true;
            StartAnimation();
            return;
        }
        g_searchFocused = true;
        g_filterOpen = false;
        StartAnimation();
        return;
    }

    if (!g_searchText.empty() && InRect(SearchClearRect(), p))
    {
        g_searchText.clear();
        ApplyFilters();
        g_searchFocused = true;
        StartAnimation();
        return;
    }

    if (InRect(g_filterRect, p))
    {
        g_filterOpen = !g_filterOpen;
        g_searchFocused = false;
        StartAnimation();
        return;
    }

    if (g_visibleCount == 0 && InRect(g_emptyResetRect, p))
    {
        g_searchText.clear();
        g_seriesFilter.clear();
        g_tagFilter = -1;
        g_searchFocused = false;
        ApplyFilters();
        StartAnimation();
        return;
    }

    ClearSearchFocus();

    if (InRect(D2D1::RectF(PANEL_L, SIDE_TOP, SIDEBAR_R, SIDE_BOTTOM), p))
    {
        std::vector<SideRow> rows;
        float contentH = 0.0f;
        BuildSideRows(rows, contentH);
        for (const auto& row : rows)
        {
            if (row.rect.top < SIDE_TOP || row.rect.bottom > SIDE_BOTTOM) continue;
            if (InRect(row.rect, p))
            {
                HandleSidebarClick(row);
                return;
            }
        }
    }

    for (int i = 0; i < (int)g_cards.size(); ++i)
    {
        auto& card = g_cards[i];
        if (!card.visible || card.presence < 0.4f) continue;
        if (InRect(D2D1::RectF(card.x, card.y, card.x + CARD_W, card.y + CARD_H), p))
        {
            g_selectedCard = i;
            StartAnimation();
            return;
        }
    }
}

static void HandleClick(const D2D1_POINT_2F& p)
{
    if (g_navActive) return;

    if (g_page == Page::Home)
    {
        for (int i = 0; i < 3; ++i)
        {
            if (InRect(g_brands[i].rect, p))
            {
                if (i == 0)
                {
                    NavigateTo(Page::Library);
                }
                else
                {
                    ShowToast(i == 1 ? L"AMD 显卡库正在建设中 · 敬请期待"
                        : L"Intel 显卡库正在建设中 · 敬请期待", g_brands[i].accent);
                }
                StartAnimation();
                return;
            }
        }
        return;
    }

    HandleLibraryClick(p);
}

// ============================================================================
//  窗口过程
// ============================================================================

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_hwnd = hWnd;
        g_instance = (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE);
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2dFactory);
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&g_dwrite));
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&g_wic));

        g_arrowCursor = LoadCursor(nullptr, IDC_ARROW);
        g_handCursorHandle = LoadCursor(nullptr, IDC_HAND);

        UpdateViewMetrics();
        CreateFonts();

        InitData();
        ApplyFilters();
        SnapCardsToLayout();
        g_homeIntro = 0.0f;
        g_libIntro = 0.0f;

        CreateDeviceResources();

        StartAnimation();
        break;
    }

    case WM_GETMINMAXINFO:
    {
        LPMINMAXINFO info = (LPMINMAXINFO)lParam;
        float scale = g_dpi / 96.0f;
        info->ptMinTrackSize.x = (LONG)(1100 * scale);
        info->ptMinTrackSize.y = (LONG)(650 * scale);
        break;
    }

    case WM_SIZE:
    {
        if (wParam == SIZE_MINIMIZED) break;

        UpdateViewMetrics();
        if (g_rt)
        {
            RECT rc{};
            GetClientRect(hWnd, &rc);
            g_rt->SetDpi((float)g_dpi, (float)g_dpi);
            g_rt->Resize(D2D1::SizeU((UINT)std::max<LONG>(1, rc.right),
                (UINT)std::max<LONG>(1, rc.bottom)));
            CreateScreenBackgroundBrush();
        }
        InvalidateRect(hWnd, nullptr, FALSE);
        break;
    }

    case WM_DPICHANGED:
    {
        g_dpi = LOWORD(wParam);
        g_dpiScale = g_dpi / 96.0f;
        RECT* suggested = (RECT*)lParam;
        SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        UpdateViewMetrics();
        if (g_rt)
        {
            g_rt->SetDpi((float)g_dpi, (float)g_dpi);
            CreateScreenBackgroundBrush();
        }
        InvalidateRect(hWnd, nullptr, FALSE);
        break;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT)
        {
            UpdateCursor();
            return TRUE;
        }
        break;

    case WM_MOUSEMOVE:
    {
        g_mousePx.x = GET_X_LPARAM(lParam);
        g_mousePx.y = GET_Y_LPARAM(lParam);
        g_mouseInside = ToDesignPoint(g_mousePx.x, g_mousePx.y, g_mouse);
        if (!g_mouseTracking)
        {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);
            g_mouseTracking = true;
        }

        UpdateHoverTargets();
        UpdateCursor();
        StartAnimation();
        break;
    }

    case WM_MOUSELEAVE:
    {
        g_mouseInside = false;
        g_mouseTracking = false;
        g_mousePx = { -1, -1 };
        g_mouse = { -1000.0f, -1000.0f };
        g_homeHover = -1;
        g_hoverCard = -1;
        g_sideHoverRow = -1;
        g_filterHover = -1;
        g_handCursor = false;
        SetCursor(g_arrowCursor);
        StartAnimation();
        break;
    }

    case WM_LBUTTONDOWN:
    {
        SetFocus(hWnd);
        D2D1_POINT_2F p{};
        int px = GET_X_LPARAM(lParam);
        int py = GET_Y_LPARAM(lParam);
        if (ToDesignPoint(px, py, p))
        {
            HandleClick(p);
        }
        StartAnimation();
        break;
    }

    case WM_LBUTTONUP:
        break;

    case WM_MOUSEWHEEL:
    {
        if (g_page != Page::Library || g_navActive) break;

        POINT screen{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &screen);
        D2D1_POINT_2F p{};
        if (!ToDesignPoint(screen.x, screen.y, p)) break;

        if (g_filterOpen) break;
        if (p.x >= PANEL_L && p.x <= SIDEBAR_R && p.y >= SIDE_TOP && p.y <= SIDE_BOTTOM)
        {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            g_sideScrollToBottom = false;
            g_sideScrollTarget -= (delta / 120.0f) * 54.0f;
            g_sideScrollTarget = std::max(0.0f, std::min(g_sideScrollTarget, g_sideScrollMax));
            StartAnimation();
        }
        break;
    }

    case WM_CHAR:
    {
        if (g_page != Page::Library || g_navActive) break;

        wchar_t ch = (wchar_t)wParam;
        if (ch == VK_BACK || ch == 8)
        {
            if (g_searchFocused) BackspaceSearch();
            break;
        }
        if (ch == VK_RETURN || ch == 13)
        {
            if (g_searchFocused)
                g_searchFocused = false;
            break;
        }
        if (ch == 27) break;

        if (ch >= 32)
        {
            if (!g_searchFocused) g_searchFocused = true;
            AppendSearch(ch);
        }
        break;
    }

    case WM_IME_CHAR:
    {
        if (g_page == Page::Library && !g_navActive)
        {
            if (!g_searchFocused) g_searchFocused = true;
            AppendSearch((wchar_t)wParam);
        }
        break;
    }

    case WM_KEYDOWN:
    {
        if (wParam == VK_ESCAPE)
        {
            if (g_filterOpen)
            {
                g_filterOpen = false;
                StartAnimation();
            }
            else if (g_searchFocused)
            {
                g_searchFocused = false;
                StartAnimation();
            }
            else if (g_page == Page::Library && !g_navActive)
            {
                NavigateTo(Page::Home);
            }
            break;
        }

        if (wParam == 'F' && (GetKeyState(VK_CONTROL) & 0x8000))
        {
            if (g_page == Page::Library && !g_navActive)
            {
                g_searchFocused = true;
                g_filterOpen = false;
                StartAnimation();
            }
            break;
        }

        if (wParam == 'V' && (GetKeyState(VK_CONTROL) & 0x8000))
        {
            if (g_page == Page::Library && g_searchFocused)
                PasteToSearch();
            break;
        }

        if (wParam == VK_LEFT && (GetKeyState(VK_MENU) & 0x8000))
        {
            if (g_page == Page::Library && !g_navActive) NavigateTo(Page::Home);
            break;
        }

        break;
    }

    case WM_SETFOCUS:
        InvalidateRect(hWnd, nullptr, FALSE);
        break;

    case WM_KILLFOCUS:
        g_searchFocused = false;
        g_filterOpen = false;
        StartAnimation();
        break;

    case WM_TIMER:
    {
        if (wParam != 1) break;

        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - g_lastTick).count();
        g_lastTick = now;
        if (dt <= 0.0f) dt = 0.001f;
        if (dt > 0.05f) dt = 0.05f;

        UpdateAnimation(dt);
        InvalidateRect(hWnd, nullptr, FALSE);

        if (!NeedsContinuousFrames())
            StopAnimation();
        break;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        BeginPaint(hWnd, &ps);
        if (!g_rt && g_d2dFactory)
            CreateDeviceResources();

        if (g_rt && g_solid)
        {
            g_rt->BeginDraw();
            DrawScene();
            HRESULT hr = g_rt->EndDraw();
            if (hr == D2DERR_RECREATE_TARGET)
            {
                DiscardDeviceResources();
                CreateDeviceResources();
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        EndPaint(hWnd, &ps);
        break;
    }

    case WM_DESTROY:
    {
        StopAnimation();
        DiscardDeviceResources();
        DestroyFonts();

        if (g_d2dFactory) { g_d2dFactory->Release(); g_d2dFactory = nullptr; }
        if (g_dwrite) { g_dwrite->Release(); g_dwrite = nullptr; }
        if (g_wic) { g_wic->Release(); g_wic = nullptr; }
        CoUninitialize();
        PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

// ============================================================================
//  入口
// ============================================================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const wchar_t CLASS_NAME[] = L"CoreBaseWindow";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_CPUGPU),
        IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_SMALL),
        IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = CLASS_NAME;

    RegisterClassExW(&wc);

    UINT sysDpi = GetDpiForSystem();

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int workW = work.right - work.left;
    int workH = work.bottom - work.top;

    int clientW = MulDiv(1600, sysDpi, 96);
    int clientH = MulDiv(900, sysDpi, 96);

    // 小屏幕也能完整显示，不会让窗口跑到屏幕外
    int maxW = (std::max)(640, workW - MulDiv(80, sysDpi, 96));
    int maxH = (std::max)(480, workH - MulDiv(100, sysDpi, 96));
    if (clientW > maxW)
    {
        clientW = maxW;
        clientH = clientW * 900 / 1600;
    }
    if (clientH > maxH)
    {
        clientH = maxH;
        clientW = clientH * 1600 / 900;
    }

    RECT windowRect{ 0, 0, clientW, clientH };
    AdjustWindowRectExForDpi(&windowRect, WS_OVERLAPPEDWINDOW, FALSE, 0, sysDpi);

    int windowW = windowRect.right - windowRect.left;
    int windowH = windowRect.bottom - windowRect.top;

    int x = work.left + ((work.right - work.left) - windowW) / 2;
    int y = work.top + ((work.bottom - work.top) - windowH) / 2;
    x = std::max((int)work.left, x);
    y = std::max((int)work.top, y);

    HWND hWnd = CreateWindowExW(
        0, CLASS_NAME, L"CoreBase · 硬件图鉴", WS_OVERLAPPEDWINDOW,
        x, y, windowW, windowH,
        nullptr, nullptr, hInstance, nullptr);

    if (!hWnd) return 0;

    BOOL dark = TRUE;
    DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    int corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    COLORREF caption = RGB(10, 13, 24);
    COLORREF text = RGB(226, 233, 247);
    COLORREF border = RGB(48, 54, 78);
    DwmSetWindowAttribute(hWnd, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
    DwmSetWindowAttribute(hWnd, DWMWA_TEXT_COLOR, &text, sizeof(text));
    DwmSetWindowAttribute(hWnd, DWMWA_BORDER_COLOR, &border, sizeof(border));

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}