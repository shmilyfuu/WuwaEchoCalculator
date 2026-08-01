#define UNICODE
#define _UNICODE
#include <windows.h>
#include <dwmapi.h>
#include <wrl.h>
#include <WebView2.h>
#include <filesystem>
#include <string>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {
HWND g_window = nullptr;
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webview;
EventRegistrationToken g_messageToken{};
std::wstring g_exeDir;

constexpr wchar_t kWindowClass[] = L"WuwaEchoCalculatorWindow";
constexpr wchar_t kWindowTitle[] = L"鸣潮声骸计算器";
constexpr int kClientWidthDips = 1188;
constexpr int kClientHeightDips = 772;
constexpr DWORD kWindowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;

SIZE WindowSizeForDpi(UINT dpi) {
    RECT rect{
        0,
        0,
        MulDiv(kClientWidthDips, static_cast<int>(dpi), 96),
        MulDiv(kClientHeightDips, static_cast<int>(dpi), 96)
    };
    AdjustWindowRectExForDpi(&rect, kWindowStyle, FALSE, 0, dpi);
    return SIZE{rect.right - rect.left, rect.bottom - rect.top};
}

std::wstring GetExecutableDirectory() {
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return L".";
    return std::filesystem::path(path).parent_path().wstring();
}

void ApplyWindows11Appearance(HWND hwnd) {
    BOOL dark = TRUE;
    constexpr DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_VALUE = 20;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_VALUE, &dark, sizeof(dark));
    constexpr DWORD DWMWA_SYSTEMBACKDROP_TYPE_VALUE = 38;
    int backdrop = 2;
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE_VALUE, &backdrop, sizeof(backdrop));
    const COLORREF captionColor = RGB(32, 32, 32);
    constexpr DWORD DWMWA_CAPTION_COLOR_VALUE = 35;
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR_VALUE, &captionColor, sizeof(captionColor));
}

void ResizeWebView() {
    if (!g_controller || !g_window) return;
    RECT bounds{};
    GetClientRect(g_window, &bounds);
    g_controller->put_Bounds(bounds);
}

