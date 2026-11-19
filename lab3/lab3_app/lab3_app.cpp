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
#define IDC_STOP_THREADS 2004
#define IDC_EXPERIMENT 2005
#define IDC_EXPERIMENT_COUNT 3001
#define IDC_EXPERIMENT_START 3002
#define IDC_EXPERIMENT_CANCEL 3003
#define IDC_EXPERIMENT_ITERATIONS 3004
#define WM_EXPERIMENT_DONE (WM_APP + 2)
#define EXPERIMENT_MAX_VALUES 1000000000
#define EXPERIMENT_DEFAULT_REPETITIONS 10
#define EXPERIMENT_MAX_REPETITIONS 1000

HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];

ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ExperimentSettingsProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ExperimentResultsProc(HWND, UINT, WPARAM, LPARAM);
void StopWorkers();
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

struct ExperimentResult
{
    int threadCount;
    double elapsedMilliseconds;
    double cpuLoadPercent;
};

struct ExperimentWorkerContext
{
    size_t first;
    size_t last;
};

HANDLE experimentMutex = nullptr;
HANDLE experimentController = nullptr;
HWND experimentSettingsWindow = nullptr;
HWND experimentResultsWindow = nullptr;
std::vector<int> experimentValues;
std::vector<ExperimentResult> experimentResults;
long long experimentSum = 0;
bool experimentRunning = false;
int experimentRepetitions = EXPERIMENT_DEFAULT_REPETITIONS;

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

DWORD WINAPI ExperimentWorkerProc(LPVOID parameter)
{
    auto* context = static_cast<ExperimentWorkerContext*>(parameter);
    long long localSum = 0;
    for (int repetition = 0; repetition < experimentRepetitions; ++repetition)
    {
        for (size_t index = context->first; index < context->last; ++index)
        {
            localSum += experimentValues[index];
        }
    }

    if (WaitForSingleObject(experimentMutex, INFINITE) == WAIT_OBJECT_0)
    {
        experimentSum += localSum;
        ReleaseMutex(experimentMutex);
    }

    delete context;
    ExitThread(0);
}

double FileTimeToMilliseconds(const FILETIME& time)
{
    ULARGE_INTEGER value{};
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return static_cast<double>(value.QuadPart) / 10000.0;
}

ExperimentResult RunExperiment(int threadCount)
{
    LARGE_INTEGER frequency{}, startCounter{}, endCounter{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&startCounter);

    FILETIME creation{}, exit{}, kernelStart{}, userStart{};
    FILETIME kernelEnd{}, userEnd{};
    GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernelStart, &userStart);

    experimentSum = 0;
    std::vector<HANDLE> handles;
    const size_t valuesPerThread = experimentValues.size() / threadCount;
    size_t first = 0;

    for (int index = 0; index < threadCount; ++index)
    {
        const size_t last = index == threadCount - 1
            ? experimentValues.size()
            : first + valuesPerThread;
        auto* context = new ExperimentWorkerContext{ first, last };
        HANDLE worker = CreateThread(nullptr, 0, ExperimentWorkerProc, context, 0, nullptr);
        if (worker == nullptr)
        {
            delete context;
            continue;
        }
        handles.push_back(worker);
        first = last;
    }

    if (!handles.empty())
    {
        WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), TRUE, INFINITE);
    }
    for (HANDLE handle : handles)
    {
        CloseHandle(handle);
    }

    GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernelEnd, &userEnd);
    QueryPerformanceCounter(&endCounter);

    const double totalElapsedMilliseconds = static_cast<double>(endCounter.QuadPart - startCounter.QuadPart)
        * 1000.0 / static_cast<double>(frequency.QuadPart);
    const double processMilliseconds =
        FileTimeToMilliseconds(kernelEnd) + FileTimeToMilliseconds(userEnd)
        - FileTimeToMilliseconds(kernelStart) - FileTimeToMilliseconds(userStart);
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const double cpuLoad = totalElapsedMilliseconds > 0.0
        ? processMilliseconds / totalElapsedMilliseconds * 100.0 / systemInfo.dwNumberOfProcessors
        : 0.0;
    const double elapsedMilliseconds = totalElapsedMilliseconds / experimentRepetitions;

    return { threadCount, elapsedMilliseconds, cpuLoad };
}

