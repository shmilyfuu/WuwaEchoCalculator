#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace {

constexpr wchar_t kClassName[] = L"WuwaEchoUpdaterWindow";
constexpr UINT kProgressMessage = WM_APP + 1;
constexpr UINT kCompleteMessage = WM_APP + 2;
constexpr UINT kFailureMessage = WM_APP + 3;

struct Arguments {
    DWORD parentPid = 0;
    std::filesystem::path target;
    std::filesystem::path package;
    std::filesystem::path restart;
    std::wstring version;
};

struct ProgressState {
    int percent = 0;
    std::wstring phase = L"正在准备更新";
    std::wstring detail;
    bool failed = false;
    bool complete = false;
};

HWND g_window = nullptr;
std::mutex g_stateMutex;
ProgressState g_state;
HFONT g_titleFont = nullptr;
HFONT g_bodyFont = nullptr;
HFONT g_smallFont = nullptr;

#ifdef WUWA_UI_PREVIEW
std::wstring PreviewArgument() {
    int count = 0;
    LPWSTR* values = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!values) return {};
    std::wstring result;
    for (int index = 1; index + 1 < count; ++index) {
        if (_wcsicmp(values[index], L"--ui-preview") == 0) {
            result = values[index + 1];
            break;
        }
    }
    LocalFree(values);
    return result;
}

void ApplyPreviewState(const std::wstring& state) {
    if (state == L"updater-waiting") g_state = {4,L"正在等待计算器退出",L"",false,false};
    else if (state == L"updater-preparing") g_state = {10,L"正在准备更新文件",L"WuwaEchoCalculator-v1.3.2-windows-x64.zip",false,false};
    else if (state == L"updater-checking") g_state = {22,L"正在检查更新文件",L"",false,false};
    else if (state == L"updater-backup") g_state = {26,L"正在备份当前版本",L"",false,false};
    else if (state == L"updater-replacing") g_state = {64,L"正在替换程序文件",L"models\\rec.onnx",false,false};
    else if (state == L"updater-cleaning") g_state = {93,L"正在清理临时文件",L"",false,false};
    else if (state == L"updater-restarting") g_state = {98,L"正在重新启动",L"",false,false};
    else if (state == L"updater-complete") g_state = {100,L"更新完成",L"新版本已启动",false,true};
    else if (state == L"updater-error" || state == L"updater-error-dialog")
        g_state = {64,L"更新失败",L"替换程序文件失败：鸣潮声骸计算器.exe",true,false};
}
#endif

std::wstring Quote(const std::wstring& value) {
    if (value.find_first_of(L" \t\"") == std::wstring::npos) return value;
    std::wstring output = L"\"";
    std::size_t slashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'\"') {
            output.append(slashes * 2 + 1, L'\\');
            output.push_back(L'\"');
            slashes = 0;
            continue;
        }
        output.append(slashes, L'\\'); slashes = 0; output.push_back(ch);
    }
    output.append(slashes * 2, L'\\'); output.push_back(L'\"');
    return output;
}

std::wstring ErrorText(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring result = length && buffer ? std::wstring(buffer, length) : L"Windows 错误 " + std::to_wstring(code);
    if (buffer) LocalFree(buffer);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' ')) result.pop_back();
    return result;
}

void SetProgress(int percent, std::wstring phase, std::wstring detail = {}) {
    {
        std::lock_guard lock(g_stateMutex);
        g_state.percent = std::clamp(percent, 0, 100);
        g_state.phase = std::move(phase);
        g_state.detail = std::move(detail);
    }
    if (g_window) PostMessageW(g_window, kProgressMessage, 0, 0);
}

void SetFailure(std::wstring error) {
    {
        std::lock_guard lock(g_stateMutex);
        g_state.failed = true;
        g_state.phase = L"更新失败";
        g_state.detail = std::move(error);
    }
    if (g_window) PostMessageW(g_window, kFailureMessage, 0, 0);
}

