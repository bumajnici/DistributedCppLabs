// lab3_app.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "lab3_app.h"

#include <cstdio>
#include <deque>
#include <iomanip>
#include <random>
#include <sstream>
#include <vector>

#define MAX_LOADSTRING 100
#define BUFFER_SIZE 10
#define TIMER_ID 1
#define TIMER_INTERVAL 100
#define DEFAULT_WORKER_COUNT 3
#define MAX_WORKER_COUNT 64
#define IDC_THREAD_COUNT_INPUT 2001
#define IDC_APPLY_THREADS 2002
#define IDC_STATUS_TEXT 2003

HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];

ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);

struct SharedState
{
    std::deque<int> numbers;
    int currentNumber = 0;
    double average = 0.0;
};

HWND mainWindow = nullptr;
HWND threadCountInput = nullptr;
HWND statusText = nullptr;
HANDLE resourceMutex = nullptr;
HANDLE startEvent = nullptr;
HANDLE stopEvent = nullptr;
HANDLE dataEvent = nullptr;
std::vector<HANDLE> workerHandles;
SharedState sharedState;

void LogMessage(const char* message, DWORD threadId)
{
    std::printf("[Thread %lu] %s\n", static_cast<unsigned long>(threadId), message);
    std::fflush(stdout);
}

void LogResourceAccess(DWORD threadId, int number)
{
    std::printf("[Thread %lu] Accessing shared resource, generated %d\n",
        static_cast<unsigned long>(threadId), number);
    std::fflush(stdout);
}

DWORD WINAPI WorkerThreadProc(LPVOID parameter)
{
    UNREFERENCED_PARAMETER(parameter);
    const DWORD threadId = GetCurrentThreadId();
    LogMessage("Started", threadId);

    if (WaitForSingleObject(startEvent, INFINITE) != WAIT_OBJECT_0)
    {
        LogMessage("Failed to receive start event", threadId);
        ExitThread(1);
    }

    std::mt19937 generator(static_cast<unsigned int>(GetTickCount64() ^ threadId));
    std::uniform_int_distribution<int> distribution(1, 100);

    while (WaitForSingleObject(stopEvent, 100) == WAIT_TIMEOUT)
    {
        const int number = distribution(generator);

        if (WaitForSingleObject(resourceMutex, INFINITE) != WAIT_OBJECT_0)
        {
            LogMessage("Failed to acquire mutex", threadId);
            ExitThread(2);
        }

        sharedState.currentNumber = number;
        sharedState.numbers.push_back(number);
        if (sharedState.numbers.size() > BUFFER_SIZE)
        {
            sharedState.numbers.pop_front();
        }

        double sum = 0.0;
        for (const int value : sharedState.numbers)
        {
            sum += value;
        }
        sharedState.average = sum / sharedState.numbers.size();
        LogResourceAccess(threadId, number);

        ReleaseMutex(resourceMutex);
        SetEvent(dataEvent);
        Sleep(100);
    }

    LogMessage("Finished", threadId);
    ExitThread(0);
}

void StopWorkers()
{
    if (workerHandles.empty())
    {
        return;
    }

    SetEvent(stopEvent);
    WaitForMultipleObjects(static_cast<DWORD>(workerHandles.size()),
        workerHandles.data(), TRUE, INFINITE);

    for (HANDLE worker : workerHandles)
    {
        CloseHandle(worker);
    }
    workerHandles.clear();
    ResetEvent(stopEvent);
    ResetEvent(dataEvent);
}

bool StartWorkers(int count)
{
    StopWorkers();
    ResetEvent(stopEvent);
    SetEvent(startEvent);

    for (int index = 0; index < count; ++index)
    {
        HANDLE worker = CreateThread(nullptr, 0, WorkerThreadProc, nullptr, 0, nullptr);
        if (worker == nullptr)
        {
            StopWorkers();
            return false;
        }
        workerHandles.push_back(worker);
    }

    return true;
}

void UpdateDisplayedValues(HWND hWnd)
{
    if (WaitForSingleObject(dataEvent, 0) != WAIT_OBJECT_0 ||
        WaitForSingleObject(resourceMutex, INFINITE) != WAIT_OBJECT_0)
    {
        return;
    }

    std::wstringstream text;
    text << L"Current number: " << sharedState.currentNumber << L"\n"
         << L"Average of the last " << BUFFER_SIZE << L" numbers: "
         << std::fixed << std::setprecision(2) << sharedState.average;
    SetWindowTextW(statusText, text.str().c_str());

    ReleaseMutex(resourceMutex);
    ResetEvent(dataEvent);
}

