// lab2_app.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "lab2_app.h"

#include <cmath>
#include <commdlg.h>
#include <vector>
#include <windowsx.h>

#pragma comment(lib, "Comdlg32.lib")

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

std::vector<POINT> figurePoints;
bool isDrawing = false;
bool isClosed = false;
COLORREF fillColor = RGB(20, 50, 250);
COLORREF hatchColor = RGB(30, 80, 160);
int hatchStep = 12;

void ClearFigure(HWND hWnd)
{
    figurePoints.clear();
    isDrawing = false;
    isClosed = false;
    ReleaseCapture();
    InvalidateRect(hWnd, nullptr, TRUE);
}

bool ChooseColor(HWND hWnd, COLORREF& color)
{
    static COLORREF customColors[16] = {};
    CHOOSECOLOR chooseColor = {};
    chooseColor.lStructSize = sizeof(chooseColor);
    chooseColor.hwndOwner = hWnd;
    chooseColor.rgbResult = color;
    chooseColor.lpCustColors = customColors;
    chooseColor.Flags = CC_FULLOPEN | CC_RGBINIT;

    if (!::ChooseColor(&chooseColor))
    {
        return false;
    }

    color = chooseColor.rgbResult;
    return true;
}

void AddPointIfNeeded(LPARAM lParam)
{
    POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    if (figurePoints.empty())
    {
        figurePoints.push_back(point);
        return;
    }

    const POINT& previous = figurePoints.back();
    const int dx = point.x - previous.x;
    const int dy = point.y - previous.y;
    if (dx * dx + dy * dy >= 9)
    {
        figurePoints.push_back(point);
    }
}

void BuildFigurePath(HDC hdc)
{
    BeginPath(hdc);
    MoveToEx(hdc, figurePoints.front().x, figurePoints.front().y, nullptr);
    for (size_t i = 1; i < figurePoints.size(); ++i)
    {
        LineTo(hdc, figurePoints[i].x, figurePoints[i].y);
    }
    CloseFigure(hdc);
    EndPath(hdc);
}

void DrawFigure(HDC hdc, const RECT& clientRect)
{
    if (figurePoints.size() < 3)
    {
        return;
    }

    BuildFigurePath(hdc);

    HBRUSH fillBrush = CreateSolidBrush(fillColor);
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, fillBrush));
    FillPath(hdc);
    SelectObject(hdc, oldBrush);
    DeleteObject(fillBrush);

    BuildFigurePath(hdc);
    HRGN figureRegion = PathToRegion(hdc);
    if (figureRegion != nullptr)
    {
        int savedState = SaveDC(hdc);
        SelectClipRgn(hdc, figureRegion);

        HPEN hatchPen = CreatePen(PS_SOLID, 1, hatchColor);
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, hatchPen));
        for (int y = clientRect.top - clientRect.right; y < clientRect.bottom + clientRect.right; y += hatchStep)
        {
            MoveToEx(hdc, clientRect.left, y, nullptr);
            LineTo(hdc, clientRect.right, y + clientRect.right - clientRect.left);
        }
        SelectObject(hdc, oldPen);
        DeleteObject(hatchPen);
        RestoreDC(hdc, savedState);
        DeleteObject(figureRegion);
    }

    BuildFigurePath(hdc);
    HPEN outlinePen = CreatePen(PS_SOLID, 2, hatchColor);
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, outlinePen));
    HBRUSH oldOutlineBrush = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
    StrokePath(hdc);
    SelectObject(hdc, oldOutlineBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(outlinePen);
}

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_LAB2APP, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_LAB2APP));

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_LAB2APP));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_LAB2APP);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // Parse the menu selections:
            switch (wmId)
            {
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_LBUTTONDOWN:
        if (!isDrawing)
        {
            figurePoints.clear();
            isClosed = false;
            isDrawing = true;
            SetCapture(hWnd);
            AddPointIfNeeded(lParam);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    case WM_MOUSEMOVE:
        if (isDrawing && (wParam & MK_LBUTTON) != 0)
        {
            AddPointIfNeeded(lParam);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    case WM_LBUTTONUP:
        if (isDrawing)
        {
            AddPointIfNeeded(lParam);
            isDrawing = false;
            isClosed = figurePoints.size() >= 3;
            ReleaseCapture();
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    case WM_KEYDOWN:
        if (wParam == 'C')
        {
            ClearFigure(hWnd);
        }
        else if (wParam == 'B')
        {
            if (ChooseColor(hWnd, fillColor))
            {
                InvalidateRect(hWnd, nullptr, TRUE);
            }
        }
        else if (wParam == 'H')
        {
            if (ChooseColor(hWnd, hatchColor))
            {
                InvalidateRect(hWnd, nullptr, TRUE);
            }
        }
        else if (wParam == VK_ADD || wParam == '=' || wParam == VK_OEM_PLUS)
        {
            hatchStep = min(hatchStep + 2, 100);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        else if (wParam == VK_SUBTRACT || wParam == '-' || wParam == VK_OEM_MINUS)
        {
            hatchStep = max(hatchStep - 2, 2);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT clientRect;
        GetClientRect(hWnd, &clientRect);
        FillRect(
            hdc,
            &clientRect,
            static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH))
        );

        if (isClosed)
        {
            DrawFigure(hdc, clientRect);
        }
        else if (figurePoints.size() >= 2)
        {
            HPEN previewPen = CreatePen(
                PS_SOLID,
                2,
                RGB(90, 90, 90)
            );
            HPEN oldPen =
                static_cast<HPEN>(SelectObject(hdc, previewPen));

            MoveToEx(
                hdc,
                figurePoints.front().x,
                figurePoints.front().y,
                nullptr
            );

            for (size_t i = 1; i < figurePoints.size(); ++i)
            {
                LineTo(
                    hdc,
                    figurePoints[i].x,
                    figurePoints[i].y
                );
            }

            SelectObject(hdc, oldPen);
            DeleteObject(previewPen);
        }

        SetBkMode(hdc, TRANSPARENT);

        const wchar_t* helpText =
            L"Draw with the left mouse button | "
            L"B: fill color | H: hatch color | "
            L"+/-: hatch step | C: clear";

        TextOutW(
            hdc,
            10,
            10,
            helpText,
            static_cast<int>(wcslen(helpText))
        );

        EndPaint(hWnd, &ps);
    }
    break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