bool ParseArguments(Arguments& output, std::wstring& error) {
    int count = 0;
    LPWSTR* values = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!values) { error = L"无法读取更新参数"; return false; }
    for (int index = 1; index + 1 < count; index += 2) {
        const std::wstring key = values[index];
        const std::wstring value = values[index + 1];
        if (key == L"--parent") output.parentPid = static_cast<DWORD>(_wtoi(value.c_str()));
        else if (key == L"--target") output.target = value;
        else if (key == L"--package") output.package = value;
        else if (key == L"--restart") output.restart = value;
        else if (key == L"--version") output.version = value;
    }
    LocalFree(values);
    if (output.parentPid == 0 || output.target.empty() || output.package.empty() || output.restart.empty()) {
        error = L"更新参数不完整"; return false;
    }
    if (!std::filesystem::exists(output.package)) { error = L"更新包不存在"; return false; }
    return true;
}

bool RunHiddenProcess(const std::wstring& application, std::wstring command, DWORD& exitCode, std::wstring& error) {
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> mutableCommand(command.begin(), command.end()); mutableCommand.push_back(L'\0');
    if (!CreateProcessW(application.empty() ? nullptr : application.c_str(), mutableCommand.data(), nullptr, nullptr,
            FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        error = L"启动解压程序失败：" + ErrorText(GetLastError()); return false;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return true;
}

bool ExtractPackage(const std::filesystem::path& package, const std::filesystem::path& destination, std::wstring& error) {
    std::error_code ec;
    std::filesystem::remove_all(destination, ec);
    std::filesystem::create_directories(destination, ec);
    if (ec) { error = L"无法创建更新解压目录"; return false; }
    DWORD exitCode = 1;
    std::wstring tarCommand = L"tar.exe -xf " + Quote(package.wstring()) + L" -C " + Quote(destination.wstring());
    if (RunHiddenProcess(L"", tarCommand, exitCode, error) && exitCode == 0) return true;
    const std::wstring script = L"Expand-Archive -LiteralPath " + Quote(package.wstring()) + L" -DestinationPath " + Quote(destination.wstring()) + L" -Force";
    std::wstring powershellCommand = L"powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command " + Quote(script);
    if (!RunHiddenProcess(L"", powershellCommand, exitCode, error)) return false;
    if (exitCode != 0) { error = L"解压更新包失败，退出代码 " + std::to_wstring(exitCode); return false; }
    return true;
}

std::filesystem::path FindPackageRoot(const std::filesystem::path& extracted, std::wstring& error) {
    std::filesystem::path found;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(extracted, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        if (_wcsicmp(it->path().filename().c_str(), L"鸣潮声骸计算器.exe") != 0) continue;
        if (!found.empty()) { error = L"更新包包含多个主程序"; return {}; }
        found = it->path().parent_path();
    }
    if (ec) { error = L"读取更新包目录失败"; return {}; }
    if (found.empty()) { error = L"更新包中缺少鸣潮声骸计算器.exe"; }
    return found;
}

bool IsPreservedPath(const std::filesystem::path& relative) {
    if (relative.empty()) return false;
    const auto first = relative.begin()->wstring();
    return _wcsicmp(first.c_str(), L"data") == 0 || _wcsicmp(first.c_str(), L"updates") == 0;
}

struct CopiedFile {
    std::filesystem::path relative;
    bool hadOriginal = false;
};

bool RestoreBackup(const std::filesystem::path& target, const std::filesystem::path& backup,
                   const std::vector<CopiedFile>& copied) {
    std::error_code ec;
    bool ok = true;
    for (auto it = copied.rbegin(); it != copied.rend(); ++it) {
        const auto destination = target / it->relative;
        if (it->hadOriginal) {
            std::filesystem::create_directories(destination.parent_path(), ec); ec.clear();
            std::filesystem::copy_file(backup / it->relative, destination, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) ok = false;
        } else {
            std::filesystem::remove(destination, ec); ec.clear();
        }
    }
    return ok;
}

bool ReplaceFiles(const std::filesystem::path& source, const std::filesystem::path& target,
                  const std::filesystem::path& backup, std::wstring& error) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(source, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        const auto relative = std::filesystem::relative(it->path(), source, ec);
        if (ec) break;
        if (!IsPreservedPath(relative)) files.push_back(relative);
    }
    if (ec) { error = L"读取更新文件失败"; return false; }
    if (files.empty()) { error = L"更新包中没有可替换文件"; return false; }
    std::filesystem::remove_all(backup, ec); ec.clear();
    std::filesystem::create_directories(backup, ec);
    if (ec) { error = L"无法创建更新备份目录"; return false; }

    std::vector<CopiedFile> copied;
    for (std::size_t index = 0; index < files.size(); ++index) {
        const auto& relative = files[index];
        const auto from = source / relative;
        const auto to = target / relative;
        const bool existed = std::filesystem::exists(to);
        if (existed) {
            const auto backupFile = backup / relative;
            std::filesystem::create_directories(backupFile.parent_path(), ec); ec.clear();
            std::filesystem::copy_file(to, backupFile, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) { error = L"备份当前文件失败：" + relative.wstring(); RestoreBackup(target, backup, copied); return false; }
        }
        std::filesystem::create_directories(to.parent_path(), ec); ec.clear();
        std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            error = L"替换程序文件失败：" + relative.wstring();
            copied.push_back({relative, existed});
            RestoreBackup(target, backup, copied);
            return false;
        }
        copied.push_back({relative, existed});
        const int percent = 28 + static_cast<int>((index + 1) * 62 / files.size());
        SetProgress(percent, L"正在替换程序文件", relative.wstring());
    }
    return true;
}