void SetTopmost(bool enabled) {
    if (!g_window) return;
    SetWindowPos(g_window, enabled ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void ShowRuntimeError(HRESULT hr) {
    wchar_t message[768]{};
    swprintf_s(message,
        L"WebView2 初始化失败（0x%08X）。\n\n请确认 Microsoft Edge WebView2 Runtime 已安装。Windows 11 通常已包含该组件。",
        static_cast<unsigned int>(hr));
    MessageBoxW(g_window, message, kWindowTitle, MB_OK | MB_ICONERROR);
}

void InitializeWebView() {
    const std::filesystem::path webDir = std::filesystem::path(g_exeDir) / L"web";
    const std::filesystem::path dataDir = std::filesystem::path(g_exeDir) / L"data" / L"WebView2";

    if (!std::filesystem::exists(webDir / L"panel.html")) {
        MessageBoxW(g_window, L"未找到 web\\panel.html。请完整解压应用文件夹后再启动。", kWindowTitle, MB_OK | MB_ICONERROR);
        return;
    }

    std::error_code error;
    std::filesystem::create_directories(dataDir, error);

    const HRESULT startResult = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, dataDir.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [webDir](HRESULT environmentResult, ICoreWebView2Environment* environment) -> HRESULT {
                if (FAILED(environmentResult) || environment == nullptr) {
                    ShowRuntimeError(environmentResult);
                    return environmentResult;
                }
                return environment->CreateCoreWebView2Controller(
                    g_window,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [webDir](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(controllerResult) || controller == nullptr) {
                                ShowRuntimeError(controllerResult);
                                return controllerResult;
                            }
                            g_controller = controller;
                            controller->get_CoreWebView2(&g_webview);
                            if (!g_webview) return E_FAIL;

                            ComPtr<ICoreWebView2Controller2> controller2;
                            if (SUCCEEDED(g_controller.As(&controller2))) {
                                COREWEBVIEW2_COLOR background{255, 32, 32, 32};
                                controller2->put_DefaultBackgroundColor(background);
                            }

                            ComPtr<ICoreWebView2Settings> settings;
                            if (SUCCEEDED(g_webview->get_Settings(&settings)) && settings) {
                                settings->put_IsScriptEnabled(TRUE);
                                settings->put_AreDefaultScriptDialogsEnabled(TRUE);
                                settings->put_IsWebMessageEnabled(TRUE);
                                settings->put_AreDevToolsEnabled(FALSE);
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->put_IsZoomControlEnabled(FALSE);
                            }

                            ComPtr<ICoreWebView2_3> webview3;
                            if (FAILED(g_webview.As(&webview3)) || !webview3) {
                                MessageBoxW(g_window, L"当前 WebView2 Runtime 版本过旧，请更新 Microsoft Edge WebView2 Runtime。", kWindowTitle, MB_OK | MB_ICONERROR);
                                return E_NOINTERFACE;
                            }

                            const HRESULT mapResult = webview3->SetVirtualHostNameToFolderMapping(
                                L"app.local", webDir.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
                            if (FAILED(mapResult)) {
                                ShowRuntimeError(mapResult);
                                return mapResult;
                            }

                            g_webview->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        LPWSTR raw = nullptr;
                                        if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw) {
                                            const std::wstring value(raw);
                                            CoTaskMemFree(raw);
                                            if (value == L"topmost:1") SetTopmost(true);
                                            else if (value == L"topmost:0") SetTopmost(false);
                                        }
                                        return S_OK;
                                    }).Get(),
                                &g_messageToken);

                            ResizeWebView();
                            g_controller->put_IsVisible(TRUE);
                            return g_webview->Navigate(L"https://app.local/panel.html");
                        }).Get());
            }).Get());

    if (FAILED(startResult)) ShowRuntimeError(startResult);
}

LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        g_window = hwnd;
        ApplyWindows11Appearance(hwnd);
        InitializeWebView();
        return 0;
    case WM_SIZE:
        ResizeWebView();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        const SIZE size = WindowSizeForDpi(GetDpiForWindow(hwnd));
        info->ptMinTrackSize.x = size.cx;
        info->ptMinTrackSize.y = size.cy;
        info->ptMaxTrackSize.x = size.cx;
        info->ptMaxTrackSize.y = size.cy;
        return 0;
    }
    case WM_DPICHANGED: {
        const RECT* suggested = reinterpret_cast<RECT*>(lParam);
        const SIZE size = WindowSizeForDpi(HIWORD(wParam));
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, size.cx, size.cy,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DESTROY:
        if (g_webview) g_webview->remove_WebMessageReceived(g_messageToken);
        if (g_controller) g_controller->Close();
        g_webview.Reset();
        g_controller.Reset();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult)) return 1;

    g_exeDir = GetExecutableDirectory();

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.lpszClassName = kWindowClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    windowClass.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(101));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));

    if (!RegisterClassExW(&windowClass)) {
        CoUninitialize();
        return 1;
    }

    const SIZE windowSize = WindowSizeForDpi(GetDpiForSystem());
    const int x = max(0, (GetSystemMetrics(SM_CXSCREEN) - windowSize.cx) / 2);
    const int y = max(0, (GetSystemMetrics(SM_CYSCREEN) - windowSize.cy) / 2);

    HWND hwnd = CreateWindowExW(0, kWindowClass, kWindowTitle, kWindowStyle,
                                x, y, windowSize.cx, windowSize.cy, nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        CoUninitialize();
        return 1;
    }

    ShowWindow(hwnd, showCommand == SW_SHOWMINIMIZED ? SW_SHOWMINIMIZED : SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    CoUninitialize();
    return static_cast<int>(message.wParam);
}
