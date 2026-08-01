from pathlib import Path

SOURCE = Path('native/update_manager.cpp')
TARGET = Path('native/update_manager_fixed.cpp')
text = SOURCE.read_text(encoding='utf-8')


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected one match, found {count}')
    text = text.replace(old, new, 1)


replace_once('#include <chrono>\n', '#include <chrono>\n#include <cwctype>\n', 'cwctype include')
replace_once(
    '''    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    std::array<wchar_t, 256> host{};
    std::array<wchar_t, 4096> path{};
    components.lpszHostName = host.data(); components.dwHostNameLength = static_cast<DWORD>(host.size());
    components.lpszUrlPath = path.data(); components.dwUrlPathLength = static_cast<DWORD>(path.size());
    components.lpszExtraInfo = path.data();
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components)) {
        error = L"解析更新地址失败：" + ErrorText(GetLastError());
        return false;
    }
    const std::wstring hostName(components.lpszHostName, components.dwHostNameLength);
    std::wstring requestPath(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength && components.lpszExtraInfo) requestPath.append(components.lpszExtraInfo, components.dwExtraInfoLength);''',
    '''    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components)) {
        error = L"解析更新地址失败：" + ErrorText(GetLastError());
        return false;
    }
    if (!components.lpszHostName || !components.dwHostNameLength) {
        error = L"更新地址缺少服务器名称";
        return false;
    }
    const std::wstring hostName(components.lpszHostName, components.dwHostNameLength);
    std::wstring requestPath = components.lpszUrlPath && components.dwUrlPathLength
        ? std::wstring(components.lpszUrlPath, components.dwUrlPathLength) : L"/";
    if (components.dwExtraInfoLength && components.lpszExtraInfo)
        requestPath.append(components.lpszExtraInfo, components.dwExtraInfoLength);''',
    'URL component parsing',
)

TARGET.write_text(text, encoding='utf-8')
print(f'Generated {TARGET} ({len(text)} chars)')