void WaitForParent(DWORD parentPid) {
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
    if (!process) return;
    WaitForSingleObject(process, 60000);
    CloseHandle(process);
}

void Worker(Arguments arguments) {
    SetProgress(4, L"正在等待计算器退出");
    WaitForParent(arguments.parentPid);
    const auto updateRoot = arguments.target / L"updates";
    const auto extraction = updateRoot / (L"install-temp-" + arguments.version);
    const auto backup = updateRoot / (L"backup-" + arguments.version);
    std::wstring error;
    SetProgress(10, L"正在准备更新文件", arguments.package.filename().wstring());
    if (!ExtractPackage(arguments.package, extraction, error)) { SetFailure(error); return; }
    SetProgress(22, L"正在检查更新文件");
    const auto packageRoot = FindPackageRoot(extraction, error);
    if (packageRoot.empty()) { SetFailure(error); return; }
    SetProgress(26, L"正在备份当前版本");
    if (!ReplaceFiles(packageRoot, arguments.target, backup, error)) { SetFailure(error); return; }
    SetProgress(93, L"正在清理临时文件");
    std::error_code ec;
    std::filesystem::remove_all(extraction, ec);
    SetProgress(98, L"正在重新启动");
    HINSTANCE launched = ShellExecuteW(nullptr, L"open", arguments.restart.c_str(), nullptr, arguments.target.c_str(), SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(launched) <= 32) { SetFailure(L"更新完成，但重新启动失败"); return; }
    {
        std::lock_guard lock(g_stateMutex);
        g_state.percent = 100; g_state.phase = L"更新完成"; g_state.detail = L"新版本已启动"; g_state.complete = true;
    }
    PostMessageW(g_window, kCompleteMessage, 0, 0);
}

void DrawTextLine(HDC dc, HFONT font, COLORREF color, const std::wstring& text, RECT rect, UINT format) {
    HGDIOBJ old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, format);
    SelectObject(dc, old);
}