DWORD WINAPI ExperimentControllerProc(LPVOID parameter)
{
    const int valueCount = static_cast<int>(reinterpret_cast<INT_PTR>(parameter));
    experimentValues.resize(valueCount);
    std::mt19937 generator(123456789u);
    std::uniform_int_distribution<int> distribution(1, 100);
    for (int& value : experimentValues)
    {
        value = distribution(generator);
    }

    experimentResults.clear();
    for (const int threadCount : { 2, 4, 8, 16 })
    {
        experimentResults.push_back(RunExperiment(threadCount));
    }

    PostMessage(mainWindow, WM_EXPERIMENT_DONE, 0, 0);
    ExitThread(0);
}

void ShowExperimentResults()
{
    std::wstringstream table;
    table << std::left << std::setw(12) << L"Threads"
          << std::setw(26) << L"Execution time (ms)"
          << L"Average CPU load (%)\r\n";
    table << L"--------------------------------------------------------------------------\r\n";
    for (const ExperimentResult& result : experimentResults)
    {
        table << std::left << std::setw(12) << result.threadCount
              << std::setw(26) << std::fixed << std::setprecision(2)
              << result.elapsedMilliseconds
              << std::fixed << std::setprecision(6) << result.cpuLoadPercent << L"\r\n";
    }

    experimentResultsWindow = CreateWindowExW(
        WS_EX_DLGMODALFRAME, L"Lab3ExperimentResults", L"Experiment Results",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 780, 260, mainWindow, nullptr, hInst, nullptr);
    HWND tableControl = CreateWindowW(L"EDIT", table.str().c_str(),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY,
        15, 15, 740, 180, experimentResultsWindow, nullptr, hInst, nullptr);
    SendMessage(tableControl, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(SYSTEM_FIXED_FONT)), TRUE);
}

void ShowExperimentSettings()
{
    if (experimentSettingsWindow != nullptr)
    {
        SetForegroundWindow(experimentSettingsWindow);
        return;
    }

    experimentSettingsWindow = CreateWindowExW(
        WS_EX_DLGMODALFRAME, L"Lab3ExperimentSettings", L"Experiment Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 440, 240, mainWindow, nullptr, hInst, nullptr);
    CreateWindowW(L"STATIC", L"Number of values (1 - 1000000000):",
        WS_CHILD | WS_VISIBLE, 20, 20, 230, 24, experimentSettingsWindow,
        nullptr, hInst, nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"10000000",
        WS_CHILD | WS_VISIBLE | ES_NUMBER, 20, 48, 180, 26,
        experimentSettingsWindow, reinterpret_cast<HMENU>(IDC_EXPERIMENT_COUNT), hInst, nullptr);
    CreateWindowW(L"STATIC", L"Iterations (1 - 1000):",
        WS_CHILD | WS_VISIBLE, 20, 82, 230, 24, experimentSettingsWindow,
        nullptr, hInst, nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"10",
        WS_CHILD | WS_VISIBLE | ES_NUMBER, 20, 110, 180, 26,
        experimentSettingsWindow, reinterpret_cast<HMENU>(IDC_EXPERIMENT_ITERATIONS), hInst, nullptr);
    CreateWindowW(L"BUTTON", L"Start", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        20, 155, 90, 28, experimentSettingsWindow,
        reinterpret_cast<HMENU>(IDC_EXPERIMENT_START), hInst, nullptr);
    CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        120, 155, 90, 28, experimentSettingsWindow,
        reinterpret_cast<HMENU>(IDC_EXPERIMENT_CANCEL), hInst, nullptr);
    ShowWindow(experimentSettingsWindow, SW_SHOW);
}

void RegisterExperimentWindowClasses()
{
    WNDCLASSW settingsClass{};
    settingsClass.lpfnWndProc = ExperimentSettingsProc;
    settingsClass.hInstance = hInst;
    settingsClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    settingsClass.lpszClassName = L"Lab3ExperimentSettings";
    RegisterClassW(&settingsClass);

    WNDCLASSW resultsClass{};
    resultsClass.lpfnWndProc = ExperimentResultsProc;
    resultsClass.hInstance = hInst;
    resultsClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    resultsClass.lpszClassName = L"Lab3ExperimentResults";
    RegisterClassW(&resultsClass);
}

