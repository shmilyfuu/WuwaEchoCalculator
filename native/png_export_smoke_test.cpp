#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <filesystem>
#include <iostream>

using Microsoft::WRL::ComPtr;

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 64;
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 65;

    int exitCode = 1;
    {
        ComPtr<IWICImagingFactory> wic;
        ComPtr<ID2D1Factory> d2d;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(wic.GetAddressOf()));
        if (SUCCEEDED(hr)) hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf());

        ComPtr<IWICBitmap> bitmap;
        if (SUCCEEDED(hr)) hr = wic->CreateBitmap(576, 749, GUID_WICPixelFormat32bppPBGRA,
                                                  WICBitmapCacheOnLoad, bitmap.GetAddressOf());
        ComPtr<ID2D1RenderTarget> target;
        if (SUCCEEDED(hr)) {
            auto props = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                96.0f, 96.0f);
            hr = d2d->CreateWicBitmapRenderTarget(bitmap.Get(), props, target.GetAddressOf());
        }
        if (SUCCEEDED(hr)) {
            target->BeginDraw();
            target->Clear(D2D1::ColorF(0x202020));
            ComPtr<ID2D1SolidColorBrush> brush;
            hr = target->CreateSolidColorBrush(D2D1::ColorF(0x4cc2ff), brush.GetAddressOf());
            if (SUCCEEDED(hr)) target->FillRectangle(D2D1::RectF(40, 40, 536, 709), brush.Get());
            if (SUCCEEDED(hr)) hr = target->EndDraw();
        }
        target.Reset();

        if (SUCCEEDED(hr)) DeleteFileW(argv[1]);
        ComPtr<IWICStream> stream;
        if (SUCCEEDED(hr)) hr = wic->CreateStream(stream.GetAddressOf());
        if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(argv[1], GENERIC_WRITE);
        ComPtr<IWICBitmapEncoder> encoder;
        if (SUCCEEDED(hr)) hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf());
        if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> bag;
        if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(frame.GetAddressOf(), bag.GetAddressOf());
        if (SUCCEEDED(hr)) hr = frame->Initialize(bag.Get());
        if (SUCCEEDED(hr)) hr = frame->SetSize(576, 749);
        if (SUCCEEDED(hr)) hr = frame->SetResolution(96.0, 96.0);

        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
        ComPtr<IWICBitmapSource> encoderSource;
        if (SUCCEEDED(hr)) hr = WICConvertBitmapSource(format, bitmap.Get(), encoderSource.GetAddressOf());
        if (SUCCEEDED(hr)) hr = frame->WriteSource(encoderSource.Get(), nullptr);
        if (SUCCEEDED(hr)) hr = frame->Commit();
        if (SUCCEEDED(hr)) hr = encoder->Commit();

        frame.Reset(); encoderSource.Reset(); encoder.Reset(); stream.Reset(); bitmap.Reset();
        if (SUCCEEDED(hr) && std::filesystem::exists(argv[1]) &&
            std::filesystem::file_size(argv[1]) > 1024) {
            std::wcout << L"png_size=" << std::filesystem::file_size(argv[1]) << L"\n";
            exitCode = 0;
        } else {
            std::wcerr << L"png_export_failed hr=0x" << std::hex
                       << static_cast<unsigned long>(hr) << L"\n";
            exitCode = 66;
        }
    }

    CoUninitialize();
    return exitCode;
}