void PaintWindow(HWND hwnd) {
    PAINTSTRUCT paint{}; HDC dc = BeginPaint(hwnd, &paint);
    RECT client{}; GetClientRect(hwnd, &client);
    HBRUSH background = CreateSolidBrush(RGB(32,32,32)); FillRect(dc, &client, background); DeleteObject(background);
    ProgressState state; { std::lock_guard lock(g_stateMutex); state = g_state; }
    RECT title{24,22,client.right-24,54};
    DrawTextLine(dc,g_titleFont,RGB(255,255,255),L"鸣潮声骸计算器更新",title,DT_LEFT|DT_SINGLELINE|DT_VCENTER);
    RECT phase{24,72,client.right-24,98};
    DrawTextLine(dc,g_bodyFont,state.failed?RGB(255,153,164):RGB(255,255,255),state.phase,phase,DT_LEFT|DT_SINGLELINE|DT_VCENTER);
    RECT track{24,112,client.right-24,122};
    HBRUSH trackBrush=CreateSolidBrush(RGB(55,55,55));FillRect(dc,&track,trackBrush);DeleteObject(trackBrush);
    RECT fill=track;fill.right=fill.left+(fill.right-fill.left)*state.percent/100;
    HBRUSH fillBrush=CreateSolidBrush(state.failed?RGB(196,43,28):RGB(76,194,255));FillRect(dc,&fill,fillBrush);DeleteObject(fillBrush);
    RECT percent{client.right-90,76,client.right-24,102};
    DrawTextLine(dc,g_bodyFont,RGB(150,150,150),std::to_wstring(state.percent)+L"%",percent,DT_RIGHT|DT_SINGLELINE|DT_VCENTER);
    RECT detail{24,138,client.right-24,190};
    DrawTextLine(dc,g_smallFont,RGB(150,150,150),state.detail,detail,DT_LEFT|DT_WORDBREAK|DT_END_ELLIPSIS);
    EndPaint(hwnd,&paint);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: PaintWindow(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case kProgressMessage: InvalidateRect(hwnd,nullptr,FALSE); return 0;
    case kCompleteMessage:
        InvalidateRect(hwnd,nullptr,FALSE);
        SetTimer(hwnd,1,900,nullptr);
        return 0;
    case kFailureMessage:
        InvalidateRect(hwnd,nullptr,FALSE);
        MessageBoxW(hwnd,g_state.detail.c_str(),L"鸣潮声骸计算器更新失败",MB_OK|MB_ICONERROR);
        return 0;
    case WM_TIMER: KillTimer(hwnd,1); DestroyWindow(hwnd); return 0;
    case WM_CLOSE: {
        ProgressState state; { std::lock_guard lock(g_stateMutex); state=g_state; }
        if (!state.failed && !state.complete) return 0;
        DestroyWindow(hwnd); return 0;
    }
    case WM_DESTROY:
        if(g_titleFont)DeleteObject(g_titleFont);if(g_bodyFont)DeleteObject(g_bodyFont);if(g_smallFont)DeleteObject(g_smallFont);
        PostQuitMessage(0);return 0;
    default:return DefWindowProcW(hwnd,message,wParam,lParam);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#ifdef WUWA_UI_PREVIEW
    const std::wstring previewState = PreviewArgument();
    const bool preview = !previewState.empty();
    Arguments arguments; std::wstring error;
    if (preview) ApplyPreviewState(previewState);
    else if(!ParseArguments(arguments,error)){MessageBoxW(nullptr,error.c_str(),L"鸣潮声骸计算器更新失败",MB_OK|MB_ICONERROR);return 1;}
#else
    Arguments arguments; std::wstring error;
    if(!ParseArguments(arguments,error)){MessageBoxW(nullptr,error.c_str(),L"鸣潮声骸计算器更新失败",MB_OK|MB_ICONERROR);return 1;}
#endif
    WNDCLASSEXW windowClass{sizeof(windowClass)};windowClass.hInstance=instance;windowClass.lpfnWndProc=WindowProc;
    windowClass.lpszClassName=kClassName;windowClass.hCursor=LoadCursorW(nullptr,IDC_ARROW);windowClass.hbrBackground=nullptr;
    windowClass.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));windowClass.hIconSm=windowClass.hIcon;
    if(!RegisterClassExW(&windowClass))return 1;
    const int width=520,height=240,x=(GetSystemMetrics(SM_CXSCREEN)-width)/2,y=(GetSystemMetrics(SM_CYSCREEN)-height)/2;
    g_window=CreateWindowExW(0,kClassName,L"鸣潮声骸计算器更新",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,
        x,y,width,height,nullptr,nullptr,instance,nullptr);
    if(!g_window)return 1;
    BOOL dark=TRUE;DwmSetWindowAttribute(g_window,20,&dark,sizeof(dark));
    g_titleFont=CreateFontW(-22,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    g_bodyFont=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    g_smallFont=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    ShowWindow(g_window,showCommand==SW_HIDE?SW_SHOWNORMAL:showCommand);UpdateWindow(g_window);
#ifdef WUWA_UI_PREVIEW
    if (previewState == L"updater-error-dialog")
        MessageBoxW(g_window,g_state.detail.c_str(),L"鸣潮声骸计算器更新失败",MB_OK|MB_ICONERROR);
    if (!preview) { std::thread worker(Worker,arguments);worker.detach(); }
#else
    std::thread worker(Worker,arguments);worker.detach();
#endif
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}return 0;
}
