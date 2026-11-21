#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

ID2D1Factory* pFactory = nullptr;
ID2D1HwndRenderTarget* pRenderTarget = nullptr;
IDWriteFactory* pDWriteFactory = nullptr;

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &pFactory);
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&pDWriteFactory));

        RECT rc;
        GetClientRect(hWnd, &rc);
        pFactory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(hWnd, D2D1::SizeU(rc.right, rc.bottom)),
            &pRenderTarget
        );

        // 深色标题栏
        BOOL darkMode = TRUE;
        DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
        break;
    }

    case WM_GETMINMAXINFO:
    {
        LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
        lpMMI->ptMinTrackSize.x = 640;
        lpMMI->ptMinTrackSize.y = 420;
        break;
    }

    case WM_SIZE:
    {
        if (pRenderTarget)
        {
            pRenderTarget->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        if (!pRenderTarget || !pDWriteFactory) break;

        pRenderTarget->BeginDraw();
        pRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::Black));

        RECT rc;
        GetClientRect(hWnd, &rc);
        float w = (float)rc.right;
        float h = (float)rc.bottom;

        // 1. 蓝紫线性渐变背景
        ID2D1GradientStopCollection* pGradStops = nullptr;
        ID2D1LinearGradientBrush* pGradBrush = nullptr;
        D2D1_GRADIENT_STOP stops[] = {
            {0.0f, D2D1::ColorF(0.02f, 0.04f, 0.12f)},
            {0.5f, D2D1::ColorF(0.08f, 0.06f, 0.28f)},
            {1.0f, D2D1::ColorF(0.25f, 0.05f, 0.38f)}
        };
        pRenderTarget->CreateGradientStopCollection(stops, ARRAYSIZE(stops), D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &pGradStops);
        pRenderTarget->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(0,0), D2D1::Point2F(w, h)),
            D2D1::BrushProperties(), pGradStops, &pGradBrush);
        pRenderTarget->FillRectangle(D2D1::RectF(0,0,w,h), pGradBrush);

        // 2. 网格线
        ID2D1SolidColorBrush* pGridBrush = nullptr;
        pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f,0.6f,1.0f,0.18f), &pGridBrush);
        for(float x = 60; x < w; x += 80)
            pRenderTarget->DrawLine(D2D1::Point2F(x, 40), D2D1::Point2F(x, h-40), pGridBrush, 0.8f);
        for(float y = 60; y < h; y += 80)
            pRenderTarget->DrawLine(D2D1::Point2F(40, y), D2D1::Point2F(w-40, y), pGridBrush, 0.8f);

        // 3. 霓虹四角装饰
        ID2D1SolidColorBrush* pBorderBrush = nullptr;
        pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.7f,0.2f,0.95f,0.85f), &pBorderBrush);
        float margin = 20.0f, cornerLen = 28.0f;

        // 左上
        pRenderTarget->DrawLine(D2D1::Point2F(margin,margin), D2D1::Point2F(margin+cornerLen,margin), pBorderBrush, 2.0f);
        pRenderTarget->DrawLine(D2D1::Point2F(margin,margin), D2D1::Point2F(margin,margin+cornerLen), pBorderBrush, 2.0f);

        // 右上
        pRenderTarget->DrawLine(D2D1::Point2F(w-margin-cornerLen,margin), D2D1::Point2F(w-margin,margin), pBorderBrush, 2.0f);
        pRenderTarget->DrawLine(D2D1::Point2F(w-margin,margin), D2D1::Point2F(w-margin,margin+cornerLen), pBorderBrush, 2.0f);

        // 左下
        pRenderTarget->DrawLine(D2D1::Point2F(margin, h-margin-cornerLen), D2D1::Point2F(margin, h-margin), pBorderBrush, 2.0f);
        pRenderTarget->DrawLine(D2D1::Point2F(margin, h-margin), D2D1::Point2F(margin+cornerLen, h-margin), pBorderBrush, 2.0f);

        // 右下
        pRenderTarget->DrawLine(D2D1::Point2F(w-margin-cornerLen, h-margin), D2D1::Point2F(w-margin, h-margin), pBorderBrush, 2.0f);
        pRenderTarget->DrawLine(D2D1::Point2F(w-margin, h-margin-cornerLen), D2D1::Point2F(w-margin, h-margin), pBorderBrush, 2.0f);

        pRenderTarget->EndDraw();

        // 释放资源
        pGradBrush->Release();
        pGradStops->Release();
        pGridBrush->Release();
        pBorderBrush->Release();

        EndPaint(hWnd, &ps);
        break;
    }

    case WM_DESTROY:
    {
        if(pRenderTarget) pRenderTarget->Release();
        if(pFactory) pFactory->Release();
        if(pDWriteFactory) pDWriteFactory->Release();
        PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    const wchar_t CLASS_NAME[] = L"CoreBaseWindow";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(
        0, CLASS_NAME, L"CoreBase", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720,
        nullptr, nullptr, hInstance, nullptr
    );

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg{};
    while(GetMessageW(&msg, nullptr,0,0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}