void MonitorWorkers()
{
    if (workerHandles.empty() || WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0)
    {
        return;
    }

    const DWORD result = WaitForMultipleObjects(
        static_cast<DWORD>(workerHandles.size()), workerHandles.data(), FALSE, 0);
    if (result >= WAIT_OBJECT_0 &&
        result < WAIT_OBJECT_0 + workerHandles.size())
    {
        SetWindowTextW(statusText, L"A worker terminated unexpectedly. Restarting...");
        StartWorkers(static_cast<int>(workerHandles.size()));
    }
}

int ReadWorkerCount()
{
    wchar_t buffer[16]{};
    GetWindowTextW(threadCountInput, buffer, _countof(buffer));
    const int count = _wtoi(buffer);
    return count;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    AllocConsole();
    FILE* output = nullptr;
    freopen_s(&output, "CONOUT$", "w", stdout);

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_LAB3APP, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow))
    {
        FreeConsole();
        return FALSE;
    }

    resourceMutex = CreateMutexW(nullptr, FALSE, nullptr);
    startEvent = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    dataEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    if (resourceMutex == nullptr || startEvent == nullptr ||
        stopEvent == nullptr || dataEvent == nullptr ||
        !StartWorkers(DEFAULT_WORKER_COUNT))
    {
        DestroyWindow(mainWindow);
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return static_cast<int>(msg.wParam);
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_LAB3APP));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_LAB3APP);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));
    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;
    HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 620, 360, nullptr, nullptr, hInstance, nullptr);
    if (!hWnd)
    {
        return FALSE;
    }

    mainWindow = hWnd;
    CreateWindowW(L"STATIC", L"Worker threads:", WS_CHILD | WS_VISIBLE,
        20, 20, 110, 24, hWnd, nullptr, hInstance, nullptr);
    threadCountInput = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"3",
        WS_CHILD | WS_VISIBLE | ES_NUMBER, 135, 18, 60, 26, hWnd,
        reinterpret_cast<HMENU>(IDC_THREAD_COUNT_INPUT), hInstance, nullptr);
    CreateWindowW(L"BUTTON", L"Apply", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        205, 18, 80, 26, hWnd, reinterpret_cast<HMENU>(IDC_APPLY_THREADS),
        hInstance, nullptr);
    statusText = CreateWindowW(L"STATIC", L"Starting workers...",
        WS_CHILD | WS_VISIBLE, 20, 80, 560, 100, hWnd, nullptr, hInstance, nullptr);

    SetTimer(hWnd, TIMER_ID, TIMER_INTERVAL, nullptr);
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_APPLY_THREADS)
        {
            const int count = ReadWorkerCount();
            if (count < 1 || count > MAX_WORKER_COUNT)
            {
                SetWindowTextW(statusText, L"Enter a value from 1 to 64.");
                break;
            }

            if (StartWorkers(count))
            {
                std::wstringstream status;
                status << L"Running " << count << L" worker threads.";
                SetWindowTextW(statusText, status.str().c_str());
            }
            else
            {
                SetWindowTextW(statusText, L"Failed to create worker threads.");
            }
        }
        else if (LOWORD(wParam) == IDM_ABOUT)
        {
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
        }
        else if (LOWORD(wParam) == IDM_EXIT)
        {
            DestroyWindow(hWnd);
        }
        else
        {
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        break;
    case WM_TIMER:
        UpdateDisplayedValues(hWnd);
        MonitorWorkers();
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT clientRect;
            GetClientRect(hWnd, &clientRect);
            DrawTextW(hdc, L"Use Apply to change the number of running worker threads.",
                -1, &clientRect, DT_CENTER | DT_BOTTOM | DT_SINGLELINE);
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        KillTimer(hWnd, TIMER_ID);
        StopWorkers();
        if (resourceMutex != nullptr) CloseHandle(resourceMutex);
        if (startEvent != nullptr) CloseHandle(startEvent);
        if (stopEvent != nullptr) CloseHandle(stopEvent);
        if (dataEvent != nullptr) CloseHandle(dataEvent);
        PostQuitMessage(0);
        FreeConsole();
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return static_cast<INT_PTR>(TRUE);
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return static_cast<INT_PTR>(TRUE);
        }
        break;
    }
    return static_cast<INT_PTR>(FALSE);
}
