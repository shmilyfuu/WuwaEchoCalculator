#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "ocr_engine.h"

using Microsoft::WRL::ComPtr;

static std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc != 3) {
        std::cerr << "usage: ocr_smoke_test <models-dir> <image>\n";
        return 64;
    }
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 65;

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) { CoUninitialize(); return 66; }
    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(argv[2], nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
    if (FAILED(hr)) { CoUninitialize(); return 67; }
    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(hr)) { CoUninitialize(); return 68; }
    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr)) { CoUninitialize(); return 69; }
    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) { CoUninitialize(); return 70; }

    UINT width = 0, height = 0;
    converter->GetSize(&width, &height);
    const UINT stride = width * 4;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(stride) * height);
    hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data());
    if (FAILED(hr)) { CoUninitialize(); return 71; }

    NativeOcrEngine engine;
    std::wstring error;
    if (!engine.Initialize(argv[1], error)) {
        std::cerr << WideToUtf8(error) << "\n";
        CoUninitialize();
        return 72;
    }
    std::atomic_bool cancelled{false};
    const auto result = engine.Recognize(pixels, static_cast<int>(width), static_cast<int>(height), static_cast<int>(stride), cancelled);
    if (!result.error.empty()) std::cerr << WideToUtf8(result.error) << "\n";
    std::cout << "recognized_lines=" << result.lines.size() << "\n";
    for (const auto& line : result.lines) {
        std::cout << WideToUtf8(line.text) << "\t" << line.confidence << "\n";
    }
    CoUninitialize();
    return result.lines.size() >= 3 ? 0 : 73;
}
