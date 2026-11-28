// ============================================================================
//  CoreBase · CPU & GPU 图鉴
//  ---------------------------------------------------------------------------
//  Windows 原生硬件目录与官网采集入口。
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
#include "CoreBaseData.h"
#include "CoreBaseSync.h"
#include <shellapi.h>
#include <map>
#include <thread>
#pragma comment(lib, "shell32.lib")

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
static const float SIDEBAR_GROUP_H = 34.0f;

enum class Page
{
    Home,
    Library
};

struct CatalogCard
{
    const corebase::HardwareItem* item;
    std::wstring search;
    std::array<std::pair<std::wstring, std::wstring>, 3> specs;
    float hover = 0, hoverVel = 0;
};

struct SideGroup
{
    std::wstring title;
    std::wstring form;
    std::vector<std::pair<std::wstring, int>> items;
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
    IDWriteTextFormat* detailLabel = nullptr;
    IDWriteTextFormat* detailValue = nullptr;
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
static corebase::HardwareCatalog g_catalog;
static int g_brand = 0;
static bool g_cpu = false;
static std::wstring g_dbError, g_syncStatus, g_seriesForm;
static std::vector<CatalogCard> g_cards;
static std::vector<int> g_slots;
static int g_hoverCard = -1;
static int g_detail = -1;
static float g_detailScroll = 0, g_detailMax = 0;
static float g_detailT = 0, g_detailVel = 0;
static bool g_detailClosing = false;
static bool g_dragGrid = false;
static float g_dragOffset = 0;
static float g_gridScroll = 0, g_gridTarget = 0, g_gridVel = 0, g_gridMax = 0;
static float g_gridReveal = 1.0f, g_gridRevealVel = 0.0f;
static int g_gridRevealDir = 1;   // 1: 向下，-1: 向上
static const D2D1_RECT_F gridView{ CONTENT_L - 8, GRID_TOP - 5, CONTENT_R + 8, FOOTER_LINE_Y - 12 };
static const D2D1_RECT_F leftTab{742,101,839,143}, rightTab{853,101,953,143}, syncButton{970,101,1077,143};
static bool CpuFirst() { return g_brand != 0; }   // Intel / AMD 把 CPU 放在前面
static D2D1_RECT_F GpuTabRect() { return CpuFirst() ? rightTab : leftTab; }
static D2D1_RECT_F CpuTabRect() { return CpuFirst() ? leftTab : rightTab; }
static const D2D1_RECT_F detailPanel{295,72,1305,832}, closeButton{1200,92,1270,130}, sourceButton{1080,770,1270,810};
static bool g_syncBusy = false, g_closing = false;
static corebase::detail::Handle g_cancel;
static std::thread g_worker;
static corebase::SyncResult g_result;
static constexpr UINT SyncComplete = WM_APP + 1;
static D2D1_COLOR_F Accent() { return g_brands[g_brand].accent; }
static float SideItemHeight()
{
    if (!g_cpu) return 27.0f;
    // CPU 系列已经分组精简成单行，所有 CPU 品牌统一用更紧凑的行距
    return 36.0f;
}
static int g_visibleCount = 0;
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
static std::wstring g_seriesFilter;

// 搜索 / 筛选
static std::wstring g_searchText;
static bool g_searchFocused = false;
static float g_caretTime = 0.0f;
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
static int g_formFilter = -1;
static const wchar_t* forms[] = { L"Desktop", L"Mobile", L"Workstation" };
static const wchar_t* formLabels[] = { L"桌面端 Desktop", L"移动端 Mobile", L"工作站 Workstation" };
static const wchar_t* formNames[] = { L"桌面端", L"移动端", L"工作站" };

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
static const D2D1_RECT_F toastBox{350,720,1250,800};

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

static D2D1_RECT_F CardSlotRect(int slot)
{
    int col = slot % 3;
    int row = slot / 3;
    float x = CONTENT_L + col * (CARD_W + CARD_GAP);
    float y = GRID_TOP + row * (CARD_H + CARD_GAP) - g_gridScroll;
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
    return D2D1::RectF(1320.0f, 152.0f, 1468.0f, 152.0f + 4.0f * 32.0f + 14.0f);
}

static D2D1_RECT_F FilterPopupRowRect(int row)
{
    D2D1_RECT_F p = FilterPopupRect();
    return D2D1::RectF(p.left + 7.0f, p.top + 7.0f + row * 32.0f, p.right - 7.0f,
        p.top + 7.0f + row * 32.0f + 30.0f);
}

static float CardEnter(int index)
{
    float delay = (index % 3) * 0.055f;
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
    g_fonts.detailLabel = MakeTextFormat(12.0f, DWRITE_FONT_WEIGHT_NORMAL);
    g_fonts.detailValue = MakeTextFormat(19.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
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
        &g_fonts.emptyBody, &g_fonts.watermark, &g_fonts.detailLabel,
        &g_fonts.detailValue
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
                D2D1::Point2F(400.0f, 150.0f),
                D2D1::Point2F(1200.0f, 220.0f)),
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
    // 简洁的单手柄放大镜，避免双线造成“图标有瑕疵”的观感
    float ox = cx - size * 0.08f;
    float oy = cy - size * 0.08f;
    Ring(ox, oy, size * 0.43f, c, 1.5f);
    Line(ox + size * 0.31f, oy + size * 0.31f,
        ox + size * 0.66f, oy + size * 0.66f, c, 1.6f);
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

static std::wstring Value(const std::wstring& raw, const std::wstring& unit = {})
{
    if (raw.empty()) return L"--";
    return unit.empty() ? raw : raw + L" " + unit;
}

using Specs = decltype(CatalogCard::specs);
static Specs Summary(const corebase::GpuItem& g)
{
    auto third = std::make_pair(L"TDP", Value(g.tdp_w, L"W"));
    if (g.tdp_w.empty() && !g.boost_clock_mhz.empty())
        third = {L"加速频率", Value(g.boost_clock_mhz, L"MHz")};
    else if (g.tdp_w.empty() && !g.base_clock_mhz.empty())
        third = {L"基础频率", Value(g.base_clock_mhz, L"MHz")};
    return {{ {L"显存", Value(g.vram_gb, L"GB") + L" " + Value(g.vram_type)},
        {L"位宽", Value(g.bus_bits, L"bit")}, third }};
}
static Specs Summary(const corebase::CpuItem& c)
{
    return {{ {L"核心 / 线程", Value(c.cores) + L" / " + Value(c.threads)},
        {L"基础 / 加速频率", Value(c.base_clock_ghz) + L" / " + Value(c.boost_clock_ghz) + L" GHz"},
        c.l3_cache_mb.empty() ? std::make_pair(L"TDP", Value(c.tdp_w, L"W"))
                              : std::make_pair(L"L3 缓存", Value(c.l3_cache_mb, L"MB")) }};
}
static std::wstring Series(const corebase::HardwareItem& item)
{
    return item.series.empty() ? L"其他系列" : item.series;
}

static std::wstring ReplaceAll(std::wstring text, const std::wstring& from, const std::wstring& to)
{
    if (from.empty()) return text;
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::wstring::npos)
    {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

// Intel 官方系列名很长，侧栏只显示简洁的中文名称。
static std::wstring SimplifyIntelCpuSeries(const std::wstring& raw)
{
    if (raw.empty()) return L"其他系列";
    const std::wstring lower = Lower(raw);
    auto digitsBefore = [&](size_t end) {
        size_t begin = end;
        while (begin > 0 && std::iswdigit(lower[begin - 1])) --begin;
        return std::make_pair(begin, end);
    };

    // 10th Generation Intel Core i3/i5/i7/i9 -> 10代酷睿系列
    size_t genEnd = 0;
    while (genEnd < lower.size() && std::iswdigit(lower[genEnd])) ++genEnd;
    if (genEnd > 0 && lower.find(L"generation", genEnd) != std::wstring::npos &&
        lower.find(L"core", genEnd) != std::wstring::npos)
        return raw.substr(0, genEnd) + L"代酷睿系列";

    // Intel Core i5 Processors (14th gen) -> 14代酷睿系列
    size_t thGen = lower.find(L"th gen");
    if (thGen != std::wstring::npos)
    {
        auto bounds = digitsBefore(thGen);
        if (bounds.first < bounds.second)
            return raw.substr(bounds.first, bounds.second - bounds.first) + L"代酷睿系列";
    }

    // Intel Core Ultra ... -> 酷睿 Ultra 系列
    if (lower.find(L"core ultra") != std::wstring::npos) return L"酷睿 Ultra 系列";

    if (lower.find(L"core x-series") != std::wstring::npos) return L"酷睿 X 系列";
    if (lower.find(L"core processor n-series") != std::wstring::npos) return L"酷睿 N 系列";
    if (lower.find(L"arc g3") != std::wstring::npos) return L"锐炫系列";

    if (lower.find(L"core processors") != std::wstring::npos) return L"酷睿系列";
    if (lower.find(L"core i") != std::wstring::npos ||
        lower.find(L"core m") != std::wstring::npos) return L"酷睿系列";

    if (lower.find(L"processor n-series") != std::wstring::npos) return L"N 系列";
    if (lower.find(L"processor u-series") != std::wstring::npos) return L"U 系列";
    if (lower.find(L"processor for desktop") != std::wstring::npos) return L"桌面处理器";

    if (lower.find(L"xeon") != std::wstring::npos) return L"至强系列";
    if (lower.find(L"celeron") != std::wstring::npos) return L"赛扬系列";
    if (lower.find(L"pentium") != std::wstring::npos) return L"奔腾系列";
    if (lower.find(L"atom") != std::wstring::npos) return L"凌动系列";

    std::wstring text = raw;
    text = ReplaceAll(text, L"Intel ", L"");
    text = ReplaceAll(text, L" Processors", L"");
    text = ReplaceAll(text, L" processors", L"");
    text = ReplaceAll(text, L" Processor", L"");
    text = ReplaceAll(text, L" processor", L"");
    text = ReplaceAll(text, L"  ", L" ");
    while (!text.empty() && text.front() == L' ') text.erase(text.begin());
    while (!text.empty() && text.back() == L' ') text.pop_back();
    return text.empty() ? L"其他系列" : text;
}

static std::wstring SimplifyAmdCpuSeries(const std::wstring& raw)
{
    if (raw.empty()) return L"其他系列";
    const std::wstring lower = Lower(raw);

    if (lower.find(L"threadripper") != std::wstring::npos) return L"线程撕裂者系列";
    if (lower.find(L"ryzen ai") != std::wstring::npos) return L"锐龙 AI 系列";
    if (lower.find(L"ryzen z") != std::wstring::npos) return L"锐龙 Z 系列";
    if (lower.find(L"ryzen") != std::wstring::npos)
    {
        static const wchar_t* generations[] = {
            L"9000", L"8000", L"7000", L"6000", L"5000", L"4000", L"3000", L"2000", L"1000",
            L"400", L"300", L"200", L"100", L"10"
        };
        for (const wchar_t* gen : generations)
        {
            std::wstring needle = std::wstring(L" ") + gen + L" ";
            if (lower.find(needle) != std::wstring::npos)
                return std::wstring(L"锐龙 ") + gen + L" 系列";
        }
        return L"锐龙系列";
    }

    if (lower.find(L"athlon") != std::wstring::npos) return L"速龙系列";
    if (lower.find(L"sempron") != std::wstring::npos) return L"闪龙系列";
    if (lower.find(L"phenom") != std::wstring::npos) return L"羿龙系列";
    if (lower.find(L"fx") != std::wstring::npos) return L"FX 系列";
    if (lower.find(L"e1-series") != std::wstring::npos ||
        lower.find(L"e2-series") != std::wstring::npos ||
        lower.find(L"e-series") != std::wstring::npos) return L"E 系列 APU";
    if (lower.find(L"a-series") != std::wstring::npos || lower.find(L"apu") != std::wstring::npos)
        return L"A 系列 APU";
    if (lower.find(L"amd 3000") != std::wstring::npos) return L"AMD 3000 系列";
    return raw;
}

static std::wstring SeriesLabel(const corebase::HardwareItem& item)
{
    if (g_cpu && item.brand == L"Intel") return SimplifyIntelCpuSeries(item.series);
    if (g_cpu && item.brand == L"AMD") return SimplifyAmdCpuSeries(item.series);

    std::wstring label = Series(item);
    label = ReplaceAll(label, L"-Series", L" 系列");
    label = ReplaceAll(label, L"-series", L" 系列");
    label = ReplaceAll(label, L" Series", L" 系列");
    label = ReplaceAll(label, L" series", L" 系列");
    label = ReplaceAll(label, L" SERIES", L" 系列");
    return label;
}

static int SeriesSidebarOrder(const std::wstring& form, const std::wstring& series)
{
    int order = 0;
    for (const auto& group : g_sideGroups)
    {
        for (const auto& item : group.items)
        {
            if (group.form == form && item.first == series) return order;
            ++order;
        }
    }
    return -1;
}

static int SidebarSeriesOrder(const std::wstring& label)
{
    size_t i = 0;
    while (i < label.size() && !std::iswdigit(label[i])) ++i;
    if (i == label.size()) return 100000;
    int number = 0;
    while (i < label.size() && std::iswdigit(label[i]) && number < 1000000)
        number = number * 10 + (label[i++] - L'0');
    return number;
}
static bool ReloadCatalog()
{
    bool ok = corebase::LoadCatalogFromDb(g_catalog, g_dbError);
    for (auto& brand : g_brands)
    {
        auto matches = [&](const auto& row) { return row.brand == brand.name; };
        auto gpu = std::count_if(g_catalog.gpus.begin(), g_catalog.gpus.end(), matches);
        auto cpu = std::count_if(g_catalog.cpus.begin(), g_catalog.cpus.end(), matches);
        brand.caption = std::to_wstring(gpu) + L" 款 GPU";
        if (brand.logo != 0) brand.caption += L" · " + std::to_wstring(cpu) + L" 款 CPU";
        if (!ok && g_catalog.path.empty()) brand.caption = L"数据库读取失败";
    }
    return ok;
}
static void InitData()
{
    g_brands[0] = { L"NVIDIA", {}, 0, Col(96,239,142), HomeCardRect(0) };
    g_brands[1] = { L"AMD", {}, 1, Col(250,122,111), HomeCardRect(1) };
    g_brands[2] = { L"Intel", {}, 2, Col(104,183,255), HomeCardRect(2) };
    ReloadCatalog();
}
static void StartAnimation();

static void ApplyFilters(bool animate = true, int direction = 1)
{
    g_slots.clear();
    auto needle = Lower(g_searchText);
    for (int i = 0; i < static_cast<int>(g_cards.size()); ++i)
    {
        auto& card = g_cards[i];
        const auto& item = *card.item;
        card.hover = card.hoverVel = 0;
        if (!g_seriesFilter.empty() && (SeriesLabel(item) != g_seriesFilter || item.form_factor != g_seriesForm)) continue;
        if (g_formFilter >= 0 && item.form_factor != forms[g_formFilter]) continue;
        if (card.search.find(needle) != std::wstring::npos) g_slots.push_back(i);
    }
    g_visibleCount = static_cast<int>(g_slots.size());
    int rows = (g_visibleCount + 2) / 3;
    g_gridMax = std::max(0.0f, GRID_TOP + rows * (CARD_H + CARD_GAP) - CARD_GAP - gridView.bottom);
    g_gridScroll = g_gridTarget = g_gridVel = 0;
    g_emptyTarget = g_slots.empty() ? 1.0f : 0.0f;
    g_hoverCard = g_detail = -1;
    g_detailT = g_detailVel = 0;
    g_detailClosing = false;

    // 切换系列 / 形态 / 品牌时，卡片淡入，并按导航方向轻微滑动。
    if (animate)
    {
        g_gridReveal = 0.0f;
        g_gridRevealVel = 0.0f;
        g_gridRevealDir = direction;
        StartAnimation();
    }
}
static void RebuildLibrary()
{
    g_cards.clear();
    g_sideGroups.clear();
    std::map<std::wstring, std::map<std::wstring,int>> grouped;
    auto add = [&](const auto& items, const auto& fields) {
        for (const auto& item : items)
        {
            if (item.brand != g_brands[g_brand].name) continue;
            std::wstring search;
            auto append = [&](const auto& f) { search += item.*f.member; search += L" "; };
            for (const auto& f : corebase::commonFields) append(f);
            for (const auto& f : fields) append(f);
            g_cards.push_back({&item, Lower(std::move(search)), Summary(item)});
            ++grouped[item.form_factor][SeriesLabel(item)];
        }
    };
    if (g_cpu) add(g_catalog.cpus, corebase::cpuFields);
    else add(g_catalog.gpus, corebase::gpuFields);

    for (int i = 0; i < 3; ++i)
    {
        auto group = grouped.find(forms[i]);
        if (group == grouped.end()) continue;
        SideGroup side;
        side.title = g_cpu ? formNames[i] : formLabels[i];
        side.form = forms[i];
        side.items.assign(group->second.begin(), group->second.end());
        if (g_cpu)
        {
            std::stable_sort(side.items.begin(), side.items.end(),
                [](const auto& a, const auto& b) {
                    int oa = SidebarSeriesOrder(a.first);
                    int ob = SidebarSeriesOrder(b.first);
                    return oa != ob ? oa < ob : a.first < b.first;
                });
        }
        side.expanded = true;
        side.expand = side.expandTarget = 1;
        g_sideGroups.push_back(std::move(side));
    }
    g_syncStatus = L"暂无同步记录";
    for (const auto& log : g_catalog.logs)
    {
        if (log.brand != g_brands[g_brand].name || log.category != (g_cpu ? L"CPU" : L"GPU")) continue;
        auto time = log.finished_at.substr(0,16);
        std::replace(time.begin(), time.end(), L'T', L' ');
        g_syncStatus = L"最近同步  " + time + L" UTC  ·  "
            + (log.status == L"success" ? L"成功" : log.status == L"partial" ? L"部分完成" : L"失败");
    }
    if (g_catalog.immutable) g_syncStatus += L"  ·  只读主库快照";
    if (!g_dbError.empty()) g_syncStatus = L"读取失败  " + g_dbError;
    ApplyFilters();
}
static void ResetLibraryState()
{
    g_searchText.clear();
    g_searchFocused = g_filterOpen = false;
    g_caretTime = 0.0f;
    g_searchFocusT = g_searchFocusVel = g_filterOpenT = g_filterOpenVel = 0;
    g_formFilter = -1;
    g_seriesFilter.clear();
    g_sideScroll = g_sideScrollTarget = g_sideScrollVel = 0;
    RebuildLibrary();
}
static int FirstSlot() { return std::max(0, (static_cast<int>(g_gridScroll / (CARD_H + CARD_GAP)) - 1) * 3); }
static int EndSlot() { return std::min(g_visibleCount, FirstSlot() + 18); }
static int CardAt(D2D1_POINT_2F p)
{
    if (InRect(gridView, p))
        for (int slot = FirstSlot(); slot < EndSlot(); ++slot)
            if (InRect(CardSlotRect(slot), p)) return g_slots[slot];
    return -1;
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

    if (g_detail >= 0)
    {
        if (!g_detailClosing)
            g_handCursor = InRect(closeButton, g_mouse) ||
                (g_cards[g_detail].item->source_url.rfind(L"https://",0) == 0 && InRect(sourceButton, g_mouse));
        return;
    }
    if (g_filterOpen)
    {
        if (InRect(FilterPopupRect(), g_mouse))
        {
            for (int i = 0; i < 4; ++i)
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
    if (!g_cards.empty() && g_visibleCount == 0 && InRect(g_emptyResetRect, g_mouse))
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

    g_hoverCard = CardAt(g_mouse);
    g_handCursor =
        InRect(GpuTabRect(),g_mouse) || (g_brand != 0 && InRect(CpuTabRect(),g_mouse)) || (!g_syncBusy && InRect(syncButton,g_mouse)) ||
        InRect(g_backRect, g_mouse) || InRect(g_searchRect, g_mouse) ||
        InRect(g_filterRect, g_mouse) ||
        (!g_searchText.empty() && InRect(SearchClearRect(), g_mouse)) ||
        g_sideHoverRow >= 0 || g_hoverCard >= 0 ||
        (!g_cards.empty() && g_visibleCount == 0 && InRect(g_emptyResetRect, g_mouse));
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

        float itemsH = static_cast<float>(group.items.size()) * SideItemHeight() * group.expand;
        if (itemsH > 0.4f)
        {
            float clipBottom = y + itemsH;
            for (int ii = 0; ii < (int)group.items.size(); ++ii)
            {
                float top = y + ii * SideItemHeight();
                float bottom = top + SideItemHeight();
                if (top + 1.0f >= clipBottom) break;

                // 展开/收起动画进行到一半时，后面的分组标题会紧跟在
                // itemsH 后面。这里把当前行也裁到 itemsH，避免行内
                // 图标/文字/计数压到下一个分组标题上。
                float visibleBottom = std::min(bottom, clipBottom);
                rows.push_back({ gi, ii, false,
                    D2D1::RectF(PANEL_L + 28.0f, top, SIDEBAR_R - 14.0f, visibleBottom) });
            }
        }
        y += itemsH;
    }

    contentHeight = (y + g_sideScroll) - SIDE_TOP;
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
    if (g_searchFocused)
        g_caretTime += dt;
    else
        g_caretTime = 0.0f;

    // 筛选面板
    g_filterOpenT = SmoothDamp(g_filterOpenT, g_filterOpen ? 1.0f : 0.0f,
        g_filterOpenVel, 0.10f, dt);

    // 侧栏展开
    for (auto& group : g_sideGroups)
        group.expand = SmoothDamp(group.expand, group.expandTarget,
            group.expandVel, 0.13f, dt);

    // 详情面板的开合过渡
    if (g_detail >= 0)
    {
        g_detailT = SmoothDamp(g_detailT, g_detailClosing ? 0.0f : 1.0f,
            g_detailVel, 0.13f, dt);
        if (g_detailClosing && g_detailT < 0.015f)
        {
            g_detail = -1;
            g_detailClosing = false;
            g_detailT = g_detailVel = 0.0f;
        }
    }

    float contentH = 0.0f;
    {
        std::vector<SideRow> rows;
        BuildSideRows(rows, contentH);
    }
    float viewportH = SIDE_BOTTOM - SIDE_TOP;
    g_sideScrollMax = std::max(0.0f, contentH - viewportH);
    g_sideScrollTarget = std::max(0.0f, std::min(g_sideScrollTarget, g_sideScrollMax));
    g_sideScroll = SmoothDamp(g_sideScroll, g_sideScrollTarget, g_sideScrollVel, 0.12f, dt);
    g_sideScroll = std::max(0.0f, std::min(g_sideScroll, g_sideScrollMax));

    g_sideHoverT = SmoothDamp(g_sideHoverT, (g_sideHoverRow >= 0) ? 1.0f : 0.0f,
        g_sideHoverVel, 0.08f, dt);

    g_gridScroll = SmoothDamp(g_gridScroll, g_gridTarget, g_gridVel, 0.10f, dt);
    if (std::abs(g_gridScroll - g_gridTarget) < 0.05f) { g_gridScroll = g_gridTarget; g_gridVel = 0; }
    g_gridReveal = SmoothDamp(g_gridReveal, 1.0f, g_gridRevealVel, 0.16f, dt);
    if (g_gridReveal > 0.999f) g_gridReveal = 1.0f;
    g_hoverCard = (g_page == Page::Library && !g_navActive && !g_filterOpen && g_detail < 0) ? CardAt(g_mouse) : -1;
    for (int slot = FirstSlot(); slot < EndSlot(); ++slot)
    {
        auto index = g_slots[slot];
        auto& card = g_cards[index];
        card.hover = SmoothDamp(card.hover, index == g_hoverCard ? 1.0f : 0.0f, card.hoverVel, 0.09f, dt);
    }

    // 计数 / 空状态
    g_emptyT = SmoothDamp(g_emptyT, g_emptyTarget, g_emptyVel, 0.18f, dt);

    if (g_hoverCard >= 0)
        g_fanAngle += dt * (1.2f + 2.6f * g_cards[g_hoverCard].hover);
    else
        g_fanAngle += dt * 0.18f;

    // Toast
    if (g_toastVisible)
    {
        g_toastTime += dt;
        if (g_toastTime > 7.0f)
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
    if (g_detail >= 0 && !Nearly(g_detailT, g_detailClosing ? 0.0f : 1.0f, 0.004f)) return true;
    if (!Nearly(g_sideScroll, g_sideScrollTarget, 0.05f)) return true;
    if (!Nearly(g_emptyT, g_emptyTarget, 0.004f)) return true;

    for (int i = 0; i < 3; ++i)
    {
        if (!Nearly(g_homeHoverV[i], g_homeHoverTarget[i], 0.004f)) return true;
    }
    if (!Nearly(g_gridScroll, g_gridTarget, 0.05f) || g_hoverCard >= 0) return true;
    if (!Nearly(g_gridReveal, 1.0f, 0.004f)) return true;
    for (int slot = FirstSlot(); slot < EndSlot(); ++slot)
        if (g_cards[g_slots[slot]].hover > 0.004f) return true;

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
    const wchar_t* actionText = L"浏览图鉴  →";
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
            TextBrush(L"Welcome to use CoreBase", g_fonts.homeTitle,
                D2D1::RectF(0.0f, 144.0f, DESIGN_W, 232.0f), g_homeTitleGrad,
                DWRITE_TEXT_ALIGNMENT_CENTER);
            PopLayer();
        }
        else
        {
            TextBrush(L"Welcome to use CoreBase", g_fonts.homeTitle,
                D2D1::RectF(0.0f, 144.0f, DESIGN_W, 232.0f), g_homeTitleGrad,
                DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }
    else
    {
        Text(L"Welcome to use CoreBase", g_fonts.homeTitle,
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

static void Wrapped(const std::wstring& s, IDWriteTextFormat* font, D2D1_RECT_F rect, D2D1_COLOR_F color)
{
    font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    font->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    font->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    SetSolid(color);
    g_rt->DrawText(s.data(), static_cast<UINT32>(s.size()), font, rect, g_solid, D2D1_DRAW_TEXT_OPTIONS_CLIP);
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
            const auto& item = group.items[row.item].first;
            bool selected = (g_seriesFilter == item && g_seriesForm == group.form);

            // 分组展开到一半时，行高可能小于 SideItemHeight()。
            // 只有这种“半行”状态需要额外裁剪，防止图标和文字越界。
            bool partialRow = (row.rect.bottom - row.rect.top) < SideItemHeight() - 0.5f;
            if (partialRow)
                g_rt->PushAxisAlignedClip(row.rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

            if (selected)
            {
                FillRRect(D2D1::RectF(row.rect.left - 1.0f, row.rect.top + 1.0f,
                    row.rect.right, row.rect.bottom - 1.0f), 7.0f, Alpha(Accent(),0.08f));
                FillRRect(row.rect.left - 1.0f, row.rect.top + 7.0f,
                    row.rect.left + 1.0f, row.rect.bottom - 7.0f, 1.0f,
                    Accent());
            }
            else if (hovered)
            {
                FillRRect(D2D1::RectF(row.rect.left - 1.0f, row.rect.top + 1.0f,
                    row.rect.right, row.rect.bottom - 1.0f), 7.0f,
                    Col(255, 255, 255, (int)(8 * std::max(0.35f, g_sideHoverT))));
            }

            D2D1_COLOR_F itemColor = selected
                ? Accent()
                : (hovered ? Col(224, 233, 248) : Col(190, 201, 219));
            D2D1_COLOR_F iconColor = selected
                ? Accent()
                : Col(110, 143, 167, hovered ? 220 : 160);

            DrawMiniGpuIcon(row.rect.left + 19.0f, row.rect.top + 8.0f, 15.0f, 10.0f, iconColor);
            Wrapped(item, g_fonts.sideItem,
                D2D1::RectF(row.rect.left + 43.0f, row.rect.top + 4.0f,
                    row.rect.right - 41.0f, row.rect.bottom - 3.0f),
                itemColor);
            Text(std::to_wstring(group.items[row.item].second), g_fonts.tag,
                {row.rect.right - 39, row.rect.top + 4, row.rect.right - 4, row.rect.bottom}, Accent(), DWRITE_TEXT_ALIGNMENT_TRAILING);

            if (partialRow)
                g_rt->PopAxisAlignedClip();
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

static void Button(D2D1_RECT_F rect, const std::wstring& label, bool active = false, bool disabled = false)
{
    auto ink = disabled ? Col(107,116,136) : active ? Accent() : Col(211,222,237);
    FillRRect(rect, 9, active ? Alpha(Accent(),0.13f) : Col(26,32,48));
    StrokeRRect(rect, 9, active ? Alpha(Accent(),0.5f) : Col(51,61,80), 1);
    Text(label, g_fonts.search, rect, ink, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}
static void DrawHeader()
{
    DrawBackButton();
    auto& brand = g_brands[g_brand];
    Text(brand.name + (g_cpu ? L" 处理器库" : L" 显卡库"), g_fonts.h1,
        {CONTENT_L,86,742,145}, Accent());
    Text(brand.name + (g_cpu ? L"  /  CPU" : L"  /  GPU") + L"     " + g_syncStatus,
        g_fonts.subtitle, {CONTENT_L,151,CONTENT_R,180}, Col(159,170,191));
    Button(GpuTabRect(), L"显卡 GPU", !g_cpu);
    Button(CpuTabRect(), L"处理器 CPU", g_cpu, g_brand == 0);
    Button(syncButton, g_syncBusy ? L"正在同步…" : L"同步数据", false, g_syncBusy);

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

    DrawSearchIcon(1114.0f, 121.5f, 16.0f, Col(137, 148, 170));

    const float textX = 1141.0f;
    const float textW = 192.0f;
    if (g_searchText.empty())
    {
        if (!g_searchFocused)
        {
            Text(L"搜索型号、系列或规格…", g_fonts.search,
                D2D1::RectF(textX, 107.0f, textX + textW, 139.0f),
                Col(102, 112, 134), DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        else if (std::fmod(g_caretTime, 1.05f) < 0.58f)
        {
            Line(textX + 1.0f, 111.0f, textX + 1.0f, 133.0f, Accent(), 1.4f);
        }
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

            if (g_searchFocused && std::fmod(g_caretTime, 1.05f) < 0.58f)
            {
                DWRITE_TEXT_METRICS metrics{};
                layout->GetMetrics(&metrics);
                float caretX = textX + std::min(metrics.widthIncludingTrailingWhitespace, textW);
                Line(caretX + 1.0f, 111.0f, caretX + 1.0f, 133.0f,
                    Accent(), 1.4f);
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

    if (g_formFilter >= 0)
    {
        Circle(g_filterRect.right - 8.0f, g_filterRect.top + 8.0f, 2.6f, Accent());
    }
}

// ============================================================================
//  显卡卡片
// ============================================================================

static void DrawGpuArt(const D2D1_RECT_F& box, D2D1_COLOR_F accent, float hover, float active)
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

static void DrawCpuArt(float x, float y, D2D1_COLOR_F color)
{
    for (int i = -3; i <= 3; ++i)
    {
        float offset = i * 7.0f;
        Line(x+offset,y-32,x+offset,y-40,color,2);
        Line(x+offset,y+32,x+offset,y+40,color,2);
        Line(x-32,y+offset,x-40,y+offset,color,2);
        Line(x+32,y+offset,x+40,y+offset,color,2);
    }
    FillRRect(x-32,y-32,x+32,y+32,6,Col(32,43,57));
    StrokeRRect(x-32,y-32,x+32,y+32,6,Alpha(color,0.7f),1.5f);
    FillRRect(x-22,y-22,x+22,y+22,4,Alpha(color,0.16f));
    Text(L"CPU",g_fonts.footerBold,{x-22,y-20,x+22,y+20},color,
        DWRITE_TEXT_ALIGNMENT_CENTER,DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

static void DrawCard(int slot)
{
    const auto& card = g_cards[g_slots[slot]];
    auto box = CardSlotRect(slot);
    auto accent = Accent();

    // 切换系列/形态时，新结果按行列轻微错峰淡入并上浮。
    float delay = std::min(0.28f, (slot % 6) * 0.035f + (slot / 6) * 0.015f);
    float local = Clamp01((g_gridReveal - delay) / std::max(0.01f, 1.0f - delay));
    float enter = CardEnter(slot) * EaseOutCubic(local);
    if (enter <= 0.001f) return;

    float x = box.left, y = box.top;
    D2D1_MATRIX_3X2_F saved{};
    if (enter < 0.999f)
    {
        g_rt->GetTransform(&saved);
        float cx = (box.left + box.right) * 0.5f;
        float cy = (box.top + box.bottom) * 0.5f;
        float scale = 0.97f + 0.03f * enter;
        float slideY = -static_cast<float>(g_gridRevealDir) * (1.0f - enter) * 20.0f;
        D2D1::Matrix3x2F cardT =
            D2D1::Matrix3x2F::Translation(0.0f, slideY) *
            D2D1::Matrix3x2F::Translation(cx, cy) *
            D2D1::Matrix3x2F::Scale(scale, scale) *
            D2D1::Matrix3x2F::Translation(-cx, -cy);
        g_rt->SetTransform(cardT * saved);
    }

    if (enter < 1) PushLayer(enter);
    FillRRect(box, 14, Col(22,28,43));
    StrokeRRect(box, 14, card.hover > 0.01f ? Alpha(accent,0.25f + 0.65f * card.hover) : Col(51,61,81), 1.2f);
    Text(card.item->form_factor, g_fonts.tag, {x+18,y+12,box.right-18,y+32}, accent);
    Wrapped(card.item->model, g_fonts.specValue, {x+18,y+38,box.right-16,y+90}, Col(241,246,254));

    if (g_cpu) DrawCpuArt(x+68,y+154,accent);
    else DrawGpuArt({x+8,y+106,x+132,y+198},accent,card.hover,card.hover);
    for (int i = 0; i < 3; ++i)
    {
        float top = y + 88 + 44.0f * i;
        Text(card.specs[i].first, g_fonts.specLabel, {x+142,top,box.right-7,top+18},Col(156,168,191));
        Text(card.specs[i].second, g_fonts.specValue, {x+142,top+15,box.right-6,top+42},Col(231,239,250));
    }
    if (enter < 1) PopLayer();
    if (enter < 0.999f) g_rt->SetTransform(saved);
}

struct DetailTile
{
    std::wstring label, value;
};
static std::vector<DetailTile> g_detailTiles;
static constexpr float detailGridTop = 178.0f;
static constexpr float detailGridBottom = 742.0f;
static constexpr float detailGridLeft = 325.0f;
static constexpr float detailGridRight = 1275.0f;
static constexpr float detailTileGap = 14.0f;
static constexpr float detailTileH = 98.0f;

// 详情页先展示最关键的规格，再展示其余数据。
static int DetailRank(const std::wstring& label)
{
    static const wchar_t* cpuOrder[] = {
        L"核心数", L"线程数", L"基础频率", L"加速频率", L"L3 缓存", L"最大睿频功耗",
        L"TDP", L"插槽", L"P 核", L"E 核", L"L2 缓存", L"内存支持", L"PCIe 版本",
        L"集成显卡", L"系列", L"形态", L"架构", L"发布年份"
    };
    static const wchar_t* gpuOrder[] = {
        L"显存容量", L"显存类型", L"显存位宽", L"显存带宽", L"TDP", L"加速频率",
        L"基础频率", L"推荐电源", L"计算单元", L"计算单元类型", L"PCIe 接口",
        L"系列", L"形态", L"架构", L"发布年份"
    };
    const wchar_t* const* order = g_cpu ? cpuOrder : gpuOrder;
    int count = g_cpu ? static_cast<int>(ARRAYSIZE(cpuOrder)) : static_cast<int>(ARRAYSIZE(gpuOrder));
    for (int i = 0; i < count; ++i)
        if (label == order[i]) return i;
    return 100;
}

static void OpenDetail(int index)
{
    if (index < 0 || index >= static_cast<int>(g_cards.size())) return;

    g_detail = index;
    g_detailClosing = false;
    g_detailT = 0.0f;
    g_detailVel = 0.0f;
    g_detailScroll = 0.0f;
    g_detailTiles.clear();

    auto& item = *g_cards[index].item;
    auto append = [&](const auto& row, const auto& field) {
        std::wstring label = field.label;
        if (label == L"型号" || label == L"品牌" || label == L"来源网址" ||
            label == L"产品 ID" || label == L"首次入库" || label == L"更新于")
            return;

        std::wstring value = Value(row.*field.member, field.unit);
        if (label == L"系列")
            value = SeriesLabel(item);

        // 空白的次要参数直接隐藏，详情页更清爽；主要参数保留 -- 占位。
        if (value == L"--" && DetailRank(label) >= 6) return;
        g_detailTiles.push_back({ std::move(label), std::move(value) });
    };
    if (g_cpu)
        for (auto& f : corebase::cpuFields) append(static_cast<const corebase::CpuItem&>(item), f);
    else
        for (auto& f : corebase::gpuFields) append(static_cast<const corebase::GpuItem&>(item), f);
    for (auto& f : corebase::commonFields) append(item, f);

    std::stable_sort(g_detailTiles.begin(), g_detailTiles.end(),
        [](const DetailTile& a, const DetailTile& b) {
            return DetailRank(a.label) < DetailRank(b.label);
        });

    int rows = (static_cast<int>(g_detailTiles.size()) + 2) / 3;
    float contentH = rows > 0 ? rows * detailTileH + (rows - 1) * detailTileGap : 0.0f;
    g_detailMax = std::max(0.0f, contentH - (detailGridBottom - detailGridTop));
    g_detailScroll = 0.0f;
    StartAnimation();
}

static void CloseDetail()
{
    if (g_detail < 0 || g_detailClosing) return;
    g_detailClosing = true;
    StartAnimation();
}
static bool HasSource()
{
    return g_detail >= 0 && g_cards[g_detail].item->source_url.rfind(L"https://",0) == 0;
}
static float ThumbHeight(float height, float maxOffset)
{
    return std::max(28.0f,height*height/(height+maxOffset));
}
static float GridThumbTop()
{
    float height = gridView.bottom-gridView.top;
    return gridView.top + (height-ThumbHeight(height,g_gridMax))*g_gridScroll/g_gridMax;
}
static void DragGrid(float y)
{
    float height = gridView.bottom-gridView.top;
    float travel = height-ThumbHeight(height,g_gridMax);
    g_gridScroll = g_gridTarget = g_gridMax * Clamp01((y-gridView.top-g_dragOffset)/travel);
    g_gridVel = 0;
}
static void Scrollbar(float x, float top, float bottom, float offset, float maxOffset)
{
    if (maxOffset <= 0) return;
    float h = bottom-top;
    float thumb = ThumbHeight(h,maxOffset);
    float y = top + (h-thumb)*offset/maxOffset;
    FillRRect(x,top,x+4,bottom,2,Col(49,57,73));
    FillRRect(x,y,x+4,y+thumb,2,Alpha(Accent(),0.7f));
}
static void DrawDetail()
{
    if (g_detail < 0) return;

    float raw = Clamp01(g_detailT);
    float a = raw * raw * (3.0f - 2.0f * raw);
    if (a <= 0.002f) return;

    // 背景遮罩淡入
    FillRect({ 0,0,DESIGN_W,DESIGN_H }, Col(2, 5, 11, static_cast<int>(183 * a)));

    D2D1_MATRIX_3X2_F saved{};
    g_rt->GetTransform(&saved);
    float cx = (detailPanel.left + detailPanel.right) * 0.5f;
    float cy = (detailPanel.top + detailPanel.bottom) * 0.5f;
    float scale = 0.965f + 0.035f * a;
    D2D1::Matrix3x2F panelT =
        D2D1::Matrix3x2F::Translation(0.0f, (1.0f - a) * 18.0f) *
        D2D1::Matrix3x2F::Translation(cx, cy) *
        D2D1::Matrix3x2F::Scale(scale, scale) *
        D2D1::Matrix3x2F::Translation(-cx, -cy);
    g_rt->SetTransform(panelT * saved);

    bool useLayer = a < 0.999f;
    if (useLayer) PushLayer(a);

    // 面板阴影
    FillRRect(detailPanel.left + 4, detailPanel.top + 10, detailPanel.right + 4,
        detailPanel.bottom + 12, 20, Col(0, 0, 0, 115));
    FillRRect(detailPanel, 18, Col(21, 28, 43, 250));
    StrokeRRect(detailPanel, 18, Alpha(Accent(), 0.5f), 1);

    auto& item = *g_cards[g_detail].item;
    Text(g_brands[g_brand].name + (g_cpu ? L"  /  CPU 参数" : L"  /  GPU 参数"),
        g_fonts.tag, { 325,90,900,112 }, Accent());
    Text(item.form_factor, g_fonts.tag, { 920,90,1175,112 }, Col(142, 156, 178),
        DWRITE_TEXT_ALIGNMENT_TRAILING);
    Wrapped(item.model, g_fonts.cardTitle, { 325,112,1170,166 }, Col(239, 244, 252));
    Button(closeButton, L"关闭 ×");

    // 标题下分隔线
    Line(325, 168, 1275, 168, Col(255, 255, 255, 18), 1);

    // 参数网格
    g_rt->PushAxisAlignedClip({ detailGridLeft,detailGridTop,detailGridRight,detailGridBottom },
        D2D1_ANTIALIAS_MODE_ALIASED);
    float tileW = (detailGridRight - detailGridLeft - 2.0f * detailTileGap) / 3.0f;
    for (int i = 0; i < static_cast<int>(g_detailTiles.size()); ++i)
    {
        int col = i % 3;
        int row = i / 3;
        float x = detailGridLeft + col * (tileW + detailTileGap);
        float y = detailGridTop + row * (detailTileH + detailTileGap) - g_detailScroll;
        if (y + detailTileH < detailGridTop || y > detailGridBottom) continue;

        bool primary = i < 6;
        D2D1_RECT_F tile = { x,y,x + tileW,y + detailTileH };
        FillRRect(tile, 10, primary ? Col(26, 35, 51, 242) : Col(20, 26, 40, 225));
        StrokeRRect(tile, 10, primary ? Alpha(Accent(), 0.34f) : Col(48, 57, 76, 170), 1.0f);
        if (primary)
            FillRRect(x + 1, y + 14, x + 3, y + detailTileH - 14, 1, Alpha(Accent(), 0.8f));

        Text(g_detailTiles[i].label, g_fonts.detailLabel,
            { x + 16,y + 12,x + tileW - 14,y + 34 },
            primary ? Accent() : Col(137, 150, 173));
        Wrapped(g_detailTiles[i].value, g_fonts.detailValue,
            { x + 16,y + 34,x + tileW - 14,y + detailTileH - 10 }, Col(236, 242, 252));
    }
    g_rt->PopAxisAlignedClip();

    Scrollbar(1282, detailGridTop, detailGridBottom, g_detailScroll, g_detailMax);
    if (HasSource()) Button(sourceButton, L"查看官网来源  ↗", true);
    Text(L"滚轮翻阅参数  ·  ESC 关闭", g_fonts.footer,
        { 325,781,760,810 }, Col(148, 160, 180));

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

    Text((g_cards.empty() ? L"此类别暂无本地数据" : L"没有找到匹配的硬件"), g_fonts.emptyTitle,
        D2D1::RectF(cx - 300.0f, cy + 8.0f, cx + 300.0f, cy + 38.0f),
        Col(218, 227, 243), DWRITE_TEXT_ALIGNMENT_CENTER);
    Text((g_cards.empty() ? L"点击顶栏的“同步数据”获取官网规格" : L"试一试更换关键词，或者清除当前筛选条件"), g_fonts.emptyBody,
        D2D1::RectF(cx - 300.0f, cy + 44.0f, cx + 300.0f, cy + 66.0f),
        Col(128, 140, 160), DWRITE_TEXT_ALIGNMENT_CENTER);

    if (g_cards.empty())
    {
        PopLayer();
        g_rt->SetTransform(saved);
        return;
    }
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

static void DrawFooter()
{
    const auto& brand = g_brands[g_brand];
    DrawLogo(g_brand, {CONTENT_L,789,CONTENT_L+26,815});
    Text(brand.name, g_fonts.footerBold, {CONTENT_L+36,791,CONTENT_L+104,819},Accent());
    Text(g_cpu ? L"处理器技术规格" : L"显卡技术规格", g_fonts.footer,
        {CONTENT_L+120,791,CONTENT_L+325,819},Col(154,167,187));
    Text(std::to_wstring(g_visibleCount) + (g_cpu ? L" 款 CPU" : L" 款 GPU"),g_fonts.count,
        {1210,791,CONTENT_R,819},Accent(),DWRITE_TEXT_ALIGNMENT_TRAILING);
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

    for (int i = 0; i < 4; ++i)
    {
        D2D1_RECT_F row = FilterPopupRowRect(i);
        bool selected = (i == 0 && g_formFilter < 0) ||
            (i > 0 && g_formFilter == i - 1);
        bool hovered = (g_filterHover == i);

        if (selected)
            FillRRect(row, 7.0f, Alpha(Accent(),0.08f));
        else if (hovered)
            FillRRect(row, 7.0f, Col(255, 255, 255, 10));

        std::wstring label = (i == 0) ? L"全部" : forms[i - 1];
        D2D1_COLOR_F color = selected ? Accent() : Col(194, 205, 222);
        Text(label, g_fonts.sideItem,
            D2D1::RectF(row.left + 13.0f, row.top + 5.0f, row.right - 28.0f, row.bottom - 1.0f),
            color);

        if (selected)
            DrawCheck(row.right - 17.0f, row.top + 15.0f, 10.0f, Accent());
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

    g_rt->PushAxisAlignedClip(gridView,D2D1_ANTIALIAS_MODE_ALIASED);
    for (int slot = FirstSlot(); slot < EndSlot(); ++slot) DrawCard(slot);
    g_rt->PopAxisAlignedClip();
    Scrollbar(CONTENT_R+13,gridView.top,gridView.bottom,g_gridScroll,g_gridMax);
    DrawEmptyState();
    DrawFooter();
    DrawFilterPopup();
    if (g_detail >= 0) DrawDetail();
}

// ============================================================================
//  Toast / 页面合成
// ============================================================================

static void DrawToast()
{
    if (!g_toastVisible) return;

    float a = 0.0f;
    if (g_toastTime < 0.22f) a = EaseOutCubic(g_toastTime / 0.22f);
    else if (g_toastTime < 6.6f) a = 1.0f;
    else a = 1.0f - Clamp01((g_toastTime - 6.6f) / 0.38f);
    if (a <= 0.01f) return;

    auto box = toastBox;

    D2D1_MATRIX_3X2_F saved{};
    g_rt->GetTransform(&saved);
    g_rt->SetTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - a) * 12.0f) * saved);
    PushLayer(a);

    FillRRect(box.left + 3.0f, box.top + 8.0f, box.right + 3.0f, box.bottom + 9.0f,
        11.0f, Col(0, 0, 0, 85));
    FillRRect(box, 11.0f, Col(20, 26, 41, 248));
    StrokeRRect(box, 11.0f, Alpha(g_toastColor, 0.50f), 1.0f);
    Circle(box.left + 22.0f, (box.top + box.bottom) * 0.5f, 3.2f, g_toastColor);
    bool longMessage = g_toastText.size() > 92;
    auto preview = longMessage ? g_toastText.substr(0,92) + L"…" : g_toastText;
    if (longMessage) Text(L"点击查看完整信息",g_fonts.tag,
        {box.left+36,box.bottom-24,box.right-20,box.bottom},g_toastColor);
    Wrapped(preview, g_fonts.toast,
        D2D1::RectF(box.left + 36.0f, box.top + 9.0f, box.right - 18.0f, box.bottom - 5),
        Col(220, 228, 242));

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

static void FocusSearch()
{
    g_searchFocused = true;
    g_caretTime = 0.0f;       // 点击后立刻显示光标，不再等随机闪烁相位
}

static void AppendSearch(wchar_t ch)
{
    if (ch < 32 || ch == 127) return;
    if (!g_searchFocused) FocusSearch();
    if (g_searchText.size() >= 40) return;
    g_searchText.push_back(ch);
    g_caretTime = 0.0f;
    g_filterOpen = false;
    ApplyFilters(false);
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
    ApplyFilters(false);
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
                if (!g_searchFocused) FocusSearch();
                g_searchText += value;
                if (g_searchText.size() > 40) g_searchText.resize(40);
                g_caretTime = 0.0f;
                g_filterOpen = false;
                ApplyFilters(false);
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
        StartAnimation();
        return;
    }

    if (row.item < 0 || row.item >= (int)group.items.size()) return;

    const auto& item = group.items[row.item].first;
    bool selected = g_seriesFilter == item && g_seriesForm == group.form;
    std::wstring nextFilter = selected ? L"" : item;
    std::wstring nextForm = selected ? L"" : group.form;

    // 系列在列表里往下切时，新内容向下滑入；往上切时反向滑入。
    int oldOrder = g_seriesFilter.empty() ? -1 : SeriesSidebarOrder(g_seriesForm, g_seriesFilter);
    int newOrder = nextFilter.empty() ? -1 : SeriesSidebarOrder(nextForm, nextFilter);
    int direction = (oldOrder >= 0 && newOrder >= 0 && newOrder < oldOrder) ? -1 : 1;

    g_seriesFilter = std::move(nextFilter);
    g_seriesForm = std::move(nextForm);
    g_searchFocused = false;
    ApplyFilters(true, direction);
    StartAnimation();
}

static void StartSync()
{
    if (g_syncBusy) return;
    if (g_catalog.path.empty()) { ShowToast(L"无法定位数据库：" + g_dbError, Accent()); return; }
    g_searchFocused = false;
    g_cancel.reset(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    if (!g_cancel) { ShowToast(L"无法创建后台同步任务",Accent()); return; }
    auto root = g_catalog.path.parent_path();
    auto brand = Lower(g_brands[g_brand].name);
    std::wstring category = g_cpu ? L"cpu" : L"gpu";
    try
    {
        g_worker = std::thread([root, brand, category, window=g_hwnd, cancel=g_cancel.get()] {
            g_result = corebase::RunSync(root,brand,category,cancel);
            g_result.message = brand + L" " + category + L" · " + g_result.message;
            PostMessageW(window,SyncComplete,0,0);
        });
        g_syncBusy = true;
    }
    catch (const std::system_error& e) { ShowToast(corebase::FromUtf8(e.what()),Accent()); }
}
static void HandleLibraryClick(const D2D1_POINT_2F& p)
{
    if (g_detail >= 0)
    {
        if (g_detailClosing) return;
        if (!InRect(detailPanel,p) || InRect(closeButton,p)) { CloseDetail(); return; }
        else if (InRect(sourceButton,p) && HasSource())
        {
            auto result = ShellExecuteW(g_hwnd,L"open",g_cards[g_detail].item->source_url.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(result) <= 32) ShowToast(L"无法打开官网链接",Accent());
        }
        return;
    }
    if (g_filterOpen)
    {
        if (InRect(FilterPopupRect(), p))
        {
            for (int i = 0; i < 4; ++i)
            {
                if (InRect(FilterPopupRowRect(i), p))
                {
                    g_formFilter = (i == 0) ? -1 : (i - 1);
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

    if (InRect(syncButton,p)) { StartSync(); return; }
    if (InRect(GpuTabRect(),p) || InRect(CpuTabRect(),p))
    {
        bool cpu = InRect(CpuTabRect(),p);
        if (cpu && g_brand == 0) { ShowToast(L"NVIDIA 暂无 CPU 采集来源",Accent()); return; }
        if (cpu != g_cpu) { g_cpu = cpu; ResetLibraryState(); }
        return;
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
            FocusSearch();
            StartAnimation();
            return;
        }
        FocusSearch();
        g_filterOpen = false;
        StartAnimation();
        return;
    }

    if (!g_searchText.empty() && InRect(SearchClearRect(), p))
    {
        g_searchText.clear();
        ApplyFilters();
        FocusSearch();
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

    if (!g_cards.empty() && g_visibleCount == 0 && InRect(g_emptyResetRect, p))
    {
        g_searchText.clear();
        g_seriesFilter.clear();
        g_formFilter = -1;
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

    if (g_gridMax > 0 && InRect({CONTENT_R+5,gridView.top,PANEL_R-5,gridView.bottom},p))
    {
        float top = GridThumbTop(), height = ThumbHeight(gridView.bottom-gridView.top,g_gridMax);
        g_dragOffset = (p.y >= top && p.y <= top+height) ? p.y-top : height/2;
        g_dragGrid = true;
        SetCapture(g_hwnd);
        DragGrid(p.y);
        return;
    }
    int index = CardAt(p);
    if (index >= 0) OpenDetail(index);
}

static void HandleClick(const D2D1_POINT_2F& p)
{
    if (g_navActive) return;
    if (g_toastVisible && g_toastText.size() > 92 && InRect(toastBox,p))
    {
        auto message = g_toastText;
        MessageBoxW(g_hwnd,message.c_str(),L"CoreBase · 同步信息",MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (g_page == Page::Home)
    {
        for (int i = 0; i < 3; ++i)
        {
            if (InRect(g_brands[i].rect, p))
            {
                g_brand = i;
                g_cpu = (i != 0);   // Intel / AMD 默认先看处理器
                NavigateTo(Page::Library);
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
        if (!g_dbError.empty()) ShowToast(L"无法读取硬件数据库：" + g_dbError, Col(255,151,118));

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
        if (g_dragGrid) DragGrid(g_mouse.y);
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
        if (g_dragGrid) ReleaseCapture();
        break;
    case WM_CAPTURECHANGED:
        g_dragGrid = false;
        break;

    case WM_MOUSEWHEEL:
    {
        if (g_page != Page::Library || g_navActive) break;

        POINT screen{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &screen);
        D2D1_POINT_2F p{};
        if (!ToDesignPoint(screen.x, screen.y, p)) break;

        float wheel = GET_WHEEL_DELTA_WPARAM(wParam) / 120.0f;
        if (g_detail >= 0)
        {
            g_detailScroll = std::clamp(g_detailScroll-wheel*86,0.0f,g_detailMax);
            StartAnimation();
            break;
        }
        if (g_filterOpen) break;
        if (InRect(gridView,p))
        {
            g_gridTarget = std::clamp(g_gridTarget-wheel*135,0.0f,g_gridMax);
            StartAnimation();
        }
        if (p.x >= PANEL_L && p.x <= SIDEBAR_R && p.y >= SIDE_TOP && p.y <= SIDE_BOTTOM)
        {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            g_sideScrollTarget -= (delta / 120.0f) * 54.0f;
            g_sideScrollTarget = std::max(0.0f, std::min(g_sideScrollTarget, g_sideScrollMax));
            StartAnimation();
        }
        break;
    }

    case WM_CHAR:
    {
        if (g_page != Page::Library || g_navActive) break;

        if (g_detail >= 0) break;
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
            if (!g_searchFocused) FocusSearch();
            AppendSearch(ch);
        }
        break;
    }

    case WM_IME_CHAR:
    {
        if (g_detail >= 0) break;
        if (g_page == Page::Library && !g_navActive)
        {
            if (!g_searchFocused) FocusSearch();
            AppendSearch((wchar_t)wParam);
        }
        break;
    }

    case WM_KEYDOWN:
    {
        if (g_detail >= 0)
        {
            if (wParam == VK_ESCAPE) CloseDetail();
            if (wParam == VK_NEXT) g_detailScroll = std::min(g_detailMax,g_detailScroll + 510);
            if (wParam == VK_PRIOR) g_detailScroll = std::max(0.0f,g_detailScroll - 510);
            StartAnimation();
            break;
        }
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
                FocusSearch();
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

    case SyncComplete:
    {
        g_worker.join();
        g_cancel.reset();
        g_syncBusy = false;
        if (g_closing) { DestroyWindow(hWnd); return 0; }
        bool loaded = ReloadCatalog();
        RebuildLibrary();
        auto message = g_result.message;
        if (!loaded) message += L" · 刷新数据库失败：" + g_dbError;
        ShowToast(message,loaded && g_result.ok ? Accent() : Col(255,151,118));
        break;
    }
    case WM_CLOSE:
        if (g_syncBusy)
        {
            g_closing = true;
            SetEvent(g_cancel.get());
            ShowWindow(hWnd,SW_HIDE);
        }
        else DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
    {
        if (g_worker.joinable())
        {
            SetEvent(g_cancel.get());
            g_worker.join();
            g_cancel.reset();
        }
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