LRESULT CALLBACK ExperimentSettingsProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_EXPERIMENT_CANCEL)
        {
            DestroyWindow(hWnd);
        }
        else if (LOWORD(wParam) == IDC_EXPERIMENT_START)
        {
            wchar_t buffer[32]{};
            HWND input = GetDlgItem(hWnd, IDC_EXPERIMENT_COUNT);
            GetWindowTextW(input, buffer, _countof(buffer));
            const int valueCount = _wtoi(buffer);
            if (valueCount < 1 || valueCount > EXPERIMENT_MAX_VALUES)
            {
                MessageBoxW(hWnd, L"Enter a value from 1 to 1000000000.",
                    L"Invalid value", MB_OK | MB_ICONWARNING);
                break;
            }

            wchar_t iterationBuffer[16]{};
            HWND iterationInput = GetDlgItem(hWnd, IDC_EXPERIMENT_ITERATIONS);
            GetWindowTextW(iterationInput, iterationBuffer, _countof(iterationBuffer));
            const int repetitions = _wtoi(iterationBuffer);
            if (repetitions < 1 || repetitions > EXPERIMENT_MAX_REPETITIONS)
            {
                MessageBoxW(hWnd, L"Enter the number of iterations from 1 to 1000.",
                    L"Invalid value", MB_OK | MB_ICONWARNING);
                break;
            }

            if (!workerHandles.empty())
            {
                const int answer = MessageBoxW(hWnd,
                    L"Active worker threads will be stopped before the experiment starts. Continue?",
                    L"Confirm experiment", MB_YESNO | MB_ICONQUESTION);
                if (answer != IDYES)
                {
                    break;
                }
                StopWorkers();
            }

            experimentRepetitions = repetitions;
            experimentRunning = true;
            EnableWindow(GetDlgItem(hWnd, IDC_EXPERIMENT_START), FALSE);
            DestroyWindow(hWnd);
            experimentController = CreateThread(nullptr, 0, ExperimentControllerProc,
                reinterpret_cast<LPVOID>(static_cast<INT_PTR>(valueCount)), 0, nullptr);
            if (experimentController == nullptr)
            {
                experimentRunning = false;
                MessageBoxW(mainWindow, L"Failed to start the experiment.",
                    L"Experiment error", MB_OK | MB_ICONERROR);
            }
        }
        break;
    case WM_DESTROY:
        experimentSettingsWindow = nullptr;
        break;
    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

LRESULT CALLBACK ExperimentResultsProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);
    if (message == WM_DESTROY)
    {
        experimentResultsWindow = nullptr;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
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

    RegisterExperimentWindowClasses();
    resourceMutex = CreateMutexW(nullptr, FALSE, nullptr);
    experimentMutex = CreateMutexW(nullptr, FALSE, nullptr);
    startEvent = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    dataEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    if (resourceMutex == nullptr || experimentMutex == nullptr || startEvent == nullptr ||
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
    CreateWindowW(L"BUTTON", L"Stop", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        295, 18, 80, 26, hWnd, reinterpret_cast<HMENU>(IDC_STOP_THREADS),
        hInstance, nullptr);
    CreateWindowW(L"BUTTON", L"Experiment", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        385, 18, 100, 26, hWnd, reinterpret_cast<HMENU>(IDC_EXPERIMENT),
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
        else if (LOWORD(wParam) == IDC_STOP_THREADS)
        {
            StopWorkers();
        }
        else if (LOWORD(wParam) == IDC_EXPERIMENT)
        {
            if (experimentRunning)
            {
                MessageBoxW(hWnd, L"An experiment is already running.",
                    L"Experiment", MB_OK | MB_ICONINFORMATION);
            }
            else
            {
                ShowExperimentSettings();
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
        if (experimentController != nullptr)
        {
            WaitForSingleObject(experimentController, INFINITE);
            CloseHandle(experimentController);
            experimentController = nullptr;
        }
        if (resourceMutex != nullptr) CloseHandle(resourceMutex);
        if (startEvent != nullptr) CloseHandle(startEvent);
        if (stopEvent != nullptr) CloseHandle(stopEvent);
        if (dataEvent != nullptr) CloseHandle(dataEvent);
        if (experimentMutex != nullptr) CloseHandle(experimentMutex);
        PostQuitMessage(0);
        FreeConsole();
        break;
    case WM_EXPERIMENT_DONE:
        if (experimentController != nullptr)
        {
            WaitForSingleObject(experimentController, INFINITE);
            CloseHandle(experimentController);
            experimentController = nullptr;
        }
        experimentRunning = false;
        ShowExperimentResults();
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
