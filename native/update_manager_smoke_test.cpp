#define UNICODE
#define _UNICODE
#include "update_manager.h"

#include <iostream>

int wmain() {
    struct Case { const wchar_t* left; const wchar_t* right; int expected; };
    const Case cases[] = {
        {L"1.3.0", L"1.3.1", -1},
        {L"v1.3.0", L"1.3.0", 0},
        {L"1.4.0", L"1.3.9", 1},
        {L"1.3.0-beta.1", L"1.3.0", -1},
        {L"2.0.0", L"1.99.99", 1},
    };
    for (const auto& item : cases) {
        const int actual = NativeUpdateManager::CompareVersions(item.left, item.right);
        const int normalized = actual < 0 ? -1 : actual > 0 ? 1 : 0;
        if (normalized != item.expected) {
            std::wcerr << L"version comparison failed: " << item.left << L" / " << item.right << L"\n";
            return 1;
        }
    }

    struct UrlCase {
        const wchar_t* url;
        const wchar_t* host;
        const wchar_t* path;
        std::uint16_t port;
        bool secure;
    };
    const UrlCase urls[] = {
        {L"https://gitee.com/api/v5/repos/shmilyfuu/WuwaEchoCalculator/releases/latest",
         L"gitee.com", L"/api/v5/repos/shmilyfuu/WuwaEchoCalculator/releases/latest", 443, true},
        {L"https://api.github.com/repos/shmilyfuu/WuwaEchoCalculator/releases/latest",
         L"api.github.com", L"/repos/shmilyfuu/WuwaEchoCalculator/releases/latest", 443, true},
        {L"http://example.com:8080/download?channel=test",
         L"example.com", L"/download?channel=test", 8080, false},
    };
    for (const auto& item : urls) {
        NativeHttpUrlParts parts;
        std::wstring error;
        if (!ParseNativeHttpUrl(item.url, parts, error)) {
            std::wcerr << L"URL parsing failed: " << item.url << L" / " << error << L"\n";
            return 1;
        }
        if (parts.host != item.host || parts.path != item.path || parts.port != item.port || parts.secure != item.secure) {
            std::wcerr << L"URL parsing mismatch: " << item.url << L"\n";
            return 1;
        }
    }
    NativeHttpUrlParts invalidParts;
    std::wstring invalidError;
    if (ParseNativeHttpUrl(L"/relative/update/path", invalidParts, invalidError) || invalidError.empty()) {
        std::wcerr << L"relative URL should be rejected\n";
        return 1;
    }

    std::wcout << L"native updater version and URL parsing passed\n";
    return 0;
}
