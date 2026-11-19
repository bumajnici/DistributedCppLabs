// lab3_app.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "lab3_app.h"

#include <condition_variable>
#include <chrono>
#include <deque>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

#define MAX_LOADSTRING 100
#define BUFFER_SIZE 10
#define WM_UPDATE_VALUES (WM_APP + 1)

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

struct DisplayValues
{
    int current;
    double average;
};

std::mutex dataMutex;
std::condition_variable dataAvailable;
std::condition_variable resultAvailable;
std::deque<int> numberBuffer;
int currentNumber = 0;
double currentAverage = 0.0;
unsigned long long producedVersion = 0;
unsigned long long calculatedVersion = 0;
unsigned long long displayedVersion = 0;
bool stopThreads = false;
HWND mainWindow = nullptr;
std::thread generatorThread;
std::thread calculatorThread;
std::thread displayThread;

void GenerateNumbers();
void CalculateAverage();
void DisplayValuesThread();
void StopThreads();

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_LAB3APP, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    generatorThread = std::thread(GenerateNumbers);
    calculatorThread = std::thread(CalculateAverage);
    displayThread = std::thread(DisplayValuesThread);

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_LAB3APP));

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
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_LAB3APP));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_LAB3APP);
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

   mainWindow = hWnd;

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
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            std::wstringstream text;
            text << L"Current number: " << currentNumber << L"\n"
                 << L"Average of the last " << BUFFER_SIZE << L" numbers: "
                 << std::fixed << std::setprecision(2) << currentAverage;

            RECT clientRect;
            GetClientRect(hWnd, &clientRect);
            DrawTextW(hdc, text.str().c_str(), -1, &clientRect,
                DT_CENTER | DT_VCENTER | DT_WORDBREAK);
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_UPDATE_VALUES:
        {
            DisplayValues* values = reinterpret_cast<DisplayValues*>(lParam);
            currentNumber = values->current;
            currentAverage = values->average;
            delete values;
            InvalidateRect(hWnd, nullptr, TRUE);
        }
        break;
    case WM_DESTROY:
        StopThreads();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

void GenerateNumbers()
{
    std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<int> distribution(1, 100);

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        std::lock_guard<std::mutex> lock(dataMutex);
        if (stopThreads)
        {
            return;
        }

        currentNumber = distribution(generator);
        numberBuffer.push_back(currentNumber);
        if (numberBuffer.size() > BUFFER_SIZE)
        {
            numberBuffer.pop_front();
        }
        ++producedVersion;
        dataAvailable.notify_one();
    }
}

void CalculateAverage()
{
    while (true)
    {
        std::unique_lock<std::mutex> lock(dataMutex);
        dataAvailable.wait(lock, []
        {
            return stopThreads || producedVersion != calculatedVersion;
        });

        if (stopThreads)
        {
            return;
        }

        double sum = 0.0;
        for (int number : numberBuffer)
        {
            sum += number;
        }
        currentAverage = sum / numberBuffer.size();
        calculatedVersion = producedVersion;
        resultAvailable.notify_one();
    }
}

void DisplayValuesThread()
{
    while (true)
    {
        std::unique_lock<std::mutex> lock(dataMutex);
        resultAvailable.wait(lock, []
        {
            return stopThreads || calculatedVersion != displayedVersion;
        });

        if (stopThreads)
        {
            return;
        }

        displayedVersion = calculatedVersion;
        DisplayValues* values = new DisplayValues{ currentNumber, currentAverage };
        lock.unlock();
        PostMessage(mainWindow, WM_UPDATE_VALUES, 0,
            reinterpret_cast<LPARAM>(values));
    }
}

void StopThreads()
{
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        stopThreads = true;
    }

    dataAvailable.notify_all();
    resultAvailable.notify_all();

    if (generatorThread.joinable())
    {
        generatorThread.join();
    }
    if (calculatorThread.joinable())
    {
        calculatorThread.join();
    }
    if (displayThread.joinable())
    {
        displayThread.join();
    }
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
