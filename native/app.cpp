#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wincodec.h>
#include <commdlg.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <string>
#include <algorithm>
#include <numeric>
#include <filesystem>
#include <cwchar>
#include <utility>
#include <chrono>
#include "icon_assets.h"
#include "ocr_engine.h"
#include "ocr_parser.h"
#include "update_manager.h"
#include <atomic>
#include <thread>
#include <memory>
#include <cwctype>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "dwmapi.lib")

using Microsoft::WRL::ComPtr;

namespace {
WNDPROC g_originalEditProc = nullptr;
LRESULT CALLBACK ExportEditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    const LRESULT result = CallWindowProcW(g_originalEditProc, hwnd, message, wParam, lParam);
    if (message == WM_PAINT && GetWindowTextLengthW(hwnd) == 0) {
        HDC dc = GetDC(hwnd);
        if (dc) {
            HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
            HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(150, 150, 150));
            RECT rc{}; GetClientRect(hwnd, &rc);
            TEXTMETRICW tm{}; GetTextMetricsW(dc, &tm);
            const wchar_t placeholder[] = L"请输入标题（可选）";
            TextOutW(dc, 0, (rc.bottom - tm.tmHeight) / 2, placeholder, static_cast<int>(std::size(placeholder) - 1));
            if (old) SelectObject(dc, old);
            ReleaseDC(hwnd, dc);
        }
    }
    return result;
}
constexpr wchar_t kWindowClass[] = L"WuwaEchoCalculatorNativeWindow";
constexpr wchar_t kWindowTitle[] = L"鸣潮声骸计算器";
constexpr wchar_t kAppVersion[] = L"1.3.2";
constexpr float kClientWidth = 1188.0f;
constexpr float kClientHeight = 772.0f;
constexpr DWORD kWindowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
constexpr UINT kEditId = 7001;
constexpr UINT kOcrCompleteMessage = WM_APP + 101;
constexpr UINT kUpdateCompleteMessage = WM_APP + 102;

D2D1_COLOR_F Hex(UINT rgb, float alpha = 1.0f) {
    return D2D1::ColorF(((rgb >> 16) & 0xff) / 255.0f, ((rgb >> 8) & 0xff) / 255.0f,
                        (rgb & 0xff) / 255.0f, alpha);
}

bool Contains(const D2D1_RECT_F& r, float x, float y) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

D2D1_RECT_F Rect(float x, float y, float w, float h) {
    return D2D1::RectF(x, y, x + w, y + h);
}

struct AttributeRule {
    std::wstring name;
    std::vector<std::wstring> values;
    std::vector<int> scores;
};

std::vector<AttributeRule> BuildRules() {
    return {
        {L"生命百分比", {L"6.4%",L"7.1%",L"7.9%",L"8.6%",L"9.4%",L"10.1%",L"10.9%",L"11.6%"}, {10,5,5,2,2,1,1,1}},
        {L"生命", {L"320",L"360",L"390",L"430",L"470",L"510",L"540",L"580"}, {10,5,5,2,2,1,1,1}},
        {L"防御百分比", {L"8.1%",L"9.0%",L"10.0%",L"10.9%",L"11.8%",L"12.8%",L"13.8%",L"14.7%"}, {10,5,5,2,2,1,1,1}},
        {L"防御", {L"40",L"50",L"60",L"70"}, {8,5,3,1}},
        {L"暴击", {L"6.3%",L"6.9%",L"7.5%",L"8.1%",L"8.7%",L"9.3%",L"9.9%",L"10.5%"}, {-1,-1,-1,-2,-2,-5,-5,-10}},
        {L"暴击伤害", {L"12.6%",L"13.8%",L"15.0%",L"16.2%",L"17.4%",L"18.6%",L"19.8%",L"21.0%"}, {-1,-1,-1,-2,-2,-5,-5,-10}},
        {L"攻击百分比", {L"6.4%",L"7.1%",L"7.9%",L"8.6%",L"9.4%",L"10.1%",L"10.9%",L"11.6%"}, {-1,-1,-1,-2,-2,-5,-5,-10}},
        {L"攻击", {L"30",L"40",L"50",L"60"}, {-1,-3,-5,-8}},
        {L"共鸣效率", {L"6.8%",L"7.6%",L"8.4%",L"9.2%",L"10.0%",L"10.8%",L"11.6%",L"12.4%"}, {0,0,0,0,0,0,0,0}},
        {L"普攻伤害加成", {L"6.4%",L"7.1%",L"7.9%",L"8.6%",L"9.4%",L"10.1%",L"10.9%",L"11.6%"}, {0,0,0,0,0,0,0,0}},
        {L"重击伤害加成", {L"6.4%",L"7.1%",L"7.9%",L"8.6%",L"9.4%",L"10.1%",L"10.9%",L"11.6%"}, {0,0,0,0,0,0,0,0}},
        {L"共鸣技能伤害加成", {L"6.4%",L"7.1%",L"7.9%",L"8.6%",L"9.4%",L"10.1%",L"10.9%",L"11.6%"}, {0,0,0,0,0,0,0,0}},
        {L"共鸣解放伤害加成", {L"6.4%",L"7.1%",L"7.9%",L"8.6%",L"9.4%",L"10.1%",L"10.9%",L"11.6%"}, {0,0,0,0,0,0,0,0}},
    };
}

struct RowSelection { int attribute = -1; int value = -1; };
struct SlotRecord { bool used = false; std::array<RowSelection, 5> rows{}; int subtotal = 0; };

enum class ButtonKind { Gray, Blue, Red };
enum class StatusLevel { Normal, Attention, Error };
enum class ControlId {
    None, DropZone, Stop, Again, Record, Clear, Export, Settings, Update, ModalAccept, ModalCancel,
    CardEdit0, CardEdit1, CardEdit2, CardEdit3, CardEdit4,
    CardDelete0, CardDelete1, CardDelete2, CardDelete3, CardDelete4
};
enum class DropdownKind { None, Attribute, Value, Slot };
enum class ModalKind { None, Confirm, Export, UpdateAvailable, UpdateProgress, UpdateReady, UpdateResult };
enum class ConfirmAction { None, ClearAll, DeleteSlot, OverwriteSlot };

struct DropdownState {
    DropdownKind kind = DropdownKind::None;
    int row = -1;
    int scroll = 0;
    D2D1_RECT_F anchor{};
};

class App {
public:
    ~App() { ocrCancel_.store(true); if(ocrThread_.joinable()) ocrThread_.join(); if (editFont_) DeleteObject(editFont_); if (editBrush_) DeleteObject(editBrush_); }

    HRESULT Initialize(HWND hwnd) {
        hwnd_ = hwnd;
        rules_ = BuildRules();
        dpi_ = static_cast<float>(GetDpiForWindow(hwnd_));
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.GetAddressOf());
        if (FAILED(hr)) return hr;
        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf()));
        if (FAILED(hr)) return hr;
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(wicFactory_.GetAddressOf()));
        if (FAILED(hr)) return hr;
        CreateTextFormats();
        CreateEditControl();
        DragAcceptFiles(hwnd_, TRUE);
#ifdef WUWA_UI_PREVIEW
        if (InitializeUiPreview()) return S_OK;
