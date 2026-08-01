#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>

namespace std {
inline int max(int left, LONG right) noexcept {
    return left > right ? left : static_cast<int>(right);
}
}

#undef DrawText
#define DrawTextW DrawText
#include "app_generated.cpp"
