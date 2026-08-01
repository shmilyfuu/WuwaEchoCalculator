#include <windows.h>

namespace std {
inline int max(int left, LONG right) noexcept {
    return left > right ? left : static_cast<int>(right);
}
}

#include "app.cpp"