#endif
        wchar_t modulePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr,modulePath,MAX_PATH);
        std::wstring ocrError;
        const auto modelDirectory=(std::filesystem::path(modulePath).parent_path()/L"models").wstring();
        ocrReady_=ocr_.Initialize(modelDirectory,ocrError);
        statusLevel_=ocrReady_?StatusLevel::Normal:StatusLevel::Error;
        status_=ocrReady_?L"PP-OCRv5 本地模型已就绪":ocrError;
        std::wstring updateError;
        updateManager_.Initialize(std::filesystem::path(modulePath).parent_path().wstring(),kAppVersion,
            [this](const NativeUpdateSnapshot& snapshot){
                auto* copy=new NativeUpdateSnapshot(snapshot);
                if(!PostMessageW(hwnd_,kUpdateCompleteMessage,0,reinterpret_cast<LPARAM>(copy))) delete copy;
            },updateError);
        if(!updateError.empty()){statusLevel_=StatusLevel::Error;status_=updateError;}
        updateReady_=updateManager_.HasPreparedUpdate();
        if(updateReady_){
            updateState_.phase=NativeUpdatePhase::Ready;
            updateState_.latest=updateManager_.LatestInfo();
            updateState_.latest.version=updateManager_.PreparedVersion();
            updateState_.message=L"检测到已下载的更新包";
            modal_=ModalKind::UpdateReady;
        }
        updateManager_.Check(false);
        return S_OK;
    }

    void DiscardDeviceResources() {
        imageD2D_.Reset();
        iconArrow_.Reset(); iconStar_.Reset(); iconNormal_.Reset(); iconAbnormal_.Reset(); iconManual_.Reset();
        brush_.Reset();
        renderTarget_.Reset();
    }

    HRESULT EnsureDeviceResources() {
        if (renderTarget_) return S_OK;
        RECT rc{}; GetClientRect(hwnd_, &rc);
        const auto size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
        HRESULT hr = d2dFactory_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpi_, dpi_),
            D2D1::HwndRenderTargetProperties(hwnd_, size), renderTarget_.GetAddressOf());
        if (FAILED(hr)) return hr;
        hr = renderTarget_->CreateSolidColorBrush(Hex(0xffffff), brush_.GetAddressOf());
        if (FAILED(hr)) return hr;
        RecreateD2DBitmap();
        LoadUiIcons();
        return S_OK;
    }

    void Paint() {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd_, &ps);
        if (SUCCEEDED(EnsureDeviceResources())) {
            renderTarget_->BeginDraw();
            renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
            renderTarget_->Clear(Hex(0x202020));
            DrawInterface(renderTarget_.Get(), brush_.Get());
            const HRESULT hr = renderTarget_->EndDraw();
            if (hr == D2DERR_RECREATE_TARGET) DiscardDeviceResources();
        }
        EndPaint(hwnd_, &ps);
    }

    void Resize(UINT width, UINT height) {
        if (renderTarget_) renderTarget_->Resize(D2D1::SizeU(width, height));
        PositionEditControl();
    }

    void DpiChanged(UINT dpi) {
        dpi_ = static_cast<float>(dpi);
        DiscardDeviceResources();
        UpdateEditFont();
        PositionEditControl();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void MouseMove(float x, float y) {
        lastMouseX_ = x; lastMouseY_ = y;
        ControlId next = HitTestButton(x, y);
        if (next != hovered_) { hovered_ = next; InvalidateRect(hwnd_, nullptr, FALSE); }
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0}; TrackMouseEvent(&tme);
    }

    void MouseLeave() {
        hovered_ = ControlId::None;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void MouseDown(float x, float y) {
        lastMouseX_ = x; lastMouseY_ = y;
        if (dropdown_.kind != DropdownKind::None) {
            if (HandleDropdownClick(x, y)) return;
            dropdown_ = {};
            InvalidateRect(hwnd_, nullptr, FALSE);
        }

        if (modal_ != ModalKind::None) {
            const ControlId id = HitTestButton(x, y);
            if (id == ControlId::ModalAccept || id == ControlId::ModalCancel) {
                pressed_ = id; SetCapture(hwnd_); InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return;
        }

        if (Contains(Rect(1094, 20, 74, 20), x, y)) {
            topmost_ = !topmost_;
            SetWindowPos(hwnd_, topmost_ ? HWND_TOPMOST : HWND_NOTOPMOST, 0,0,0,0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }

        for (int row = 0; row < 5; ++row) {
            const float y0 = 152.0f + row * 50.0f;
            const auto attr = Rect(416, y0 + 8, 168, 30);
            const auto value = Rect(592, y0 + 8, 96, 30);
            if (Contains(attr, x, y)) { OpenDropdown(DropdownKind::Attribute, row, attr); return; }
            if (Contains(value, x, y) && rows_[row].attribute >= 0) { OpenDropdown(DropdownKind::Value, row, value); return; }
        }
        const auto slotRect = Rect(534, 410, 168, 30);
        if (Contains(slotRect, x, y)) { OpenDropdown(DropdownKind::Slot, -1, slotRect); return; }

        for (int i = 0; i < 5; ++i) {
            const auto card = Rect(20.0f + i * 232.0f, 500, 220, 212);
            if (!slots_[i].used && Contains(card, x, y)) {
                selectedSlot_ = i; statusLevel_=StatusLevel::Normal;status_ = L"已选择声骸 " + std::to_wstring(i + 1);
                InvalidateRect(hwnd_, nullptr, FALSE); return;
            }
        }

        const ControlId id = HitTestButton(x, y);
        if (id != ControlId::None && IsControlEnabled(id)) {
            pressed_ = id;
            SetCapture(hwnd_);
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    void MouseUp(float x, float y) {
        if (pressed_ == ControlId::None) return;
        const ControlId was = pressed_;
        pressed_ = ControlId::None;
        ReleaseCapture();
        if (HitTestButton(x, y) == was && IsControlEnabled(was)) Activate(was);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void MouseWheel(float x, float y, short delta) {
        if (dropdown_.kind == DropdownKind::None) return;
        const auto popup = DropdownPopupRect();
        if (!Contains(popup, x, y)) return;
        const int count = static_cast<int>(DropdownOptions().size());
        const int visible = std::min(8, count);
        const int maxScroll = std::max(0, count - visible);
        dropdown_.scroll = std::clamp(dropdown_.scroll + (delta < 0 ? 1 : -1), 0, maxScroll);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void KeyDown(WPARAM key) {
        if (key == VK_ESCAPE) {
            if (dropdown_.kind != DropdownKind::None) { dropdown_ = {}; InvalidateRect(hwnd_, nullptr, FALSE); return; }
            if (modal_ != ModalKind::None) { CancelModal(); return; }
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && (key == 'V')) ImportClipboardBitmap();
    }

    void DropFiles(HDROP drop) {
        wchar_t path[MAX_PATH]{};
        if (DragQueryFileW(drop, 0, path, MAX_PATH)) LoadImageFile(path);
        DragFinish(drop);
    }

    HBRUSH EditColor(HDC dc) {
        SetTextColor(dc, RGB(255,255,255));
        SetBkColor(dc, RGB(32,32,32));
        SetBkMode(dc, OPAQUE);
        return editBrush_;
    }

    void EditChanged() {
        if (modal_ != ModalKind::Export) return;
        exportTitleInvalid_ = WeightedTitleLength(GetEditText()) > 12.0f;
        InvalidateRect(edit_, nullptr, TRUE);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void EditFocusChanged(bool focused) {
        exportEditFocused_ = focused;
        InvalidateRect(edit_, nullptr, TRUE);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    HWND EditHwnd() const { return edit_; }
    bool ExportModalOpen() const { return modal_ == ModalKind::Export; }
    void ExportEnter() { if (modal_ == ModalKind::Export) AcceptModal(); }
    void ExportEscape() { if (modal_ == ModalKind::Export) CloseModal(); }
    void OcrCompleted(NativeOcrJobResult* rawResult) {
        std::unique_ptr<NativeOcrJobResult> result(rawResult);
        if(ocrThread_.joinable()) ocrThread_.join();
        ocrRunning_=false;
        if(!result){statusLevel_=StatusLevel::Error;status_=L"OCR 返回结果为空";InvalidateRect(hwnd_,nullptr,FALSE);return;}
        if(result->cancelled){statusLevel_=StatusLevel::Attention;status_=L"识别已停止";InvalidateRect(hwnd_,nullptr,FALSE);return;}
        if(!result->error.empty()){statusLevel_=StatusLevel::Error;status_=result->error;InvalidateRect(hwnd_,nullptr,FALSE);return;}
        ApplyOcrLines(result->lines);
        InvalidateRect(hwnd_,nullptr,FALSE);
    }
    void UpdateCompleted(NativeUpdateSnapshot* rawSnapshot){
        std::unique_ptr<NativeUpdateSnapshot> snapshot(rawSnapshot);
        if(!snapshot)return;
        updateState_=*snapshot;
        const auto phase=snapshot->phase;
        updateBusy_=phase==NativeUpdatePhase::Checking||phase==NativeUpdatePhase::Preparing||
            phase==NativeUpdatePhase::Downloading||phase==NativeUpdatePhase::Verifying||
            phase==NativeUpdatePhase::LaunchingInstaller;
        if(phase==NativeUpdatePhase::Checking){
            statusLevel_=StatusLevel::Attention;status_=L"正在检查更新";
        }else if(phase==NativeUpdatePhase::Available){
            statusLevel_=StatusLevel::Attention;status_=L"发现新版本 v"+snapshot->latest.version;
            modal_=ModalKind::UpdateAvailable;ShowWindow(edit_,SW_HIDE);
        }else if(phase==NativeUpdatePhase::UpToDate){
            statusLevel_=StatusLevel::Normal;status_=L"当前已是最新版本";
            if(snapshot->manual){modal_=ModalKind::UpdateResult;ShowWindow(edit_,SW_HIDE);}
        }else if(phase==NativeUpdatePhase::Preparing||phase==NativeUpdatePhase::Downloading||phase==NativeUpdatePhase::Verifying){
            statusLevel_=StatusLevel::Attention;status_=snapshot->message;modal_=ModalKind::UpdateProgress;ShowWindow(edit_,SW_HIDE);
        }else if(phase==NativeUpdatePhase::Ready){
            updateReady_=true;statusLevel_=StatusLevel::Normal;status_=L"更新包已准备完成";
            modal_=ModalKind::UpdateReady;ShowWindow(edit_,SW_HIDE);
        }else if(phase==NativeUpdatePhase::Cancelled){
            statusLevel_=StatusLevel::Attention;status_=L"已取消下载更新";
            if(modal_==ModalKind::UpdateProgress)modal_=ModalKind::None;
        }else if(phase==NativeUpdatePhase::Error){
            statusLevel_=StatusLevel::Error;status_=snapshot->error.empty()?snapshot->message:snapshot->error;
            if(snapshot->manual||modal_==ModalKind::UpdateProgress||modal_==ModalKind::UpdateAvailable)
                modal_=ModalKind::UpdateResult;
        }else if(phase==NativeUpdatePhase::LaunchingInstaller){
            statusLevel_=StatusLevel::Attention;status_=L"正在启动更新助手";
        }
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

private:
#ifdef WUWA_UI_PREVIEW
    static std::wstring PreviewArgument(const wchar_t* name) {
        int count = 0;
        LPWSTR* values = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!values) return {};
        std::wstring result;
        for (int index = 1; index + 1 < count; ++index) {
            if (_wcsicmp(values[index], name) == 0) {
                result = values[index + 1];
                break;
            }
        }
        LocalFree(values);
        return result;
    }

    void LoadPreviewImage(const std::wstring& path) {
        if (path.empty()) return;
        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(wicFactory_->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()))) return;
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) return;
        ComPtr<IWICFormatConverter> converter;
        if (FAILED(wicFactory_->CreateFormatConverter(converter.GetAddressOf()))) return;
        if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return;
        if (FAILED(wicFactory_->CreateBitmapFromSource(converter.Get(), WICBitmapCacheOnLoad,
                imageWic_.GetAddressOf()))) return;
        imageWic_->GetSize(&imageW_, &imageH_);
        imageName_ = std::filesystem::path(path).filename().wstring();
    }

    void SetPreviewRows() {
        rows_ = {{{8,2},{7,1},{11,1},{5,4},{4,2}}};
        rowConfidence_ = {{1,1,2,3,1}};
    }

    void SetPreviewSlots() {
        const std::array<std::array<RowSelection,5>,5> samples{{
            {{{12,5},{7,2},{1,5},{5,4},{4,2}}},
            {{{8,2},{7,1},{11,1},{5,4},{4,2}}},
            {{{9,3},{10,7},{1,0},{5,0},{8,6}}},
            {{{1,6},{12,5},{4,3},{10,7},{3,1}}},
            {{{9,3},{10,7},{1,0},{5,0},{8,6}}}
        }};
        for (int index = 0; index < 5; ++index) {
            slots_[index].used = true;
            slots_[index].rows = samples[index];
            slots_[index].subtotal = 0;
            for (const auto& row : slots_[index].rows) slots_[index].subtotal += ScoreOf(row);
        }
    }

    bool InitializeUiPreview() {
        const std::wstring state = PreviewArgument(L"--ui-preview");
        if (state.empty()) return false;

        statusLevel_ = StatusLevel::Normal;
        status_ = L"PP-OCRv5 本地模型已就绪";
        if (state == L"main-empty") return true;

        SetPreviewRows();
        SetPreviewSlots();
        LoadPreviewImage(PreviewArgument(L"--preview-image"));

        if (state == L"main-recognized") {
            statusLevel_ = StatusLevel::Attention;
            status_ = L"已识别五条，其中 1 条建议重点核对";
        } else if (state == L"ocr-recognizing") {
            rows_ = {};
            rowConfidence_.fill(0);
            ocrRunning_ = true;
            statusLevel_ = StatusLevel::Attention;
            status_ = L"正在识别声骸属性";
        } else if (state == L"ocr-error") {
            rows_ = {};
            rowConfidence_.fill(0);
            statusLevel_ = StatusLevel::Attention;
            status_ = L"未识别到有效属性，请调整截图或手动选择";
        } else if (state == L"dropdown-attribute") {
            dropdown_ = {DropdownKind::Attribute, 0, 0, Rect(416,160,168,30)};
        } else if (state == L"dropdown-value") {
            dropdown_ = {DropdownKind::Value, 0, 0, Rect(592,160,96,30)};
        } else if (state == L"dropdown-slot") {
            dropdown_ = {DropdownKind::Slot, -1, 0, Rect(534,410,168,30)};
        } else if (state == L"confirm-clear") {
            OpenConfirm(ConfirmAction::ClearAll,L"清空记录",L"确认清空5个声骸的全部记录？",L"清空",true);
        } else if (state == L"confirm-delete") {
            pendingSlot_ = 2;
            OpenConfirm(ConfirmAction::DeleteSlot,L"删除记录",L"确认删除声骸 3 的记录？",L"删除",true);
        } else if (state == L"confirm-overwrite") {
            pendingSlot_ = 0;
            OpenConfirm(ConfirmAction::OverwriteSlot,L"覆盖记录",L"声骸 1 已有记录，确认覆盖？",L"覆盖",false);
        } else if (state == L"export-normal" || state == L"export-invalid") {
            OpenExport();
            if (state == L"export-invalid") {
                SetWindowTextW(edit_,L"这是一个超过十二个中文字符的导出标题");
                exportTitleInvalid_ = true;
            }
        } else if (state == L"update-checking") {
            updateBusy_ = true;
            updateState_.phase = NativeUpdatePhase::Checking;
            statusLevel_ = StatusLevel::Attention;
            status_ = L"正在检查更新";
        } else if (state == L"update-available") {
            updateState_.phase = NativeUpdatePhase::Available;
            updateState_.latest = {true,L"1.3.3",L"v1.3.3",L"鸣潮声骸计算器 v1.3.3",
                L"- 优化更新流程界面\n- 修复已知问题\n- 提升本地识别稳定性",L"Gitee"};
            modal_ = ModalKind::UpdateAvailable;
            statusLevel_ = StatusLevel::Attention;
            status_ = L"发现新版本 v1.3.3";
        } else if (state == L"update-preparing") {
            updateBusy_ = true;
            updateState_.phase = NativeUpdatePhase::Preparing;
            updateState_.latest.version = L"1.3.3";
            updateState_.latest.source = L"Gitee";
            updateState_.message = L"正在准备更新包";
            modal_ = ModalKind::UpdateProgress;
            statusLevel_ = StatusLevel::Attention;
            status_ = updateState_.message;
        } else if (state == L"update-downloading") {
            updateBusy_ = true;
            updateState_.phase = NativeUpdatePhase::Downloading;
            updateState_.latest.version = L"1.3.3";
            updateState_.latest.source = L"Gitee";
            updateState_.message = L"正在下载更新包";
            updateState_.downloadedBytes = 48234496;
            updateState_.totalBytes = 86507520;
            updateState_.bytesPerSecond = 3670016;
            modal_ = ModalKind::UpdateProgress;
            statusLevel_ = StatusLevel::Attention;
            status_ = updateState_.message;
        } else if (state == L"update-verifying") {
            updateBusy_ = true;
            updateState_.phase = NativeUpdatePhase::Verifying;
            updateState_.latest.version = L"1.3.3";
            updateState_.latest.source = L"Gitee";
            updateState_.message = L"正在校验更新包";
            updateState_.downloadedBytes = 86507520;
            updateState_.totalBytes = 86507520;
            modal_ = ModalKind::UpdateProgress;
            statusLevel_ = StatusLevel::Attention;
            status_ = updateState_.message;
        } else if (state == L"update-fallback") {
            updateBusy_ = true;
            updateState_.phase = NativeUpdatePhase::Preparing;
            updateState_.latest.version = L"1.3.3";
            updateState_.latest.source = L"GitHub";
            updateState_.message = L"Gitee 下载失败，正在切换到 GitHub";
            modal_ = ModalKind::UpdateProgress;
            statusLevel_ = StatusLevel::Attention;
            status_ = updateState_.message;
        } else if (state == L"update-ready") {
            updateReady_ = true;
            updateState_.phase = NativeUpdatePhase::Ready;
            updateState_.latest.version = L"1.3.3";
            modal_ = ModalKind::UpdateReady;
            statusLevel_ = StatusLevel::Normal;
            status_ = L"更新包已准备完成";
        } else if (state == L"update-ready-deferred") {
            updateReady_ = true;
            updateState_.phase = NativeUpdatePhase::Ready;
            updateState_.latest.version = L"1.3.3";
            statusLevel_ = StatusLevel::Attention;
            status_ = L"更新包已保留，可稍后安装";
        } else if (state == L"update-latest") {
            updateState_.phase = NativeUpdatePhase::UpToDate;
            modal_ = ModalKind::UpdateResult;
            statusLevel_ = StatusLevel::Normal;
            status_ = L"当前已是最新版本";
        } else if (state == L"update-error") {
            updateState_.phase = NativeUpdatePhase::Error;
            updateState_.error = L"Gitee：连接更新服务器失败；GitHub：请求更新信息超时";
            modal_ = ModalKind::UpdateResult;
            statusLevel_ = StatusLevel::Error;
            status_ = updateState_.error;
        } else if (state == L"update-cancelled") {
            updateState_.phase = NativeUpdatePhase::Cancelled;
            statusLevel_ = StatusLevel::Attention;
            status_ = L"已取消下载更新";
        }
        return true;
    }
#endif

    static std::wstring CompactOcrText(std::wstring value) {
        std::wstring output;
        for(wchar_t ch:value){
            if(iswspace(ch)||ch==L'+'||ch==L'·'||ch==L'：'||ch==L':') continue;
            if(ch==L'％') ch=L'%';
            output.push_back(ch);
        }
        return output;
    }
    static bool ContainsAny(const std::wstring& text,const std::vector<std::wstring>& aliases){
        for(const auto& alias:aliases) if(text.find(alias)!=std::wstring::npos) return true;
        return false;
    }
    int AttributeFromText(const std::wstring& raw) const {
        const auto text=CompactOcrText(raw);
        static const std::vector<std::vector<std::wstring>> aliases{
            {L"生命百分比",L"生命百份比",L"生命白分比",L"生命%"},{L"生命",L"生俞"},
            {L"防御百分比",L"防御百份比",L"防御白分比",L"防御%"},{L"防御",L"防御力"},
            {L"暴击",L"暴撃"},{L"暴击伤害",L"暴击损害",L"暴击伤書",L"暴伤"},
            {L"攻击百分比",L"攻击百份比",L"攻击白分比",L"攻击%"},{L"攻击",L"攻去"},
            {L"共鸣效率",L"共鸣效串",L"共鸣效宰"},{L"普攻伤害加成",L"普攻伤害",L"普攻加成"},
            {L"重击伤害加成",L"重击伤害",L"重击加成"},{L"共鸣技能伤害加成",L"共鸣技能伤害",L"共鸣技能加成"},
            {L"共鸣解放伤害加成",L"共鸣解放伤害",L"共鸣解放加成"}
        };
        const std::array<int,13> order{{0,2,5,6,8,9,10,11,12,4,7,3,1}};
        for(int index:order) if(ContainsAny(text,aliases[static_cast<size_t>(index)])) return index;
        return -1;
    }
    static bool ReadOcrNumber(const std::wstring& raw,double& number,bool& percent){
        const auto text=CompactOcrText(raw); percent=text.find(L'%')!=std::wstring::npos;
        std::wstring token; bool dot=false;
        for(wchar_t ch:text){
            if(iswdigit(ch)){token.push_back(ch);continue;}
            if((ch==L'.'||ch==L',')&&!dot&&!token.empty()){token.push_back(L'.');dot=true;continue;}
            if(!token.empty()&&dot) break;
        }
        if(token.empty()) return false;
        try{number=std::stod(token);return true;}catch(...){return false;}
    }
    static double RuleNumber(const std::wstring& value){
        std::wstring copy=value; copy.erase(std::remove(copy.begin(),copy.end(),L'%'),copy.end());
        try{return std::stod(copy);}catch(...){return 0.0;}
    }
    int ValueFromText(int attribute,const std::wstring& text) const {
        if(attribute<0||attribute>=static_cast<int>(rules_.size())) return -1;
        double observed=0; bool percent=false; if(!ReadOcrNumber(text,observed,percent)) return -1;
        const bool expectedPercent=rules_[attribute].values.front().find(L'%')!=std::wstring::npos;
        std::vector<double> observations{observed};
        if(expectedPercent){observations.push_back(observed/10.0);observations.push_back(observed*10.0);}
        int best=-1; double bestDistance=1e9;
        for(double candidate:observations){
            for(int i=0;i<static_cast<int>(rules_[attribute].values.size());++i){
                const double distance=std::abs(candidate-RuleNumber(rules_[attribute].values[i]));
                if(distance<bestDistance){bestDistance=distance;best=i;}
            }
        }
        const double limit=expectedPercent?1.25:12.0;
        return bestDistance<=limit?best:-1;
    }
    void ApplyOcrLines(const std::vector<NativeOcrLine>& lines){
        const auto selected=SelectMainCompatibleOcrRows(lines);
        rows_={};rowConfidence_.fill(0);
        int used=0;
        for(const auto& parsed:selected){
            if(used>=5)break;
            int attribute=-1;
            for(int index=0;index<static_cast<int>(rules_.size());++index){
                if(rules_[index].name==parsed.attribute){attribute=index;break;}
            }
            if(attribute<0)continue;
            int value=-1;
            for(int index=0;index<static_cast<int>(rules_[attribute].values.size());++index){
                if(rules_[attribute].values[index]==parsed.value){value=index;break;}
            }
            if(value<0)continue;
            rows_[used]={attribute,value};
            rowConfidence_[used]=parsed.confidence==ParsedOcrConfidence::High?1:2;
            ++used;
        }
        const int medium=static_cast<int>(std::count(rowConfidence_.begin(),rowConfidence_.end(),2));
        if(used==5&&medium==0){statusLevel_=StatusLevel::Normal;status_=L"已识别五条，请核对后写入";}
        else if(used==5){statusLevel_=StatusLevel::Attention;status_=L"已识别五条，其中 "+std::to_wstring(medium)+L" 条建议重点核对";}
        else if(used>0){statusLevel_=StatusLevel::Attention;status_=L"当前识别到 "+std::to_wstring(used)+L" 条，请通过下拉框补齐";}
        else{statusLevel_=StatusLevel::Attention;status_=L"未识别到有效属性，请调整截图或手动选择";}
    }

    bool CopyCurrentImage(std::vector<std::uint8_t>& pixels,int& width,int& height,int& stride){
        if(!imageWic_||imageW_==0||imageH_==0) return false;
        width=static_cast<int>(imageW_);height=static_cast<int>(imageH_);stride=width*4;
        pixels.resize(static_cast<size_t>(stride)*height);
        return SUCCEEDED(imageWic_->CopyPixels(nullptr,stride,static_cast<UINT>(pixels.size()),pixels.data()));
    }
    void StartOcr(){
        if(!ocrReady_){statusLevel_=StatusLevel::Error;status_=L"OCR 模型尚未就绪";InvalidateRect(hwnd_,nullptr,FALSE);return;}
        if(ocrRunning_){ocrCancel_.store(true);if(ocrThread_.joinable())ocrThread_.join();ocrRunning_=false;}
        std::vector<std::uint8_t> pixels;int width=0,height=0,stride=0;
        if(!CopyCurrentImage(pixels,width,height,stride)){statusLevel_=StatusLevel::Error;status_=L"无法读取当前图片像素";InvalidateRect(hwnd_,nullptr,FALSE);return;}
        ocrCancel_.store(false);ocrRunning_=true;rows_={};rowConfidence_.fill(0);statusLevel_=StatusLevel::Attention;status_=L"正在识别声骸属性";InvalidateRect(hwnd_,nullptr,FALSE);
        ocrThread_=std::thread([this,pixels=std::move(pixels),width,height,stride]() mutable {
            auto* result=new NativeOcrJobResult(ocr_.Recognize(pixels,width,height,stride,ocrCancel_));
            PostMessageW(hwnd_,kOcrCompleteMessage,0,reinterpret_cast<LPARAM>(result));
        });
    }
    void StopOcr(){if(!ocrRunning_)return;ocrCancel_.store(true);statusLevel_=StatusLevel::Attention;status_=L"正在停止识别";InvalidateRect(hwnd_,nullptr,FALSE);}

    static std::wstring FormatTransferSize(std::uint64_t bytes){
        wchar_t buffer[64]{};
        if(bytes>=1024ull*1024ull*1024ull)swprintf_s(buffer,L"%.2f GB",bytes/(1024.0*1024.0*1024.0));
        else if(bytes>=1024ull*1024ull)swprintf_s(buffer,L"%.1f MB",bytes/(1024.0*1024.0));
        else if(bytes>=1024ull)swprintf_s(buffer,L"%.1f KB",bytes/1024.0);
        else swprintf_s(buffer,L"%llu B",static_cast<unsigned long long>(bytes));
        return buffer;
    }
    std::wstring UpdateButtonLabel() const {
        if(updateReady_)return L"更新";
        if(updateState_.phase==NativeUpdatePhase::Downloading||updateState_.phase==NativeUpdatePhase::Verifying||updateState_.phase==NativeUpdatePhase::Preparing)return L"下载中";
        if(updateBusy_)return L"检查中";
        return L"检查更新";
    }
    D2D1_RECT_F CenteredModalBox(float width,float height) const {
        return Rect((kClientWidth-width)/2.0f,(kClientHeight-height)/2.0f,width,height);
    }
    D2D1_RECT_F UpdateModalBox() const {
        if(modal_==ModalKind::UpdateAvailable)return CenteredModalBox(460,356);
        if(modal_==ModalKind::UpdateProgress)return CenteredModalBox(460,343);
        if(modal_==ModalKind::UpdateReady)return CenteredModalBox(460,321);
        const bool failed=updateState_.phase==NativeUpdatePhase::Error;
        return CenteredModalBox(460,failed?226.0f:186.0f);
    }
    float UpdateFrameOneHeight() const {
        if(modal_==ModalKind::UpdateAvailable)return 278;
        if(modal_==ModalKind::UpdateProgress)return 265;
        if(modal_==ModalKind::UpdateReady)return 243;
        return updateState_.phase==NativeUpdatePhase::Error?148.0f:108.0f;
    }
    D2D1_RECT_F UpdatePrimaryRect() const {
        const auto box=UpdateModalBox();
        const float y=box.top+UpdateFrameOneHeight()+24;
        const float right=box.right-24;
        if(modal_==ModalKind::UpdateAvailable||modal_==ModalKind::UpdateReady)return Rect(right-200,y,96,30);
        return Rect(right-96,y,96,30);
    }
    D2D1_RECT_F UpdateSecondaryRect() const {
        const auto box=UpdateModalBox();
        return Rect(box.right-120,box.top+UpdateFrameOneHeight()+24,96,30);
    }

    void CreateTextFormats() {
        CreateFormat(20, DWRITE_FONT_WEIGHT_SEMI_BOLD, title_.GetAddressOf());
        CreateFormat(16, DWRITE_FONT_WEIGHT_SEMI_BOLD, version_.GetAddressOf());
        CreateFormat(14, DWRITE_FONT_WEIGHT_NORMAL, body14_.GetAddressOf());
        CreateFormat(12, DWRITE_FONT_WEIGHT_NORMAL, body12_.GetAddressOf());
        CreateFormat(12, DWRITE_FONT_WEIGHT_SEMI_BOLD, body12Bold_.GetAddressOf());
        CreateFormat(11, DWRITE_FONT_WEIGHT_NORMAL, body11_.GetAddressOf());
        CreateFormat(10, DWRITE_FONT_WEIGHT_NORMAL, body10_.GetAddressOf());
        CreateFormat(8, DWRITE_FONT_WEIGHT_NORMAL, body8_.GetAddressOf());
        CreateFormat(20, DWRITE_FONT_WEIGHT_SEMI_BOLD, stat20_.GetAddressOf());
        CreateFormat(24, DWRITE_FONT_WEIGHT_SEMI_BOLD, exportStat24_.GetAddressOf());
        CreateFormat(12, DWRITE_FONT_WEIGHT_NORMAL, updateBody_.GetAddressOf());
        updateBody_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    }

    void CreateFormat(float size, DWRITE_FONT_WEIGHT weight, IDWriteTextFormat** out) {
        dwriteFactory_->CreateTextFormat(L"Microsoft YaHei UI", nullptr, weight,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", out);
        (*out)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    void CreateEditControl() {
        editBrush_ = CreateSolidBrush(RGB(32,32,32));
        edit_ = CreateWindowExW(WS_EX_NOPARENTNOTIFY, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL | ES_NOHIDESEL,
                                0,0,0,0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditId)),
                                GetModuleHandleW(nullptr), nullptr);
        SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0,0));
        SendMessageW(edit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"请输入标题（可选）"));
        g_originalEditProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(edit_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ExportEditProc)));
        UpdateEditFont();
    }

    void UpdateEditFont() {
        if (editFont_) DeleteObject(editFont_);
        const int height = -MulDiv(14, static_cast<int>(dpi_), 96);
        editFont_ = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
        if (edit_) SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(editFont_), TRUE);
    }

    void PositionEditControl() {
        if (!edit_ || modal_ != ModalKind::Export) return;
        const auto r = ExportInputRect();
        const auto px = [this](float v) { return static_cast<int>(v * dpi_ / 96.0f + 0.5f); };
        SetWindowPos(edit_, HWND_TOP, px(r.left + 12), px(r.top + 6), px(r.right-r.left-33), px(20.0f),
                     SWP_SHOWWINDOW | SWP_NOACTIVATE);
        RedrawWindow(edit_,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);
    }

    void DrawInterface(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {
        DrawTopBar(rt,b);
        DrawImport(rt,b);
        DrawReview(rt,b);
        DrawSide(rt,b);
        DrawRecords(rt,b);
        DrawStatus(rt,b);
        if (dropdown_.kind != DropdownKind::None) DrawDropdown(rt,b);
        if (modal_ != ModalKind::None) DrawModal(rt,b);
    }

    void Fill(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const D2D1_RECT_F& r, D2D1_COLOR_F c) {
        b->SetColor(c); rt->FillRectangle(r,b);
    }
    void FillRound(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const D2D1_RECT_F& r, float radius, D2D1_COLOR_F c) {
        b->SetColor(c); rt->FillRoundedRectangle(D2D1::RoundedRect(r,radius,radius),b);
    }
    void StrokeRound(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const D2D1_RECT_F& r, float radius, D2D1_COLOR_F c, float width=1) {
        b->SetColor(c); rt->DrawRoundedRectangle(D2D1::RoundedRect(r,radius,radius),b,width);
    }
    void DrawDialogFrame(ID2D1RenderTarget* rt,ID2D1SolidColorBrush* b,const D2D1_RECT_F& box,float frameOneHeight){
        FillRound(rt,b,box,7,Hex(0x202020));
        const auto frameOne=Rect(box.left,box.top,box.right-box.left,frameOneHeight);
        FillRound(rt,b,frameOne,7,Hex(0x2d2d2d));
        Fill(rt,b,Rect(frameOne.left,frameOne.top+7,frameOne.right-frameOne.left,frameOneHeight-7),Hex(0x2d2d2d));
    }
    void DrawInputUnderline(ID2D1RenderTarget* rt,ID2D1SolidColorBrush* b,const D2D1_RECT_F& input,D2D1_COLOR_F color){
        ComPtr<ID2D1PathGeometry> path;
        if(FAILED(d2dFactory_->CreatePathGeometry(path.GetAddressOf())))return;
        ComPtr<ID2D1GeometrySink> sink;
        if(FAILED(path->Open(sink.GetAddressOf())))return;
        constexpr float radius=4.0f;
        const float bottom=input.bottom-.5f;
        sink->BeginFigure(D2D1::Point2F(input.left+.5f,bottom-radius),D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(input.left+radius,bottom),D2D1::SizeF(radius,radius),0,
            D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,D2D1_ARC_SIZE_SMALL));
        sink->AddLine(D2D1::Point2F(input.right-radius,bottom));
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(input.right-.5f,bottom-radius),D2D1::SizeF(radius,radius),0,
            D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,D2D1_ARC_SIZE_SMALL));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        if(SUCCEEDED(sink->Close())){b->SetColor(color);rt->DrawGeometry(path.Get(),b,1.0f);}
    }
    void Text(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const std::wstring& text,
              const D2D1_RECT_F& r, IDWriteTextFormat* fmt, D2D1_COLOR_F c,
              DWRITE_TEXT_ALIGNMENT align=DWRITE_TEXT_ALIGNMENT_LEADING,
              DWRITE_PARAGRAPH_ALIGNMENT valign=DWRITE_PARAGRAPH_ALIGNMENT_CENTER) {
        fmt->SetTextAlignment(align); fmt->SetParagraphAlignment(valign); b->SetColor(c);
        rt->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), fmt, r, b,
                      D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
    }

    void DrawTopBar(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {
        Text(rt,b,L"鸣潮声骸计算器",Rect(20,20,140,28),title_.Get(),Hex(0xffffff));
        Text(rt,b,L"v"+std::wstring(kAppVersion),Rect(165,26,60,22),version_.Get(),Hex(0xffffff));
        Text(rt,b,L"鸣潮声骸的本地识别评分工具",Rect(20,48,220,20),body14_.Get(),Hex(0xffffff,.60f));
        DrawButton(rt,b,ControlId::Settings,Rect(882,15,96,30),L"设置",ButtonKind::Gray,false);
        DrawButton(rt,b,ControlId::Update,Rect(986,15,96,30),UpdateButtonLabel(),updateReady_?ButtonKind::Blue:ButtonKind::Gray,!updateBusy_);
        Text(rt,b,L"置顶",Rect(1094,20,28,20),body14_.Get(),Hex(0xffffff));
        const auto track = Rect(1130,21,38,18);
        FillRound(rt,b,track,9, topmost_ ? Hex(0x4cc2ff) : Hex(0x000000,.10f));
        StrokeRound(rt,b,track,9,Hex(0xffffff,.06f));
        const float knobX = topmost_ ? 1153.0f : 1133.0f;
        b->SetColor(topmost_ ? Hex(0x000000) : Hex(0xffffff,.60f));
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX+6,30),6,6),b);
    }

    void DrawHeading(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, float x, float y, int step, const wchar_t* label) {
        FillRound(rt,b,Rect(x,y,20,20),3,Hex(0x4cc2ff));
        Text(rt,b,std::to_wstring(step),Rect(x,y,20,20),body12Bold_.Get(),Hex(0x000000),DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(rt,b,label,Rect(x+25,y,180,20),body14_.Get(),Hex(0xffffff));
    }

    void DrawImport(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {
        DrawHeading(rt,b,20,88,1,L"导入声骸截图");
        const auto drop = Rect(20,120,320,46);
        FillRound(rt,b,drop,3, hovered_==ControlId::DropZone ? Hex(0x4cc2ff,.10f) : Hex(0xffffff,.06f));
        b->SetColor(hovered_==ControlId::DropZone ? Hex(0x4cc2ff) : Hex(0xffffff,.20f));
        rt->DrawRoundedRectangle(D2D1::RoundedRect(drop,3,3),b,1);
        b->SetColor(Hex(0x4cc2ff));
        rt->FillRectangle(Rect(87,142,12,2),b); rt->FillRectangle(Rect(92,137,2,12),b);
        Text(rt,b,L"点击选择、拖入，或按 Ctrl+V 粘贴",Rect(105,120,215,46),body12_.Get(),Hex(0xffffff,.60f));

        const auto preview = Rect(20,178,320,220);
        FillRound(rt,b,preview,3,Hex(0x000000,.40f));
        if (imageD2D_) {
            const auto size = imageD2D_->GetSize();
            const float scale = std::min({1.0f, 320.0f/size.width, 220.0f/size.height});
            const float w=size.width*scale,h=size.height*scale;
            const auto dest=Rect(20+(320-w)/2,178+(220-h)/2,w,h);
            rt->DrawBitmap(imageD2D_.Get(),dest,1.0f,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
        DrawButton(rt,b,ControlId::Stop,Rect(20,410,156,30),L"停止识别",ButtonKind::Red,ocrRunning_);
        DrawButton(rt,b,ControlId::Again,Rect(184,410,156,30),L"重新识别",ButtonKind::Gray,imageW_>0);
    }

    void DrawReview(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {
        DrawHeading(rt,b,370,88,2,L"核对属性");
        const std::array<std::pair<const wchar_t*,D2D1_RECT_F>,5> heads{{
            {L"序号",Rect(382,120,22,24)}, {L"属性",Rect(416,120,168,24)},
            {L"档位",Rect(592,120,96,24)}, {L"分数",Rect(696,120,60,24)},
            {L"状态",Rect(764,120,29,24)}}};
        for (const auto& h:heads) Text(rt,b,h.first,h.second,body12_.Get(),Hex(0xffffff,.60f),DWRITE_TEXT_ALIGNMENT_CENTER);

        for (int i=0;i<5;++i) {
            const float y=152.0f+i*50.0f;
            const int score=ScoreOf(rows_[i]);
            const D2D1_COLOR_F rowColor = score>0 ? Hex(0x6ccb5f,.15f) : score<0 ? Hex(0xff99a4,.15f) : Hex(0xffffff,.03f);
            FillRound(rt,b,Rect(370,y,436,46),3,rowColor);
            Text(rt,b,std::to_wstring(i+1),Rect(382,y+12,22,22),body14_.Get(),Hex(0xffffff),DWRITE_TEXT_ALIGNMENT_CENTER);
            DrawSelect(rt,b,Rect(416,y+8,168,30), rows_[i].attribute>=0?rules_[rows_[i].attribute].name:L"请选择属性", true);
            std::wstring val=L"请选择档位";
            if (rows_[i].attribute>=0 && rows_[i].value>=0) val=rules_[rows_[i].attribute].values[rows_[i].value];
            DrawSelect(rt,b,Rect(592,y+8,96,30),val,rows_[i].attribute>=0);
            const std::wstring scoreText = rows_[i].attribute>=0 && rows_[i].value>=0 ? FormatScore(score) : L"—";
            const auto pill=Rect(696,y+8,60,30);
            FillRound(rt,b,pill,3,score>0?Hex(0x6ccb5f,.15f):score<0?Hex(0xff99a4,.15f):Hex(0xffffff,.06f));
            Text(rt,b,scoreText,pill,body14_.Get(),score>0?Hex(0x6ccb5f):score<0?Hex(0xff99a4):Hex(0xffffff,.60f),DWRITE_TEXT_ALIGNMENT_CENTER);
            if(rows_[i].attribute>=0){
                if(rowConfidence_[i]==1) DrawIcon(rt,iconNormal_.Get(),Rect(772,y+16,14,14));
                else if(rowConfidence_[i]==2) DrawIcon(rt,iconAbnormal_.Get(),Rect(772,y+16,14,14));
                else DrawIcon(rt,iconManual_.Get(),Rect(769,y+17,20,12));
            } else {
                b->SetColor(Hex(0xffffff,.25f));
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(779,y+23),4,4),b);
            }
        }
        Text(rt,b,L"本件记分",Rect(370,410,48,30),body12_.Get(),Hex(0xffffff));
        Text(rt,b,std::to_wstring(CurrentSubtotal()),Rect(422,410,30,30),stat20_.Get(),ScoreColor(CurrentSubtotal()),DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(rt,b,L"记录到",Rect(490,410,36,30),body12_.Get(),Hex(0xffffff));
        DrawSelect(rt,b,Rect(534,410,168,30),L"声骸 "+std::to_wstring(selectedSlot_+1)+(slots_[selectedSlot_].used?L"（已记录）":L""),true);
        DrawButton(rt,b,ControlId::Record,Rect(710,410,96,30),L"记录",ButtonKind::Blue,true);
    }

    void DrawSelect(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const D2D1_RECT_F& r, const std::wstring& value, bool enabled) {
        FillRound(rt,b,r,3,Hex(0xffffff,.06f));
        Text(rt,b,value,Rect(r.left+10,r.top,r.right-r.left-34,r.bottom-r.top),body14_.Get(),enabled?Hex(0xffffff):Hex(0xffffff,.35f));
        DrawIcon(rt,iconArrow_.Get(),Rect(r.right-20,(r.top+r.bottom)/2-5,10,10),enabled?1.0f:.35f);
    }

    void DrawSide(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {
        DrawHeading(rt,b,836,88,4,L"当前统计");
        FillRound(rt,b,Rect(836,120,332,96),3,Hex(0xffffff,.03f));
        const int count=RecordCount(), total=TotalScore();
        DrawMetric(rt,b,858,133,L"已记录",std::to_wstring(count)+L" / 5",L"",Hex(0xffffff));
        DrawMetric(rt,b,962,133,L"总分",std::to_wstring(total),L"5件声骸总分",Hex(0x6ccb5f));
        wchar_t avg[32]{}; swprintf_s(avg,L"%.1f",total/5.0);
        DrawMetric(rt,b,1066,133,L"平均分",avg,L"5件声骸平均分",Hex(0x6ccb5f));
        DrawButton(rt,b,ControlId::Clear,Rect(968,228,96,30),L"清空全部",ButtonKind::Red,count>0);
        DrawButton(rt,b,ControlId::Export,Rect(1072,228,96,30),L"导出记录",ButtonKind::Blue,count==5);

        FillRound(rt,b,Rect(836,288,332,152),4,Hex(0xffffff,.03f));
        Text(rt,b,L"操作说明",Rect(848,300,120,20),body14_.Get(),Hex(0xffffff));
        const std::array<const wchar_t*,4> lines{{L"1. 粘贴、拖入或点击上传声骸截图",L"2. 识别后核对属性与数值",L"3. 通过下拉框调整属性与档位",L"4. 选择记录位置后写入记录卡"}};
        for(int i=0;i<4;++i) Text(rt,b,lines[i],Rect(850,328+i*18,300,18),body12_.Get(),Hex(0xffffff,.60f));
        DrawIcon(rt,iconNormal_.Get(),Rect(848,413,14,14));
        Text(rt,b,L"识别准确",Rect(867,409,65,22),body10_.Get(),Hex(0xffffff,.60f));
        DrawIcon(rt,iconAbnormal_.Get(),Rect(945,413,14,14));
        Text(rt,b,L"建议核对",Rect(964,409,65,22),body10_.Get(),Hex(0xffffff,.60f));
        DrawIcon(rt,iconManual_.Get(),Rect(1043,414,20,12));
        Text(rt,b,L"手动选择",Rect(1068,409,72,22),body10_.Get(),Hex(0xffffff,.60f));
    }

    void DrawMetric(ID2D1RenderTarget* rt,ID2D1SolidColorBrush* b,float x,float y,const std::wstring& label,const std::wstring& value,const std::wstring& caption,D2D1_COLOR_F valueColor){
        Text(rt,b,label,Rect(x,y,80,17),body12_.Get(),Hex(0xffffff),DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(rt,b,value,Rect(x,y+25,80,28),stat20_.Get(),valueColor,DWRITE_TEXT_ALIGNMENT_CENTER);
        if(!caption.empty())Text(rt,b,caption,Rect(x,y+59,80,11),body8_.Get(),Hex(0xffffff,.35f),DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    void DrawRecords(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {
        DrawHeading(rt,b,20,472,3,L"声骸记录卡");
        for(int i=0;i<5;++i){
            const float x=20.0f+i*232.0f,y=500.0f;
            FillRound(rt,b,Rect(x,y,220,212),3,Hex(0xffffff,.03f));
            if(!slots_[i].used){
                Text(rt,b,L"声骸",Rect(x+12,y+12,24,17),body12Bold_.Get(),Hex(0xffffff));
                Text(rt,b,std::to_wstring(i+1),Rect(x+41,y+12,12,17),body12Bold_.Get(),Hex(0xffffff));
                Text(rt,b,selectedSlot_==i?L"当前记录位置":L"尚未记录",Rect(x,y+90,220,34),body12Bold_.Get(),Hex(0xffffff,.60f),DWRITE_TEXT_ALIGNMENT_CENTER);
                continue;
            }
            Text(rt,b,L"声骸",Rect(x+12,y+12,24,17),body12Bold_.Get(),Hex(0xffffff));
            Text(rt,b,std::to_wstring(i+1),Rect(x+41,y+12,12,17),body12Bold_.Get(),Hex(0xffffff));
            Text(rt,b,L"小计",Rect(x+161,y+12,24,17),body12Bold_.Get(),Hex(0xffffff),DWRITE_TEXT_ALIGNMENT_TRAILING);
            Text(rt,b,FormatScore(slots_[i].subtotal),Rect(x+190,y+12,18,17),body12Bold_.Get(),ScoreColor(slots_[i].subtotal),DWRITE_TEXT_ALIGNMENT_TRAILING);
            for(int r=0;r<5;++r){
                const float ry=y+37+r*27;
                FillRound(rt,b,Rect(x+12,ry,196,23),1,Hex(0xffffff,.06f));
                DrawIcon(rt,iconStar_.Get(),Rect(x+18,ry+6,11,11));
                const auto& sel=slots_[i].rows[r];
                const auto& rule=rules_[sel.attribute];
                Text(rt,b,ShortName(rule.name),Rect(x+33,ry,96,23),body11_.Get(),Hex(0xffffff));
                Text(rt,b,rule.values[sel.value],Rect(x+128,ry,48,23),body12_.Get(),Hex(0xffffff),DWRITE_TEXT_ALIGNMENT_TRAILING);
                Text(rt,b,FormatScore(rule.scores[sel.value]),Rect(x+178,ry,22,23),body12_.Get(),ScoreColor(rule.scores[sel.value]),DWRITE_TEXT_ALIGNMENT_TRAILING);
            }
            DrawButton(rt,b,static_cast<ControlId>(static_cast<int>(ControlId::CardEdit0)+i),Rect(x+12,y+176,94,24),L"编辑",ButtonKind::Gray,true,12);
            DrawButton(rt,b,static_cast<ControlId>(static_cast<int>(ControlId::CardDelete0)+i),Rect(x+114,y+176,94,24),L"删除",ButtonKind::Red,true,12);
        }
    }

    void DrawStatus(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {
        const UINT indicator=statusLevel_==StatusLevel::Normal?0x6ccb5f:statusLevel_==StatusLevel::Attention?0xfce100:0xff99a4;
        b->SetColor(Hex(indicator));
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(24,742),4,4),b);
        Text(rt,b,status_,Rect(34,732,900,20),body14_.Get(),Hex(0xffffff,.60f));
        Text(rt,b,L"v"+std::wstring(kAppVersion)+L" · Direct2D",Rect(1000,732,168,20),body11_.Get(),Hex(0xffffff,.60f),DWRITE_TEXT_ALIGNMENT_TRAILING);
    }

    void DrawButton(ID2D1RenderTarget* rt,ID2D1SolidColorBrush* b,ControlId id,const D2D1_RECT_F& original,const std::wstring& label,ButtonKind kind,bool enabled,float fontSize=14){
        const bool hot=hovered_==id, down=pressed_==id;
        D2D1_RECT_F r=original;
        if(down){r.top+=1;r.bottom+=1;}
        D2D1_COLOR_F fillColor;
        D2D1_COLOR_F textColor=Hex(0xffffff);
        if(!enabled){fillColor=Hex(0xffffff,.04f);textColor=Hex(0xffffff,.35f);}
        else if(kind==ButtonKind::Blue){fillColor=down?Hex(0x0078d4):hot?Hex(0xafe6ff):Hex(0x4cc2ff);textColor=Hex(0x000000);}
        else if(kind==ButtonKind::Red){fillColor=Hex(0xc42b1c,down?.90f:hot?.70f:.50f);}
        else {fillColor=Hex(0xffffff,down?.04f:hot?.08f:.06f);}
        FillRound(rt,b,r,4,fillColor);
        IDWriteTextFormat* format=fontSize<=12?body12_.Get():body14_.Get();
        Text(rt,b,label,r,format,textColor,DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    void DrawDropdown(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {
        const auto popup=DropdownPopupRect();
        FillRound(rt,b,popup,4,Hex(0x292929));
        StrokeRound(rt,b,popup,4,Hex(0xffffff,.12f));
        const auto options=DropdownOptions();
        const int visible=std::min(8,static_cast<int>(options.size()));
        for(int i=0;i<visible;++i){
            const int index=i+dropdown_.scroll;
            const auto item=Rect(popup.left,popup.top+i*28,popup.right-popup.left,28);
            if(Contains(item,lastMouseX_,lastMouseY_)) FillRound(rt,b,Rect(item.left+2,item.top+2,item.right-item.left-4,24),3,Hex(0xffffff,.08f));
            Text(rt,b,options[index],Rect(item.left+10,item.top,item.right-item.left-20,28),body14_.Get(),Hex(0xffffff));
        }
    }

    void DrawModal(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {
        Fill(rt,b,Rect(0,0,kClientWidth,kClientHeight),Hex(0x000000,.55f));
        if(modal_==ModalKind::UpdateAvailable||modal_==ModalKind::UpdateProgress||
           modal_==ModalKind::UpdateReady||modal_==ModalKind::UpdateResult){
            DrawUpdateModal(rt,b);return;
        }
        if(modal_==ModalKind::Confirm){
            const auto box=CenteredModalBox(384,186);
            DrawDialogFrame(rt,b,box,108);
            Text(rt,b,confirmTitle_,Rect(box.left+24,box.top+24,336,28),title_.Get(),Hex(0xffffff));
            Text(rt,b,confirmMessage_,Rect(box.left+24,box.top+64,336,20),body14_.Get(),Hex(0xffffff));
            DrawButton(rt,b,ControlId::ModalAccept,Rect(box.right-224,box.top+132,96,30),confirmAcceptText_,ButtonKind::Blue,true);
            DrawButton(rt,b,ControlId::ModalCancel,Rect(box.right-120,box.top+132,96,30),L"取消",ButtonKind::Gray,true);
        } else {
            const auto box=CenteredModalBox(384,198);
            DrawDialogFrame(rt,b,box,120);
            Text(rt,b,L"导出记录",Rect(box.left+24,box.top+24,336,28),title_.Get(),Hex(0xffffff));
            const auto input=ExportInputRect();
            const auto inputBase=Rect(input.left,input.top+1,input.right-input.left,30);
            FillRound(rt,b,inputBase,4,Hex(0x000000,.30f));
            StrokeRound(rt,b,inputBase,4,Hex(0xffffff,.10f));
            if(exportEditFocused_)DrawInputUnderline(rt,b,input,exportTitleInvalid_?Hex(0xfce100):Hex(0x4cc2ff));
            if(exportTitleInvalid_){
                ComPtr<ID2D1PathGeometry> arrow;
                if(SUCCEEDED(d2dFactory_->CreatePathGeometry(arrow.GetAddressOf()))){
                    ComPtr<ID2D1GeometrySink> sink;
                    if(SUCCEEDED(arrow->Open(sink.GetAddressOf()))){
                        sink->BeginFigure(D2D1::Point2F(446,384),D2D1_FIGURE_BEGIN_FILLED);
                        sink->AddLine(D2D1::Point2F(451,379));
                        sink->AddLine(D2D1::Point2F(456,384));
                        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                        if(SUCCEEDED(sink->Close())){
                            b->SetColor(Hex(0x454545));
                            rt->FillGeometry(arrow.Get(),b);
                        }
                    }
                }
                const auto hint=Rect(438,384,257,20);
                FillRound(rt,b,hint,3,Hex(0x454545));
                Text(rt,b,L"最多可输入12个字符，超过最大长度限制，请重新编辑",Rect(446,384,241,20),body10_.Get(),Hex(0xffffff,.60f));
            }
            DrawButton(rt,b,ControlId::ModalAccept,Rect(box.right-224,box.top+144,96,30),L"确定",ButtonKind::Blue,true);
            DrawButton(rt,b,ControlId::ModalCancel,Rect(box.right-120,box.top+144,96,30),L"取消",ButtonKind::Gray,true);
        }
    }

    void DrawUpdateModal(ID2D1RenderTarget* rt,ID2D1SolidColorBrush* b){
        const auto box=UpdateModalBox();
        const float x=box.left+24;
        const float contentTop=box.top+64;
        DrawDialogFrame(rt,b,box,UpdateFrameOneHeight());
        if(modal_==ModalKind::UpdateAvailable){
            Text(rt,b,L"发现新版本",Rect(x,box.top+24,412,28),title_.Get(),Hex(0xffffff));
            Text(rt,b,L"v"+std::wstring(kAppVersion)+L"  →  v"+updateState_.latest.version,Rect(x,contentTop,412,22),version_.Get(),Hex(0x4cc2ff));
            Text(rt,b,L"下载来源："+(updateState_.latest.source.empty()?L"Gitee / GitHub":updateState_.latest.source),Rect(x,contentTop+30,412,20),body12_.Get(),Hex(0xffffff,.60f));
            Text(rt,b,L"更新内容",Rect(x,contentTop+62,412,20),body12Bold_.Get(),Hex(0xffffff));
            const std::wstring notes=updateState_.latest.notes.empty()?L"该版本未提供更新说明。":updateState_.latest.notes;
            Text(rt,b,notes,Rect(x,contentTop+88,412,102),updateBody_.Get(),Hex(0xffffff,.70f),DWRITE_TEXT_ALIGNMENT_LEADING,DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            DrawButton(rt,b,ControlId::ModalAccept,UpdatePrimaryRect(),L"下载更新",ButtonKind::Blue,true);
            DrawButton(rt,b,ControlId::ModalCancel,UpdateSecondaryRect(),L"稍后",ButtonKind::Gray,true);
        }else if(modal_==ModalKind::UpdateProgress){
            Text(rt,b,L"正在下载更新",Rect(x,box.top+24,412,28),title_.Get(),Hex(0xffffff));
            const std::wstring version=updateState_.latest.version.empty()?L"":L"v"+std::wstring(kAppVersion)+L"  →  v"+updateState_.latest.version;
            Text(rt,b,version,Rect(x,contentTop,412,22),version_.Get(),Hex(0x4cc2ff));
            Text(rt,b,updateState_.message.empty()?L"正在准备更新包":updateState_.message,Rect(x,contentTop+41,412,20),body14_.Get(),Hex(0xffffff));
            const auto track=Rect(x,contentTop+79,412,8);FillRound(rt,b,track,4,Hex(0xffffff,.08f));
            float progress=0.0f;
            if(updateState_.totalBytes>0)progress=std::clamp(static_cast<float>(updateState_.downloadedBytes)/static_cast<float>(updateState_.totalBytes),0.0f,1.0f);
            else if(updateState_.phase==NativeUpdatePhase::Verifying)progress=1.0f;
            if(progress>0)FillRound(rt,b,Rect(x,contentTop+79,412*progress,8),4,Hex(0x4cc2ff));
            const int percent=updateState_.totalBytes>0?static_cast<int>(progress*100.0f):(updateState_.phase==NativeUpdatePhase::Verifying?100:0);
            Text(rt,b,std::to_wstring(percent)+L"%",Rect(x,contentTop+97,412,20),body12Bold_.Get(),Hex(0xffffff),DWRITE_TEXT_ALIGNMENT_TRAILING);
            const std::wstring transferred=FormatTransferSize(updateState_.downloadedBytes)+L" / "+(updateState_.totalBytes?FormatTransferSize(updateState_.totalBytes):L"未知大小");
            Text(rt,b,transferred,Rect(x,contentTop+125,250,20),body12_.Get(),Hex(0xffffff,.60f));
            Text(rt,b,updateState_.bytesPerSecond?FormatTransferSize(updateState_.bytesPerSecond)+L"/s":L"",Rect(x+262,contentTop+125,150,20),body12_.Get(),Hex(0xffffff,.60f),DWRITE_TEXT_ALIGNMENT_TRAILING);
            Text(rt,b,L"来源："+(updateState_.latest.source.empty()?L"Gitee / GitHub":updateState_.latest.source),Rect(x,contentTop+157,412,20),body12_.Get(),Hex(0xffffff,.60f));
            DrawButton(rt,b,ControlId::ModalCancel,UpdateSecondaryRect(),L"取消下载",ButtonKind::Gray,updateState_.phase==NativeUpdatePhase::Downloading||updateState_.phase==NativeUpdatePhase::Preparing);
        }else if(modal_==ModalKind::UpdateReady){
            Text(rt,b,L"更新已准备完成",Rect(x,box.top+24,412,28),title_.Get(),Hex(0xffffff));
            const std::wstring version=updateState_.latest.version.empty()?updateManager_.PreparedVersion():updateState_.latest.version;
            Text(rt,b,L"新版本 v"+version+L" 已下载，可以立即安装。",Rect(x,contentTop,412,24),body14_.Get(),Hex(0xffffff));
            Text(rt,b,L"立即更新会关闭计算器，替换程序文件并自动重新启动。声骸记录和设置文件会保留。",Rect(x,contentTop+44,412,70),updateBody_.Get(),Hex(0xffffff,.70f),DWRITE_TEXT_ALIGNMENT_LEADING,DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            Text(rt,b,L"选择稍后后，顶部按钮会变为蓝色“更新”。",Rect(x,contentTop+135,412,20),body12_.Get(),Hex(0xffffff,.60f));
            DrawButton(rt,b,ControlId::ModalAccept,UpdatePrimaryRect(),L"立即更新",ButtonKind::Blue,true);
            DrawButton(rt,b,ControlId::ModalCancel,UpdateSecondaryRect(),L"稍后",ButtonKind::Gray,true);
        }else{
            const bool failed=updateState_.phase==NativeUpdatePhase::Error;
            Text(rt,b,failed?L"更新操作失败":L"检查更新",Rect(x,box.top+24,412,28),title_.Get(),Hex(0xffffff));
            Text(rt,b,failed?(updateState_.error.empty()?updateState_.message:updateState_.error):L"当前已是最新版本 v"+std::wstring(kAppVersion),Rect(x,contentTop,412,failed?60.0f:20.0f),updateBody_.Get(),failed?Hex(0xff99a4):Hex(0xffffff,.75f),DWRITE_TEXT_ALIGNMENT_LEADING,DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            DrawButton(rt,b,ControlId::ModalAccept,UpdatePrimaryRect(),L"确定",ButtonKind::Blue,true);
        }
    }

    D2D1_RECT_F ExportInputRect() const { return Rect(426,351,336,32); }

    D2D1_COLOR_F ScoreColor(int score) const { return score>0?Hex(0x6ccb5f):score<0?Hex(0xff99a4):Hex(0xffffff,.60f); }
    std::wstring FormatScore(int score) const { return (score>0?L"+":L"")+std::to_wstring(score); }
    std::wstring ShortName(const std::wstring& n) const {
        if(n==L"生命百分比")return L"生命"; if(n==L"攻击百分比")return L"攻击"; if(n==L"防御百分比")return L"防御"; return n;
    }

    int ScoreOf(const RowSelection& row) const {
        if(row.attribute<0||row.attribute>=static_cast<int>(rules_.size()))return 0;
        const auto& rule=rules_[row.attribute];
        if(row.value<0||row.value>=static_cast<int>(rule.scores.size()))return 0;
        return rule.scores[row.value];
    }
    int CurrentSubtotal() const { int s=0;for(const auto&r:rows_)s+=ScoreOf(r);return s; }
    int RecordCount() const { return static_cast<int>(std::count_if(slots_.begin(),slots_.end(),[](const auto&s){return s.used;})); }
    int TotalScore() const { int s=0;for(const auto&slot:slots_)if(slot.used)s+=slot.subtotal;return s; }
    bool RowsValid() const {
        std::array<bool,32> seen{};
        for(const auto&r:rows_){if(r.attribute<0||r.value<0)return false;if(seen[r.attribute])return false;seen[r.attribute]=true;}
        return true;
    }

    bool IsControlEnabled(ControlId id) const {
        if(id==ControlId::Stop)return ocrRunning_;
        if(id==ControlId::Again)return imageW_>0&&!ocrRunning_;
        if(id==ControlId::Clear)return RecordCount()>0;
        if(id==ControlId::Export)return RecordCount()==5;
        if(id==ControlId::Settings)return false;
        if(id==ControlId::Update)return !updateBusy_;
        return true;
    }

    ControlId HitTestButton(float x,float y) const {
        if(modal_==ModalKind::Confirm){
            const auto box=CenteredModalBox(384,186);
            if(Contains(Rect(box.right-224,box.top+132,96,30),x,y))return ControlId::ModalAccept;
            if(Contains(Rect(box.right-120,box.top+132,96,30),x,y))return ControlId::ModalCancel;
            return ControlId::None;
        }
        if(modal_==ModalKind::Export){
            if(Contains(Rect(562,431,96,30),x,y))return ControlId::ModalAccept;
            if(Contains(Rect(666,431,96,30),x,y))return ControlId::ModalCancel;
            return ControlId::None;
        }
        if(modal_==ModalKind::UpdateAvailable||modal_==ModalKind::UpdateReady){
            if(Contains(UpdatePrimaryRect(),x,y))return ControlId::ModalAccept;
            if(Contains(UpdateSecondaryRect(),x,y))return ControlId::ModalCancel;
            return ControlId::None;
        }
        if(modal_==ModalKind::UpdateProgress){
            if(Contains(UpdateSecondaryRect(),x,y))return ControlId::ModalCancel;
            return ControlId::None;
        }
        if(modal_==ModalKind::UpdateResult){
            if(Contains(UpdatePrimaryRect(),x,y))return ControlId::ModalAccept;
            return ControlId::None;
        }
        if(Contains(Rect(20,120,320,46),x,y))return ControlId::DropZone;
        if(Contains(Rect(20,410,156,30),x,y))return ControlId::Stop;
        if(Contains(Rect(184,410,156,30),x,y))return ControlId::Again;
        if(Contains(Rect(710,410,96,30),x,y))return ControlId::Record;
        if(Contains(Rect(968,228,96,30),x,y))return ControlId::Clear;
        if(Contains(Rect(1072,228,96,30),x,y))return ControlId::Export;
        if(Contains(Rect(882,15,96,30),x,y))return ControlId::Settings;
        if(Contains(Rect(986,15,96,30),x,y))return ControlId::Update;
        for(int i=0;i<5;++i){
            const float bx=20.0f+i*232.0f;
            if(slots_[i].used&&Contains(Rect(bx+12,676,94,24),x,y))return static_cast<ControlId>(static_cast<int>(ControlId::CardEdit0)+i);
            if(slots_[i].used&&Contains(Rect(bx+114,676,94,24),x,y))return static_cast<ControlId>(static_cast<int>(ControlId::CardDelete0)+i);
        }
        return ControlId::None;
    }

    void Activate(ControlId id) {
        if(id==ControlId::DropZone){PickImageFile();return;}
        if(id==ControlId::Stop){StopOcr();return;}
        if(id==ControlId::Again){StartOcr();return;}
        if(id==ControlId::Record){RecordCurrent();return;}
        if(id==ControlId::Clear){OpenConfirm(ConfirmAction::ClearAll,L"清空记录",L"确认清空5个声骸的全部记录？",L"清空",true);return;}
        if(id==ControlId::Export){OpenExport();return;}
        if(id==ControlId::Update){
            if(updateReady_){
                updateState_.phase=NativeUpdatePhase::Ready;updateState_.latest=updateManager_.LatestInfo();
                if(updateState_.latest.version.empty())updateState_.latest.version=updateManager_.PreparedVersion();
                modal_=ModalKind::UpdateReady;ShowWindow(edit_,SW_HIDE);
            }else{
                updateBusy_=true;updateState_.phase=NativeUpdatePhase::Checking;statusLevel_=StatusLevel::Attention;status_=L"正在检查更新";
                updateManager_.Check(true);
            }
            return;
        }
        if(id==ControlId::ModalAccept){AcceptModal();return;}
        if(id==ControlId::ModalCancel){CancelModal();return;}
        const int value=static_cast<int>(id);
        const int edit0=static_cast<int>(ControlId::CardEdit0),del0=static_cast<int>(ControlId::CardDelete0);
        if(value>=edit0&&value<edit0+5){LoadSlot(value-edit0);return;}
        if(value>=del0&&value<del0+5){
            pendingSlot_=value-del0;
            OpenConfirm(ConfirmAction::DeleteSlot,L"删除记录",L"确认删除声骸 "+std::to_wstring(pendingSlot_+1)+L" 的记录？",L"删除",true);return;
        }
    }

    void RecordCurrent() {
        if(!RowsValid()){statusLevel_=StatusLevel::Attention;status_=L"请补齐五条属性及档位，并避免重复属性";return;}
        if(slots_[selectedSlot_].used){
            pendingSlot_=selectedSlot_;
            OpenConfirm(ConfirmAction::OverwriteSlot,L"覆盖记录",L"声骸 "+std::to_wstring(selectedSlot_+1)+L" 已有记录，确认覆盖？",L"覆盖",false);
            return;
        }
        SaveCurrentToSlot(selectedSlot_);
    }
    void SaveCurrentToSlot(int slot){
        const bool wasUsed=slots_[slot].used;
        slots_[slot].used=true;slots_[slot].rows=rows_;slots_[slot].subtotal=CurrentSubtotal();
        statusLevel_=StatusLevel::Normal;status_=L"已记录到声骸 "+std::to_wstring(slot+1);
        if(!wasUsed){
            for(int offset=1;offset<=5;++offset){
                const int candidate=(slot+offset)%5;
                if(!slots_[candidate].used){selectedSlot_=candidate;break;}
            }
        }
    }
    void LoadSlot(int slot){rows_=slots_[slot].rows;selectedSlot_=slot;statusLevel_=StatusLevel::Normal;status_=L"已载入声骸 "+std::to_wstring(slot+1)+L"，修改后可覆盖";}

    void OpenConfirm(ConfirmAction action,std::wstring title,std::wstring message,std::wstring accept,bool red){
        dropdown_={}; modal_=ModalKind::Confirm;confirmAction_=action;confirmTitle_=std::move(title);confirmMessage_=std::move(message);confirmAcceptText_=std::move(accept);confirmAcceptRed_=red;ShowWindow(edit_,SW_HIDE);InvalidateRect(hwnd_,nullptr,FALSE);
    }
    void OpenExport(){modal_=ModalKind::Export;exportTitleInvalid_=false;exportEditFocused_=true;SetWindowTextW(edit_,L"");PositionEditControl();ShowWindow(edit_,SW_SHOW);SetFocus(edit_);InvalidateRect(edit_,nullptr,TRUE);InvalidateRect(hwnd_,nullptr,FALSE);}
    void CloseModal(){modal_=ModalKind::None;confirmAction_=ConfirmAction::None;exportEditFocused_=false;ShowWindow(edit_,SW_HIDE);SetFocus(hwnd_);InvalidateRect(hwnd_,nullptr,FALSE);}
    void CancelModal(){
        if(modal_==ModalKind::UpdateProgress){
            if(updateState_.phase==NativeUpdatePhase::Downloading||updateState_.phase==NativeUpdatePhase::Preparing)
                updateManager_.CancelDownload();
            return;
        }
        if(modal_==ModalKind::UpdateReady){
            updateReady_=true;statusLevel_=StatusLevel::Attention;status_=L"更新包已保留，可稍后安装";CloseModal();return;
        }
        CloseModal();
    }
    void AcceptModal(){
        if(modal_==ModalKind::UpdateAvailable){
            modal_=ModalKind::UpdateProgress;updateBusy_=true;updateState_.phase=NativeUpdatePhase::Preparing;
            updateState_.message=L"正在准备更新包";updateManager_.DownloadLatest();InvalidateRect(hwnd_,nullptr,FALSE);return;
        }
        if(modal_==ModalKind::UpdateReady){
            std::wstring error;
            if(updateManager_.LaunchPreparedInstaller(error)){
                updateBusy_=true;statusLevel_=StatusLevel::Attention;status_=L"正在启动更新助手";
                PostMessageW(hwnd_,WM_CLOSE,0,0);
            }else{
                updateState_.phase=NativeUpdatePhase::Error;updateState_.error=error;statusLevel_=StatusLevel::Error;status_=error;modal_=ModalKind::UpdateResult;
            }
            InvalidateRect(hwnd_,nullptr,FALSE);return;
        }
        if(modal_==ModalKind::UpdateResult){CloseModal();return;}
        if(modal_==ModalKind::Export){
            exportTitleInvalid_=WeightedTitleLength(GetEditText())>12.0f;
            if(exportTitleInvalid_){InvalidateRect(hwnd_,nullptr,FALSE);return;}
            if(SaveExportPng(GetEditText())){statusLevel_=StatusLevel::Normal;status_=L"记录图片已导出";}
            else{statusLevel_=exportError_==L"已取消导出"?StatusLevel::Attention:StatusLevel::Error;status_=exportError_.empty()?L"导出已取消或保存失败":exportError_;}
            CloseModal();return;
        }
        if(confirmAction_==ConfirmAction::ClearAll){for(auto&s:slots_)s={};selectedSlot_=0;status_=L"已清空全部记录";statusLevel_=StatusLevel::Normal;}
        else if(confirmAction_==ConfirmAction::DeleteSlot&&pendingSlot_>=0){slots_[pendingSlot_]={};status_=L"已删除声骸 "+std::to_wstring(pendingSlot_+1);statusLevel_=StatusLevel::Normal;}
        else if(confirmAction_==ConfirmAction::OverwriteSlot&&pendingSlot_>=0){SaveCurrentToSlot(pendingSlot_);}
        CloseModal();
    }

    void OpenDropdown(DropdownKind kind,int row,const D2D1_RECT_F& anchor){dropdown_.kind=kind;dropdown_.row=row;dropdown_.anchor=anchor;dropdown_.scroll=0;InvalidateRect(hwnd_,nullptr,FALSE);}
    std::vector<std::wstring> DropdownOptions() const {
        std::vector<std::wstring> out;
        if(dropdown_.kind==DropdownKind::Attribute){for(const auto&r:rules_)out.push_back(r.name);}
        else if(dropdown_.kind==DropdownKind::Value&&dropdown_.row>=0&&rows_[dropdown_.row].attribute>=0)out=rules_[rows_[dropdown_.row].attribute].values;
        else if(dropdown_.kind==DropdownKind::Slot){for(int i=0;i<5;++i)out.push_back(L"声骸 "+std::to_wstring(i+1)+(slots_[i].used?L"（已记录）":L""));}
        return out;
    }
    D2D1_RECT_F DropdownPopupRect() const {
        const int count=static_cast<int>(DropdownOptions().size()),visible=std::min(8,count);const float h=visible*28.0f;
        float top=dropdown_.anchor.bottom+4;if(top+h>724)top=dropdown_.anchor.top-h-4;
        return Rect(dropdown_.anchor.left,top,dropdown_.anchor.right-dropdown_.anchor.left,h);
    }
    bool HandleDropdownClick(float x,float y){
        const auto popup=DropdownPopupRect();if(!Contains(popup,x,y))return false;
        const auto opts=DropdownOptions();int local=static_cast<int>((y-popup.top)/28.0f);int index=dropdown_.scroll+local;if(index<0||index>=static_cast<int>(opts.size()))return true;
        if(dropdown_.kind==DropdownKind::Attribute){rows_[dropdown_.row].attribute=index;rows_[dropdown_.row].value=-1;rowConfidence_[dropdown_.row]=3;}
        else if(dropdown_.kind==DropdownKind::Value){rows_[dropdown_.row].value=index;rowConfidence_[dropdown_.row]=3;}
        else if(dropdown_.kind==DropdownKind::Slot){selectedSlot_=index;}
        dropdown_={};statusLevel_=StatusLevel::Normal;status_=L"已更新手动选择";InvalidateRect(hwnd_,nullptr,FALSE);return true;
    }

    void PickImageFile(){
        wchar_t file[MAX_PATH]{};OPENFILENAMEW ofn{sizeof(ofn)};ofn.hwndOwner=hwnd_;ofn.lpstrFilter=L"图片文件\0*.png;*.jpg;*.jpeg;*.bmp;*.webp\0所有文件\0*.*\0";ofn.lpstrFile=file;ofn.nMaxFile=MAX_PATH;ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
        if(GetOpenFileNameW(&ofn))LoadImageFile(file);
    }
    void LoadImageFile(const std::wstring& path){
        ComPtr<IWICBitmapDecoder> decoder;HRESULT hr=wicFactory_->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,decoder.GetAddressOf());
        if(FAILED(hr)){statusLevel_=StatusLevel::Error;status_=L"无法读取图片";InvalidateRect(hwnd_,nullptr,FALSE);return;}
        ComPtr<IWICBitmapFrameDecode> frame;decoder->GetFrame(0,frame.GetAddressOf());SetImageSource(frame.Get(),std::filesystem::path(path).filename().wstring());
    }
    void ImportClipboardBitmap(){
        if(!OpenClipboard(hwnd_))return;HBITMAP hb=static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
        if(hb){ComPtr<IWICBitmap> bitmap;if(SUCCEEDED(wicFactory_->CreateBitmapFromHBITMAP(hb,nullptr,WICBitmapUsePremultipliedAlpha,bitmap.GetAddressOf())))SetImageSource(bitmap.Get(),L"剪贴板图片");}
        CloseClipboard();
    }
    void SetImageSource(IWICBitmapSource* source,const std::wstring& name){
        ComPtr<IWICFormatConverter> converter;wicFactory_->CreateFormatConverter(converter.GetAddressOf());
        if(FAILED(converter->Initialize(source,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))){statusLevel_=StatusLevel::Error;status_=L"图片格式转换失败";return;}
        imageWic_.Reset();wicFactory_->CreateBitmapFromSource(converter.Get(),WICBitmapCacheOnLoad,imageWic_.GetAddressOf());
        imageWic_->GetSize(&imageW_,&imageH_);imageName_=name;RecreateD2DBitmap();statusLevel_=StatusLevel::Normal;status_=L"已导入 "+name;InvalidateRect(hwnd_,nullptr,FALSE);StartOcr();
    }
    void RecreateD2DBitmap(){imageD2D_.Reset();if(renderTarget_&&imageWic_)renderTarget_->CreateBitmapFromWicBitmap(imageWic_.Get(),nullptr,imageD2D_.GetAddressOf());}
    void LoadEmbeddedIcon(const char* encoded, ComPtr<ID2D1Bitmap>& target){
        target.Reset(); if(!renderTarget_) return;
        auto bytes=EmbeddedIcons::DecodeBase64(encoded); if(bytes.empty()) return;
        ComPtr<IWICStream> stream; if(FAILED(wicFactory_->CreateStream(stream.GetAddressOf()))) return;
        if(FAILED(stream->InitializeFromMemory(bytes.data(),static_cast<DWORD>(bytes.size())))) return;
        ComPtr<IWICBitmapDecoder> decoder; if(FAILED(wicFactory_->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,decoder.GetAddressOf()))) return;
        ComPtr<IWICBitmapFrameDecode> frame; if(FAILED(decoder->GetFrame(0,frame.GetAddressOf()))) return;
        ComPtr<IWICFormatConverter> converter; if(FAILED(wicFactory_->CreateFormatConverter(converter.GetAddressOf()))) return;
        if(FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) return;
        renderTarget_->CreateBitmapFromWicBitmap(converter.Get(),nullptr,target.GetAddressOf());
    }
    void LoadUiIcons(){
        LoadEmbeddedIcon(EmbeddedIcons::kArrowDown,iconArrow_); LoadEmbeddedIcon(EmbeddedIcons::kStar,iconStar_);
        LoadEmbeddedIcon(EmbeddedIcons::kNormal,iconNormal_); LoadEmbeddedIcon(EmbeddedIcons::kAbnormal,iconAbnormal_);
        LoadEmbeddedIcon(EmbeddedIcons::kManual,iconManual_);
    }
    void DrawIcon(ID2D1RenderTarget* rt,ID2D1Bitmap* bitmap,const D2D1_RECT_F& rect,float opacity=1.0f){
        if(bitmap) rt->DrawBitmap(bitmap,rect,opacity,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }

    std::wstring GetEditText() const {int len=GetWindowTextLengthW(edit_);std::wstring s(static_cast<size_t>(len)+1,L'\0');if(len)GetWindowTextW(edit_,s.data(),len+1);s.resize(static_cast<size_t>(len));return s;}
    float WeightedTitleLength(const std::wstring& s) const {float n=0;for(wchar_t ch:s)n+=(ch>=0x3400&&ch<=0x9fff)?1.0f:.5f;return n;}

    ComPtr<ID2D1Bitmap> CreateEmbeddedBitmap(ID2D1RenderTarget* target,const char* encoded){
        ComPtr<ID2D1Bitmap> bitmap;
        auto bytes=EmbeddedIcons::DecodeBase64(encoded); if(bytes.empty()) return bitmap;
        ComPtr<IWICStream> stream; if(FAILED(wicFactory_->CreateStream(stream.GetAddressOf()))) return bitmap;
        if(FAILED(stream->InitializeFromMemory(bytes.data(),static_cast<DWORD>(bytes.size())))) return bitmap;
        ComPtr<IWICBitmapDecoder> decoder; if(FAILED(wicFactory_->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,decoder.GetAddressOf()))) return bitmap;
        ComPtr<IWICBitmapFrameDecode> frame; if(FAILED(decoder->GetFrame(0,frame.GetAddressOf()))) return bitmap;
        ComPtr<IWICFormatConverter> converter; if(FAILED(wicFactory_->CreateFormatConverter(converter.GetAddressOf()))) return bitmap;
        if(FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) return bitmap;
        target->CreateBitmapFromWicBitmap(converter.Get(),nullptr,bitmap.GetAddressOf());
        return bitmap;
    }

    bool SaveExportPng(const std::wstring& customTitle){
        exportError_.clear();
        wchar_t filename[MAX_PATH]{};SYSTEMTIME st{};GetLocalTime(&st);
        swprintf_s(filename,L"鸣潮声骸记录_%04d-%02d-%02d_%02d-%02d-%02d.png",st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);
        OPENFILENAMEW ofn{sizeof(ofn)};ofn.hwndOwner=hwnd_;ofn.lpstrFilter=L"PNG 图片\0*.png\0";ofn.lpstrFile=filename;ofn.nMaxFile=MAX_PATH;ofn.lpstrDefExt=L"png";ofn.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
        if(!GetSaveFileNameW(&ofn)){exportError_=L"已取消导出";return false;}
        ComPtr<IWICBitmap> bitmap;HRESULT hr=wicFactory_->CreateBitmap(576,749,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,bitmap.GetAddressOf());
        if(FAILED(hr)){exportError_=L"无法创建导出画布";return false;}
        ComPtr<ID2D1RenderTarget> rt;auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        hr=d2dFactory_->CreateWicBitmapRenderTarget(bitmap.Get(),props,rt.GetAddressOf());if(FAILED(hr)){exportError_=L"无法创建导出绘制目标";return false;}
        ComPtr<ID2D1SolidColorBrush> b;hr=rt->CreateSolidColorBrush(Hex(0xffffff),b.GetAddressOf());if(FAILED(hr)){exportError_=L"无法创建导出画刷";return false;}
        auto star=CreateEmbeddedBitmap(rt.Get(),EmbeddedIcons::kStar);
        rt->BeginDraw();rt->Clear(Hex(0x202020));
        Text(rt.Get(),b.Get(),L"声骸记录",Rect(40,40,180,28),title_.Get(),Hex(0xffffff));
        wchar_t timeText[64]{};swprintf_s(timeText,L"%04d/%d/%d %02d:%02d:%02d",st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);
        Text(rt.Get(),b.Get(),L"导出时间",Rect(41,68,56,20),body14_.Get(),Hex(0xffffff,.60f));
        Text(rt.Get(),b.Get(),timeText,Rect(101,68,170,20),body14_.Get(),Hex(0xffffff,.60f));
        const std::array<D2D1_POINT_2F,5> pos{{{40,100},{292,100},{40,307},{292,307},{40,514}}};
        for(int i=0;i<5;++i)DrawExportCard(rt.Get(),b.Get(),slots_[i],i,pos[i].x,pos[i].y,star.Get());
        FillRound(rt.Get(),b.Get(),Rect(292,514,244,155),3,Hex(0xffffff,.03f));
        DrawExportMetric(rt.Get(),b.Get(),L"总分",std::to_wstring(TotalScore()),L"5件声骸总分",320,549);
        wchar_t avg[32]{};swprintf_s(avg,L"%.1f",TotalScore()/5.0);DrawExportMetric(rt.Get(),b.Get(),L"平均分",avg,L"5件声骸平均分",428,549);
        Text(rt.Get(),b.Get(),customTitle.empty()?L"鸣潮声骸计算器 v"+std::wstring(kAppVersion):customTitle,Rect(40,687,496,22),version_.Get(),Hex(0xffffff),DWRITE_TEXT_ALIGNMENT_TRAILING);
        hr=rt->EndDraw();
        if(FAILED(hr)){wchar_t code[32]{};swprintf_s(code,L"（0x%08X）",static_cast<unsigned int>(hr));exportError_=L"绘制导出图片失败"+std::wstring(code);return false;}
        star.Reset();b.Reset();rt.Reset();
        ComPtr<IWICStream> stream;hr=wicFactory_->CreateStream(stream.GetAddressOf());if(FAILED(hr)){exportError_=L"无法创建输出流";return false;}
        hr=stream->InitializeFromFilename(filename,GENERIC_WRITE);if(FAILED(hr)){exportError_=L"无法写入所选路径";return false;}
        ComPtr<IWICBitmapEncoder> encoder;hr=wicFactory_->CreateEncoder(GUID_ContainerFormatPng,nullptr,encoder.GetAddressOf());if(FAILED(hr)){exportError_=L"无法创建 PNG 编码器";return false;}
        hr=encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);if(FAILED(hr)){exportError_=L"无法初始化 PNG 编码器";return false;}
        ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> bag;hr=encoder->CreateNewFrame(frame.GetAddressOf(),bag.GetAddressOf());if(FAILED(hr)){exportError_=L"无法创建 PNG 帧";return false;}
        if(FAILED(frame->Initialize(bag.Get()))||FAILED(frame->SetSize(576,749))){exportError_=L"无法初始化 PNG 帧";return false;}
        WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
        hr=frame->SetPixelFormat(&format);
        if(FAILED(hr)){exportError_=L"PNG 像素格式设置失败";return false;}
        ComPtr<IWICBitmapSource> encoderSource;
        hr=WICConvertBitmapSource(format,bitmap.Get(),encoderSource.GetAddressOf());
        if(FAILED(hr)){
            wchar_t code[32]{};swprintf_s(code,L"（0x%08X）",static_cast<unsigned int>(hr));
            exportError_=L"PNG 像素格式转换失败"+std::wstring(code);return false;
        }
        if(FAILED(frame->WriteSource(encoderSource.Get(),nullptr))||FAILED(frame->Commit())||FAILED(encoder->Commit())){exportError_=L"完成 PNG 文件失败";return false;}
        return true;
    }

    void DrawExportMetric(ID2D1RenderTarget* rt,ID2D1SolidColorBrush* b,const std::wstring& label,const std::wstring& value,const std::wstring& caption,float x,float y){
        Text(rt,b,label,Rect(x,y,80,22),version_.Get(),Hex(0xffffff),DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(rt,b,value,Rect(x,y+30,80,34),exportStat24_.Get(),Hex(0x6ccb5f),DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(rt,b,caption,Rect(x,y+68,80,17),body12_.Get(),Hex(0xffffff,.60f),DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    void DrawExportCard(ID2D1RenderTarget* rt,ID2D1SolidColorBrush* b,const SlotRecord& slot,int index,float x,float y,ID2D1Bitmap* star){
        FillRound(rt,b,Rect(x,y,244,195),3,Hex(0xffffff,.03f));
        Text(rt,b,L"声骸",Rect(x+12,y+12,32,22),version_.Get(),Hex(0xffffff));
        Text(rt,b,std::to_wstring(index+1),Rect(x+49,y+12,14,22),version_.Get(),Hex(0xffffff));
        Text(rt,b,L"小计",Rect(x+169,y+12,32,22),version_.Get(),Hex(0xffffff),DWRITE_TEXT_ALIGNMENT_TRAILING);
        Text(rt,b,FormatScore(slot.subtotal),Rect(x+209,y+12,23,22),version_.Get(),ScoreColor(slot.subtotal),DWRITE_TEXT_ALIGNMENT_TRAILING);
        for(int r=0;r<5;++r){
            const float ry=y+42+r*29;
            FillRound(rt,b,Rect(x+12,ry,220,25),1,Hex(0xffffff,.06f));
            if(star) DrawIcon(rt,star,Rect(x+18,ry+7,11,11));
            const auto&sel=slot.rows[r];const auto&rule=rules_[sel.attribute];
            Text(rt,b,ShortName(rule.name),Rect(x+33,ry,112,25),body14_.Get(),Hex(0xffffff));
            Text(rt,b,rule.values[sel.value],Rect(x+145,ry,39,25),body14_.Get(),Hex(0xffffff),DWRITE_TEXT_ALIGNMENT_TRAILING);
            Text(rt,b,FormatScore(rule.scores[sel.value]),Rect(x+197,ry,17,25),body14_.Get(),ScoreColor(rule.scores[sel.value]),DWRITE_TEXT_ALIGNMENT_TRAILING);
        }
    }


private:
    HWND hwnd_ = nullptr;
    HWND edit_ = nullptr;
    HFONT editFont_ = nullptr;
    HBRUSH editBrush_ = nullptr;
    float dpi_ = 96.0f;
    ComPtr<ID2D1Factory> d2dFactory_;
    ComPtr<IDWriteFactory> dwriteFactory_;
    ComPtr<IWICImagingFactory> wicFactory_;
    ComPtr<ID2D1HwndRenderTarget> renderTarget_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IWICBitmap> imageWic_;
    ComPtr<ID2D1Bitmap> imageD2D_;
    ComPtr<ID2D1Bitmap> iconArrow_,iconStar_,iconNormal_,iconAbnormal_,iconManual_;
    UINT imageW_ = 0, imageH_ = 0;
    std::wstring imageName_;
    ComPtr<IDWriteTextFormat> title_,version_,body14_,body12_,body12Bold_,body11_,body10_,body8_,stat20_,exportStat24_,updateBody_;
    std::vector<AttributeRule> rules_;
    std::array<RowSelection,5> rows_{};
    std::array<int,5> rowConfidence_{};
    std::array<SlotRecord,5> slots_{};
    int selectedSlot_ = 0;
    int pendingSlot_ = -1;
    NativeUpdateManager updateManager_;
    NativeUpdateSnapshot updateState_{};
    NativeOcrEngine ocr_;
    std::thread ocrThread_;
    std::atomic_bool ocrCancel_{false};
    bool ocrReady_ = false;
    bool ocrRunning_ = false;
    bool topmost_ = false;
    StatusLevel statusLevel_ = StatusLevel::Normal;
    bool exportTitleInvalid_ = false;
    bool exportEditFocused_ = false;
    bool updateReady_ = false;
    bool updateBusy_ = false;
    std::wstring exportError_;
    std::wstring status_;
    ControlId hovered_ = ControlId::None;
    ControlId pressed_ = ControlId::None;
    float lastMouseX_ = 0, lastMouseY_ = 0;
    DropdownState dropdown_{};
    ModalKind modal_ = ModalKind::None;
    ConfirmAction confirmAction_ = ConfirmAction::None;
    std::wstring confirmTitle_,confirmMessage_,confirmAcceptText_;
    bool confirmAcceptRed_ = false;
};

App* g_app = nullptr;

SIZE WindowSizeForDpi(UINT dpi) {
    RECT rect{0,0,MulDiv(static_cast<int>(kClientWidth),dpi,96),MulDiv(static_cast<int>(kClientHeight),dpi,96)};
    AdjustWindowRectExForDpi(&rect,kWindowStyle,FALSE,0,dpi);
    return {rect.right-rect.left,rect.bottom-rect.top};
}

void ApplyWindows11Appearance(HWND hwnd) {
    BOOL dark=TRUE;constexpr DWORD darkAttr=20;DwmSetWindowAttribute(hwnd,darkAttr,&dark,sizeof(dark));
    const COLORREF caption=RGB(32,32,32);constexpr DWORD captionAttr=35;DwmSetWindowAttribute(hwnd,captionAttr,&caption,sizeof(caption));
}

LRESULT CALLBACK WindowProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam){
    switch(msg){
    case WM_CREATE:{ApplyWindows11Appearance(hwnd);g_app=new App();if(FAILED(g_app->Initialize(hwnd)))return -1;return 0;}
    case WM_PAINT:g_app->Paint();return 0;
    case WM_ERASEBKGND:return 1;
    case WM_SIZE:g_app->Resize(LOWORD(lParam),HIWORD(lParam));return 0;
    case WM_GETMINMAXINFO:{auto*info=reinterpret_cast<MINMAXINFO*>(lParam);const auto s=WindowSizeForDpi(GetDpiForWindow(hwnd));info->ptMinTrackSize={s.cx,s.cy};info->ptMaxTrackSize={s.cx,s.cy};return 0;}
    case WM_DPICHANGED:{const UINT dpi=HIWORD(wParam);const RECT*r=reinterpret_cast<RECT*>(lParam);const auto s=WindowSizeForDpi(dpi);SetWindowPos(hwnd,nullptr,r->left,r->top,s.cx,s.cy,SWP_NOZORDER|SWP_NOACTIVATE);g_app->DpiChanged(dpi);return 0;}
    case WM_MOUSEMOVE:{const float scale=96.0f/GetDpiForWindow(hwnd);g_app->MouseMove(GET_X_LPARAM(lParam)*scale,GET_Y_LPARAM(lParam)*scale);return 0;}
    case WM_MOUSELEAVE:g_app->MouseLeave();return 0;
    case WM_LBUTTONDOWN:{SetFocus(hwnd);const float scale=96.0f/GetDpiForWindow(hwnd);g_app->MouseDown(GET_X_LPARAM(lParam)*scale,GET_Y_LPARAM(lParam)*scale);return 0;}
    case WM_LBUTTONUP:{const float scale=96.0f/GetDpiForWindow(hwnd);g_app->MouseUp(GET_X_LPARAM(lParam)*scale,GET_Y_LPARAM(lParam)*scale);return 0;}
    case WM_MOUSEWHEEL:{POINT pt{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};ScreenToClient(hwnd,&pt);const float scale=96.0f/GetDpiForWindow(hwnd);g_app->MouseWheel(pt.x*scale,pt.y*scale,GET_WHEEL_DELTA_WPARAM(wParam));return 0;}
    case WM_KEYDOWN:g_app->KeyDown(wParam);return 0;
    case WM_DROPFILES:g_app->DropFiles(reinterpret_cast<HDROP>(wParam));return 0;
    case WM_SETCURSOR:if(LOWORD(lParam)==HTCLIENT){SetCursor(LoadCursorW(nullptr,IDC_ARROW));return TRUE;}break;
    case WM_CTLCOLOREDIT:return reinterpret_cast<LRESULT>(g_app->EditColor(reinterpret_cast<HDC>(wParam)));
    case WM_COMMAND:
        if(LOWORD(wParam)==kEditId){
            if(HIWORD(wParam)==EN_CHANGE)g_app->EditChanged();
            else if(HIWORD(wParam)==EN_SETFOCUS)g_app->EditFocusChanged(true);
            else if(HIWORD(wParam)==EN_KILLFOCUS)g_app->EditFocusChanged(false);
        }
        return 0;
    case kOcrCompleteMessage:g_app->OcrCompleted(reinterpret_cast<NativeOcrJobResult*>(lParam));return 0;
    case kUpdateCompleteMessage:g_app->UpdateCompleted(reinterpret_cast<NativeUpdateSnapshot*>(lParam));return 0;
    case WM_DESTROY:delete g_app;g_app=nullptr;PostQuitMessage(0);return 0;
    default:return DefWindowProcW(hwnd,msg,wParam,lParam);
    }
    return DefWindowProcW(hwnd,msg,wParam,lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int showCommand){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE singleInstance=CreateMutexW(nullptr,TRUE,L"Local\\WuwaEchoCalculator.SingleInstance");
    if(!singleInstance)return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS){
        if(HWND existing=FindWindowW(kWindowClass,nullptr)){ShowWindow(existing,SW_RESTORE);SetForegroundWindow(existing);}
        CloseHandle(singleInstance);return 0;
    }
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED))){CloseHandle(singleInstance);return 1;}
    WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance;wc.lpfnWndProc=WindowProc;wc.lpszClassName=kWindowClass;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));wc.hIconSm=wc.hIcon;wc.hbrBackground=nullptr;
    if(!RegisterClassExW(&wc)){CoUninitialize();CloseHandle(singleInstance);return 1;}
    const auto size=WindowSizeForDpi(GetDpiForSystem());const int x=std::max(0,(GetSystemMetrics(SM_CXSCREEN)-size.cx)/2),y=std::max(0,(GetSystemMetrics(SM_CYSCREEN)-size.cy)/2);
    HWND hwnd=CreateWindowExW(0,kWindowClass,kWindowTitle,kWindowStyle,x,y,size.cx,size.cy,nullptr,nullptr,instance,nullptr);if(!hwnd){CoUninitialize();CloseHandle(singleInstance);return 1;}
    ShowWindow(hwnd,showCommand==SW_SHOWMINIMIZED?SW_SHOWMINIMIZED:SW_SHOWNORMAL);UpdateWindow(hwnd);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){
        if(g_app&&g_app->ExportModalOpen()&&msg.hwnd==g_app->EditHwnd()&&msg.message==WM_KEYDOWN){if(msg.wParam==VK_RETURN){g_app->ExportEnter();continue;}if(msg.wParam==VK_ESCAPE){g_app->ExportEscape();continue;}}
        TranslateMessage(&msg);DispatchMessageW(&msg);
    }
    CoUninitialize();CloseHandle(singleInstance);return static_cast<int>(msg.wParam);
}
