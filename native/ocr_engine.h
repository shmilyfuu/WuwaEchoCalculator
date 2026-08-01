#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct NativeOcrLine {
    std::wstring text;
    float confidence = 0.0f;
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct NativeOcrJobResult {
    bool cancelled = false;
    std::wstring error;
    std::vector<NativeOcrLine> lines;
};

class NativeOcrEngine {
public:
    NativeOcrEngine();
    ~NativeOcrEngine();
    NativeOcrEngine(const NativeOcrEngine&) = delete;
    NativeOcrEngine& operator=(const NativeOcrEngine&) = delete;

    bool Initialize(const std::wstring& modelDirectory, std::wstring& error);
    bool IsReady() const;

    NativeOcrJobResult Recognize(const std::vector<std::uint8_t>& bgra,
                                 int width,
                                 int height,
                                 int stride,
                                 std::atomic_bool& cancelFlag);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
