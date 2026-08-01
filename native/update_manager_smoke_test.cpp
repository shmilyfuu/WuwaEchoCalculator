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
    std::wcout << L"native updater version comparison passed\n";
    return 0;
}
