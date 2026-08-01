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

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "dwmapi.lib")

using Microsoft::WRL::ComPtr;

namespace {
constexpr wchar_t kWindowClass[] = L"WuwaEchoCalculatorNativeWindow";
constexpr wchar_t kWindowTitle[] = L"鸣潮声骸计算器 · 原生预览";
constexpr float kClientWidth = 1188.0f;
constexpr float kClientHeight = 772.0f;
constexpr DWORD kWindowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
constexpr UINT kEditId = 7001;

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
enum class ControlId {
    None, DropZone, Stop, Again, Record, Clear, Export, ModalAccept, ModalCancel,
    CardEdit0, CardEdit1, CardEdit2, CardEdit3, CardEdit4,
    CardDelete0, CardDelete1, CardDelete2, CardDelete3, CardDelete4
};
enum class DropdownKind { None, Attribute, Value, Slot };
enum class ModalKind { None, Confirm, Export };
enum class ConfirmAction { None, ClearAll, DeleteSlot, OverwriteSlot };

struct DropdownState {
    DropdownKind kind = DropdownKind::None;
    int row = -1;
    int scroll = 0;
    D2D1_RECT_F anchor{};
};

class App {
public:
    ~App() { if (editFont_) DeleteObject(editFont_); }

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
        status_ = L"原生界面已就绪 · OCR 模块待迁移";
        return S_OK;
    }

    void DiscardDeviceResources() {
        imageD2D_.Reset();
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

        if (Contains(Rect(1110, 20, 58, 20), x, y)) {
            topmost_ = !topmost_;
            SetWindowPos(hwnd_, topmost_ ? HWND_TOPMOST : HWND_NOTOPMOST, 0,0,0,0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }

        for (int row = 0; row < 5; ++row) {
            const float y0 = 152.0f + row * 50.0f;
            const auto attr = Rect(416, y0 + 7, 170, 32);
            const auto value = Rect(592, y0 + 7, 98, 32);
            if (Contains(attr, x, y)) { OpenDropdown(DropdownKind::Attribute, row, attr); return; }
            if (Contains(value, x, y) && rows_[row].attribute >= 0) { OpenDropdown(DropdownKind::Value, row, value); return; }
        }
        const auto slotRect = Rect(527, 410, 170, 30);
        if (Contains(slotRect, x, y)) { OpenDropdown(DropdownKind::Slot, -1, slotRect); return; }

        for (int i = 0; i < 5; ++i) {
            const auto card = Rect(20.0f + i * 232.0f, 500, 220, 212);
            if (!slots_[i].used && Contains(card, x, y)) {
                selectedSlot_ = i; status_ = L"已选择声骸 " + std::to_wstring(i + 1);
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
            if (modal_ != ModalKind::None) { CloseModal(); return; }
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
        SetBkMode(dc, TRANSPARENT);
        return static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    }

    void EditChanged() {
        if (modal_ != ModalKind::Export) return;
        exportTitleInvalid_ = WeightedTitleLength(GetEditText()) > 12.0f;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    HWND EditHwnd() const { return edit_; }
    bool ExportModalOpen() const { return modal_ == ModalKind::Export; }
    void ExportEnter() { if (modal_ == ModalKind::Export) AcceptModal(); }
    void ExportEscape() { if (modal_ == ModalKind::Export) CloseModal(); }

private:
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
    }

    void CreateFormat(float size, DWRITE_FONT_WEIGHT weight, IDWriteTextFormat** out) {
        dwriteFactory_->CreateTextFormat(L"Microsoft YaHei UI", nullptr, weight,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", out);
        (*out)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    void CreateEditControl() {
        edit_ = CreateWindowExW(WS_EX_TRANSPARENT, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
                                0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kEditId),
                                GetModuleHandleW(nullptr), nullptr);
        SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(10,10));
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
        SetWindowPos(edit_, HWND_TOP, px(r.left + 2), px(r.top + 1), px(r.right-r.left-4), px(r.bottom-r.top-2),
                     SWP_SHOWWINDOW | SWP_NOACTIVATE);
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
    void Text(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const std::wstring& text,
              const D2D1_RECT_F& r, IDWriteTextFormat* fmt, D2D1_COLOR_F c,
              DWRITE_TEXT_ALIGNMENT align=DWRITE_TEXT_ALIGNMENT_LEADING,
              DWRITE_PARAGRAPH_ALIGNMENT valign=DWRITE_PARAGRAPH_ALIGNMENT_CENTER) {
        fmt->SetTextAlignment(align); fmt->SetParagraphAlignment(valign); b->SetColor(c);
        rt->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), fmt, r, b,
                      D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
    }

    void DrawTopBar(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {
        Text(rt,b,L"鸣潮声骸计算器",Rect(20,20,180,28),title_.Get(),Hex(0xffffff));
        Text(rt,b,L"v1.3.0-native",Rect(200,22,130,24),version_.Get(),Hex(0xffffff));
        Text(rt,b,L"鸣潮声骸的本地识别评分工具",Rect(20,48,260,20),body14_.Get(),Hex(0xffffff,.60f));
        Text(rt,b,L"置顶",Rect(1110,20,32,20),body14_.Get(),Hex(0xffffff));
        const auto track = Rect(1150,21,38,18);
        FillRound(rt,b,track,9, topmost_ ? Hex(0x4cc2ff) : Hex(0xffffff,.06f));
        StrokeRound(rt,b,track,9,Hex(0xffffff,.06f));
        const float knobX = topmost_ ? 1173.0f : 1153.0f;
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
        DrawButton(rt,b,ControlId::Stop,Rect(20,410,156,30),L"停止识别",ButtonKind::Red,false);
        DrawButton(rt,b,ControlId::Again,Rect(184,410,156,30),L"重新识别",ButtonKind::Gray,imageW_>0);
    }

    void DrawReview(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {
        DrawHeading(rt,b,370,88,2,L"核对属性");
        const std::array<std::pair<const wchar_t*,D2D1_RECT_F>,5> heads{{
            {L"序号",Rect(372,124,38,17)}, {L"属性",Rect(416,124,170,17)},
            {L"档位",Rect(592,124,98,17)}, {L"分数",Rect(696,124,60,17)},
            {L"状态",Rect(762,124,42,17)}}};
        for (const auto& h:heads) Text(rt,b,h.first,h.second,body12_.Get(),Hex(0xffffff,.60f),DWRITE_TEXT_ALIGNMENT_CENTER);

        for (int i=0;i<5;++i) {
            const float y=152.0f+i*50.0f;
            const int score=ScoreOf(rows_[i]);
            D2D1_COLOR_F rowColor = score>0 ? Hex(0x6ccb5f,.18f) : score<0 ? Hex(0xff99a4,.18f) : Hex(0xffffff,.03f);
            FillRound(rt,b,Rect(370,y,436,46),3,rowColor);
            Text(rt,b,std::to_wstring(i+1),Rect(372,y,38,46),body14_.Get(),Hex(0xffffff),DWRITE_TEXT_ALIGNMENT_CENTER);
            DrawSelect(rt,b,Rect(416,y+7,170,32), rows_[i].attribute>=0?rules_[rows_[i].attribute].name:L"请选择属性", rows_[i].attribute>=0);
            std::wstring val=L"请选择档位";
            if (rows_[i].attribute>=0 && rows_[i].value>=0) val=rules_[rows_[i].attribute].values[rows_[i].value];
            DrawSelect(rt,b,Rect(592,y+7,98,32),val,rows_[i].attribute>=0);
            const std::wstring scoreText = rows_[i].attribute>=0 && rows_[i].value>=0 ? FormatScore(score) : L"—";
            const auto pill=Rect(696,y+7,60,32);
            FillRound(rt,b,pill,4,score>0?Hex(0x6ccb5f,.16f):score<0?Hex(0xff99a4,.16f):Hex(0xffffff,.06f));
            Text(rt,b,scoreText,pill,body14_.Get(),score>0?Hex(0x6ccb5f):score<0?Hex(0xff99a4):Hex(0xffffff,.60f),DWRITE_TEXT_ALIGNMENT_CENTER);
            b->SetColor(rows_[i].attribute>=0?Hex(0xff99a4):Hex(0xffffff,.25f));
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(783,y+23),4,4),b);
        }
        Text(rt,b,L"本件记分",Rect(370,410,70,30),body12_.Get(),Hex(0xffffff));
        Text(rt,b,std::to_wstring(CurrentSubtotal()),Rect(452,410,30,30),stat20_.Get(),Hex(0x6ccb5f),DWRITE_TEXT_ALIGNMENT_LEADING);
        Text(rt,b,L"记录到",Rect(482,410,36,30),body12_.Get(),Hex(0xffffff));
        DrawSelect(rt,b,Rect(527,410,170,30),L"声骸 "+std::to_wstring(selectedSlot_+1)+(slots_[selectedSlot_].used?L"（已记录）":L""),true);
        DrawButton(rt,b,ControlId::Record,Rect(706,410,96,30),L"记录",ButtonKind::Blue,true);
    }

    void DrawSelect(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const D2D1_RECT_F& r, const std::wstring& value, bool enabled) {
        FillRound(rt,b,r,4, enabled?Hex(0x202020):Hex(0x202020,.55f));
        Text(rt,b,value,Rect(r.left+11,r.top,r.right-r.left-34,r.bottom-r.top),body14_.Get(),enabled?Hex(0xffffff):Hex(0xffffff,.35f));
        b->SetColor(enabled?Hex(0xffffff,.75f):Hex(0xffffff,.25f));
        const float cx=r.right-15,cy=(r.top+r.bottom)/2;
        rt->DrawLine(D2D1::Point2F(cx-4,cy-2),D2D1::Point2F(cx,cy+2),b,1.2f);
        rt->DrawLine(D2D1::Point2F(cx,cy+2),D2D1::Point2F(cx+4,cy-2),b,1.2f);
    }

    void DrawSide(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {
        DrawHeading(rt,b,836,88,4,L"当前统计");
        FillRound(rt,b,Rect(836,120,332,96),3,Hex(0xffffff,.03f));
        const int count=RecordCount(), total=TotalScore();
        DrawMetric(rt,b,852,128,L"已记录",std::to_wstring(count)+L" / 5",L"",Hex(0xffffff));
        DrawMetric(rt,b,956,128,L"总分",std::to_wstring(total),L"5件声骸总分",Hex(0x6ccb5f));
        wchar_t avg[32]{}; swprintf_s(avg,L"%.1f",total/5.0);
        DrawMetric(rt,b,1060,128,L"平均分",avg,L"5件声骸平均分",Hex(0x6ccb5f));
        DrawButton(rt,b,ControlId::Clear,Rect(960,228,96,30),L"清空全部",ButtonKind::Red,count>0);
        DrawButton(rt,b,ControlId::Export,Rect(1064,228,96,30),L"导出记录",ButtonKind::Blue,count==5);

        FillRound(rt,b,Rect(836,288,332,152),4,Hex(0xffffff,.03f));
        Text(rt,b,L"操作说明",Rect(848,300,120,20),body14_.Get(),Hex(0xffffff));
        const std::array<const wchar_t*,4> lines{{L"1. 粘贴、拖入或点击上传声骸截图",L"2. 原生 OCR 接入前可手动选择属性",L"3. 通过下拉框调整属性与档位",L"4. 选择记录位置后写入记录卡"}};
        for(int i=0;i<4;++i) Text(rt,b,lines[i],Rect(850,328+i*18,300,18),body12_.Get(),Hex(0xffffff,.60f));
        b->SetColor(Hex(0x6ccb5f));rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(855,420),7,7),b);
        Text(rt,b,L"识别准确",Rect(867,409,65,22),body10_.Get(),Hex(0xffffff,.60f));
        b->SetColor(Hex(0xfce100));rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(952,420),7,7),b);
        Text(rt,b,L"建议核对",Rect(964,409,65,22),body10_.Get(),Hex(0xffffff,.60f));
        FillRound(rt,b,Rect(1043,414,20,12),2,Hex(0xff99a4));
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
                Text(rt,b,L"声骸 "+std::to_wstring(i+1),Rect(x+12,y+12,100,17),body12Bold_.Get(),Hex(0xffffff));
                Text(rt,b,selectedSlot_==i?L"当前记录位置":L"尚未记录",Rect(x,y+90,220,34),body12Bold_.Get(),Hex(0xffffff,.60f),DWRITE_TEXT_ALIGNMENT_CENTER);
                continue;
            }
            Text(rt,b,L"声骸 "+std::to_wstring(i+1),Rect(x+12,y,90,35),body12Bold_.Get(),Hex(0xffffff));
            Text(rt,b,L"小计",Rect(x+142,y,35,35),body12Bold_.Get(),Hex(0xffffff));
            Text(rt,b,FormatScore(slots_[i].subtotal),Rect(x+177,y,31,35),body12Bold_.Get(),ScoreColor(slots_[i].subtotal),DWRITE_TEXT_ALIGNMENT_TRAILING);
            for(int r=0;r<5;++r){
                const float ry=y+35+r*26;
                FillRound(rt,b,Rect(x+12,ry,196,23),1,Hex(0xffffff,.06f));
                Text(rt,b,L"✦",Rect(x+18,ry,12,23),body11_.Get(),Hex(0xffffff));
                const auto& sel=slots_[i].rows[r];
                const auto& rule=rules_[sel.attribute];
                Text(rt,b,ShortName(rule.name),Rect(x+33,ry,110,23),body11_.Get(),Hex(0xffffff));
                Text(rt,b,rule.values[sel.value],Rect(x+143,ry,41,23),body12_.Get(),Hex(0xffffff),DWRITE_TEXT_ALIGNMENT_TRAILING);
                Text(rt,b,FormatScore(rule.scores[sel.value]),Rect(x+184,ry,20,23),body12_.Get(),ScoreColor(rule.scores[sel.value]),DWRITE_TEXT_ALIGNMENT_TRAILING);
            }
            DrawButton(rt,b,static_cast<ControlId>(static_cast<int>(ControlId::CardEdit0)+i),Rect(x+12,y+176,94,24),L"编辑",ButtonKind::Gray,true,12);
            DrawButton(rt,b,static_cast<ControlId>(static_cast<int>(ControlId::CardDelete0)+i),Rect(x+114,y+176,94,24),L"删除",ButtonKind::Red,true,12);
        }
    }

    void DrawStatus(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {
        b->SetColor(Hex(statusError_?0xff99a4:0xfce100));
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(24,742),4,4),b);
        Text(rt,b,status_,Rect(34,732,900,20),body14_.Get(),Hex(0xffffff,.60f));
        Text(rt,b,L"Direct2D 原生预览",Rect(1000,732,168,20),body11_.Get(),Hex(0xffffff,.60f),DWRITE_TEXT_ALIGNMENT_TRAILING);
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
        if(modal_==ModalKind::Confirm){
            const auto box=Rect(402,293,384,186);
            FillRound(rt,b,box,7,Hex(0x202020));
            FillRound(rt,b,Rect(402,293,384,108),7,Hex(0xffffff,.06f));
            Fill(rt,b,Rect(402,394,384,7),Hex(0xffffff,.06f));
            Text(rt,b,confirmTitle_,Rect(426,317,336,28),title_.Get(),Hex(0xffffff));
            Text(rt,b,confirmMessage_,Rect(426,357,336,20),body14_.Get(),Hex(0xffffff));
            DrawButton(rt,b,ControlId::ModalAccept,Rect(562,425,96,30),confirmAcceptText_,confirmAcceptRed_?ButtonKind::Red:ButtonKind::Blue,true);
            DrawButton(rt,b,ControlId::ModalCancel,Rect(666,425,96,30),L"取消",ButtonKind::Gray,true);
        } else {
            const auto box=Rect(402,287,384,198);
            FillRound(rt,b,box,7,Hex(0x202020));
            FillRound(rt,b,Rect(402,287,384,120),7,Hex(0xffffff,.06f));
            Fill(rt,b,Rect(402,400,384,7),Hex(0xffffff,.06f));
            Text(rt,b,L"导出记录",Rect(426,311,336,28),title_.Get(),Hex(0xffffff));
            const auto input=ExportInputRect();
            FillRound(rt,b,input,4,Hex(0x000000,.30f));
            StrokeRound(rt,b,input,4,Hex(0xffffff,.10f));
            Fill(rt,b,Rect(input.left,input.bottom-1,input.right-input.left,1),exportTitleInvalid_?Hex(0xfce100):Hex(0x4cc2ff));
            if(exportTitleInvalid_){
                const auto hint=Rect(438,375,292,20);
                FillRound(rt,b,hint,3,Hex(0x454545));
                Text(rt,b,L"最多可输入12个字符，超过最大长度限制",hint,body10_.Get(),Hex(0xfce100),DWRITE_TEXT_ALIGNMENT_CENTER);
            }
            DrawButton(rt,b,ControlId::ModalAccept,Rect(562,431,96,30),L"确定",ButtonKind::Blue,true);
            DrawButton(rt,b,ControlId::ModalCancel,Rect(666,431,96,30),L"取消",ButtonKind::Gray,true);
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
        if(id==ControlId::Stop)return false;
        if(id==ControlId::Again)return imageW_>0;
        if(id==ControlId::Clear)return RecordCount()>0;
        if(id==ControlId::Export)return RecordCount()==5;
        return true;
    }

    ControlId HitTestButton(float x,float y) const {
        if(modal_==ModalKind::Confirm){
            if(Contains(Rect(562,425,96,30),x,y))return ControlId::ModalAccept;
            if(Contains(Rect(666,425,96,30),x,y))return ControlId::ModalCancel;
            return ControlId::None;
        }
        if(modal_==ModalKind::Export){
            if(Contains(Rect(562,431,96,30),x,y))return ControlId::ModalAccept;
            if(Contains(Rect(666,431,96,30),x,y))return ControlId::ModalCancel;
            return ControlId::None;
        }
        if(Contains(Rect(20,120,320,46),x,y))return ControlId::DropZone;
        if(Contains(Rect(20,410,156,30),x,y))return ControlId::Stop;
        if(Contains(Rect(184,410,156,30),x,y))return ControlId::Again;
        if(Contains(Rect(706,410,96,30),x,y))return ControlId::Record;
        if(Contains(Rect(960,228,96,30),x,y))return ControlId::Clear;
        if(Contains(Rect(1064,228,96,30),x,y))return ControlId::Export;
        for(int i=0;i<5;++i){
            const float bx=20.0f+i*232.0f;
            if(slots_[i].used&&Contains(Rect(bx+12,676,94,24),x,y))return static_cast<ControlId>(static_cast<int>(ControlId::CardEdit0)+i);
            if(slots_[i].used&&Contains(Rect(bx+114,676,94,24),x,y))return static_cast<ControlId>(static_cast<int>(ControlId::CardDelete0)+i);
        }
        return ControlId::None;
    }

    void Activate(ControlId id) {
        if(id==ControlId::DropZone){PickImageFile();return;}
        if(id==ControlId::Again){statusError_=false;status_=L"原生 OCR 尚未接入，当前请手动核对属性";return;}
        if(id==ControlId::Record){RecordCurrent();return;}
        if(id==ControlId::Clear){OpenConfirm(ConfirmAction::ClearAll,L"清空记录",L"确认清空5个声骸的全部记录？",L"清空",true);return;}
        if(id==ControlId::Export){OpenExport();return;}
        if(id==ControlId::ModalAccept){AcceptModal();return;}
        if(id==ControlId::ModalCancel){CloseModal();return;}
        const int value=static_cast<int>(id);
        const int edit0=static_cast<int>(ControlId::CardEdit0),del0=static_cast<int>(ControlId::CardDelete0);
        if(value>=edit0&&value<edit0+5){LoadSlot(value-edit0);return;}
        if(value>=del0&&value<del0+5){
            pendingSlot_=value-del0;
            OpenConfirm(ConfirmAction::DeleteSlot,L"删除记录",L"确认删除声骸 "+std::to_wstring(pendingSlot_+1)+L" 的记录？",L"删除",true);return;
        }
    }

    void RecordCurrent() {
        if(!RowsValid()){statusError_=true;status_=L"请补齐五条属性及档位，并避免重复属性";return;}
        if(slots_[selectedSlot_].used){
            pendingSlot_=selectedSlot_;
            OpenConfirm(ConfirmAction::OverwriteSlot,L"覆盖记录",L"声骸 "+std::to_wstring(selectedSlot_+1)+L" 已有记录，确认覆盖？",L"覆盖",false);
            return;
        }
        SaveCurrentToSlot(selectedSlot_);
    }
    void SaveCurrentToSlot(int slot){slots_[slot].used=true;slots_[slot].rows=rows_;slots_[slot].subtotal=CurrentSubtotal();statusError_=false;status_=L"已记录到声骸 "+std::to_wstring(slot+1);}
    void LoadSlot(int slot){rows_=slots_[slot].rows;selectedSlot_=slot;statusError_=false;status_=L"已载入声骸 "+std::to_wstring(slot+1)+L"，修改后可覆盖";}

    void OpenConfirm(ConfirmAction action,std::wstring title,std::wstring message,std::wstring accept,bool red){
        dropdown_={}; modal_=ModalKind::Confirm;confirmAction_=action;confirmTitle_=std::move(title);confirmMessage_=std::move(message);confirmAcceptText_=std::move(accept);confirmAcceptRed_=red;ShowWindow(edit_,SW_HIDE);InvalidateRect(hwnd_,nullptr,FALSE);
    }
    void OpenExport(){modal_=ModalKind::Export;exportTitleInvalid_=false;SetWindowTextW(edit_,L"");PositionEditControl();ShowWindow(edit_,SW_SHOW);SetFocus(edit_);InvalidateRect(hwnd_,nullptr,FALSE);}
    void CloseModal(){modal_=ModalKind::None;confirmAction_=ConfirmAction::None;ShowWindow(edit_,SW_HIDE);SetFocus(hwnd_);InvalidateRect(hwnd_,nullptr,FALSE);}
    void AcceptModal(){
        if(modal_==ModalKind::Export){
            exportTitleInvalid_=WeightedTitleLength(GetEditText())>12.0f;
            if(exportTitleInvalid_){InvalidateRect(hwnd_,nullptr,FALSE);return;}
            if(SaveExportPng(GetEditText())){statusError_=false;status_=L"记录图片已导出";}else{statusError_=true;status_=L"导出已取消或保存失败";}
            CloseModal();return;
        }
        if(confirmAction_==ConfirmAction::ClearAll){for(auto&s:slots_)s={};status_=L"已清空全部记录";statusError_=false;}
        else if(confirmAction_==ConfirmAction::DeleteSlot&&pendingSlot_>=0){slots_[pendingSlot_]={};status_=L"已删除声骸 "+std::to_wstring(pendingSlot_+1);statusError_=false;}
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
        if(dropdown_.kind==DropdownKind::Attribute){rows_[dropdown_.row].attribute=index;rows_[dropdown_.row].value=-1;}
        else if(dropdown_.kind==DropdownKind::Value){rows_[dropdown_.row].value=index;}
        else if(dropdown_.kind==DropdownKind::Slot){selectedSlot_=index;}
        dropdown_={};statusError_=false;status_=L"已更新手动选择";InvalidateRect(hwnd_,nullptr,FALSE);return true;
    }

    void PickImageFile(){
        wchar_t file[MAX_PATH]{};OPENFILENAMEW ofn{sizeof(ofn)};ofn.hwndOwner=hwnd_;ofn.lpstrFilter=L"图片文件\0*.png;*.jpg;*.jpeg;*.bmp;*.webp\0所有文件\0*.*\0";ofn.lpstrFile=file;ofn.nMaxFile=MAX_PATH;ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
        if(GetOpenFileNameW(&ofn))LoadImageFile(file);
    }
    void LoadImageFile(const std::wstring& path){
        ComPtr<IWICBitmapDecoder> decoder;HRESULT hr=wicFactory_->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,decoder.GetAddressOf());
        if(FAILED(hr)){statusError_=true;status_=L"无法读取图片";InvalidateRect(hwnd_,nullptr,FALSE);return;}
        ComPtr<IWICBitmapFrameDecode> frame;decoder->GetFrame(0,frame.GetAddressOf());SetImageSource(frame.Get(),std::filesystem::path(path).filename().wstring());
    }
    void ImportClipboardBitmap(){
        if(!OpenClipboard(hwnd_))return;HBITMAP hb=static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
        if(hb){ComPtr<IWICBitmap> bitmap;if(SUCCEEDED(wicFactory_->CreateBitmapFromHBITMAP(hb,nullptr,WICBitmapUsePremultipliedAlpha,bitmap.GetAddressOf())))SetImageSource(bitmap.Get(),L"剪贴板图片");}
        CloseClipboard();
    }
    void SetImageSource(IWICBitmapSource* source,const std::wstring& name){
        ComPtr<IWICFormatConverter> converter;wicFactory_->CreateFormatConverter(converter.GetAddressOf());
        if(FAILED(converter->Initialize(source,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))){statusError_=true;status_=L"图片格式转换失败";return;}
        imageWic_.Reset();wicFactory_->CreateBitmapFromSource(converter.Get(),WICBitmapCacheOnLoad,imageWic_.GetAddressOf());
        imageWic_->GetSize(&imageW_,&imageH_);imageName_=name;RecreateD2DBitmap();statusError_=false;status_=L"已导入 "+name+L" · 原生 OCR 待接入";InvalidateRect(hwnd_,nullptr,FALSE);
    }
    void RecreateD2DBitmap(){imageD2D_.Reset();if(renderTarget_&&imageWic_)renderTarget_->CreateBitmapFromWicBitmap(imageWic_.Get(),nullptr,imageD2D_.GetAddressOf());}

    std::wstring GetEditText() const {int len=GetWindowTextLengthW(edit_);std::wstring s(static_cast<size_t>(len)+1,L'\0');if(len)GetWindowTextW(edit_,s.data(),len+1);s.resize(static_cast<size_t>(len));return s;}
    float WeightedTitleLength(const std::wstring& s) const {float n=0;for(wchar_t ch:s)n+=(ch>=0x3400&&ch<=0x9fff)?1.0f:.5f;return n;}

    bool SaveExportPng(const std::wstring& customTitle){
        wchar_t filename[MAX_PATH]{};SYSTEMTIME st{};GetLocalTime(&st);swprintf_s(filename,L"鸣潮声骸记录_%04d-%02d-%02d_%02d-%02d-%02d.png",st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);
        OPENFILENAMEW ofn{sizeof(ofn)};ofn.hwndOwner=hwnd_;ofn.lpstrFilter=L"PNG 图片\0*.png\0";ofn.lpstrFile=filename;ofn.nMaxFile=MAX_PATH;ofn.lpstrDefExt=L"png";ofn.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
        if(!GetSaveFileNameW(&ofn))return false;
        ComPtr<IWICBitmap> bitmap;if(FAILED(wicFactory_->CreateBitmap(576,749,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,bitmap.GetAddressOf())))return false;
        ComPtr<ID2D1RenderTarget> rt;auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        if(FAILED(d2dFactory_->CreateWicBitmapRenderTarget(bitmap.Get(),props,rt.GetAddressOf())))return false;
        ComPtr<ID2D1SolidColorBrush> b;rt->CreateSolidColorBrush(Hex(0xffffff),b.GetAddressOf());rt->BeginDraw();rt->Clear(Hex(0x202020));
        Text(rt.Get(),b.Get(),L"声骸记录",Rect(40,40,180,28),title_.Get(),Hex(0xffffff));
        Text(rt.Get(),b.Get(),L"原生版导出",Rect(40,68,180,20),body14_.Get(),Hex(0xffffff,.60f));
        const std::array<D2D1_POINT_2F,5> pos{{{40,100},{292,100},{40,307},{292,307},{40,514}}};
        for(int i=0;i<5;++i)DrawExportCard(rt.Get(),b.Get(),slots_[i],i,pos[i].x,pos[i].y);
        FillRound(rt.Get(),b.Get(),Rect(292,514,244,155),3,Hex(0xffffff,.03f));
        Text(rt.Get(),b.Get(),L"总分",Rect(320,549,80,22),version_.Get(),Hex(0xffffff),DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(rt.Get(),b.Get(),std::to_wstring(TotalScore()),Rect(320,579,80,34),title_.Get(),Hex(0x6ccb5f),DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(rt.Get(),b.Get(),L"平均分",Rect(428,549,80,22),version_.Get(),Hex(0xffffff),DWRITE_TEXT_ALIGNMENT_CENTER);
        wchar_t avg[32]{};swprintf_s(avg,L"%.1f",TotalScore()/5.0);Text(rt.Get(),b.Get(),avg,Rect(428,579,80,34),title_.Get(),Hex(0x6ccb5f),DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(rt.Get(),b.Get(),customTitle.empty()?L"鸣潮声骸计算器 v1.3.0-native":customTitle,Rect(40,687,496,22),version_.Get(),Hex(0xffffff),DWRITE_TEXT_ALIGNMENT_TRAILING);
        if(FAILED(rt->EndDraw()))return false;
        ComPtr<IWICStream> stream;wicFactory_->CreateStream(stream.GetAddressOf());if(FAILED(stream->InitializeFromFilename(filename,GENERIC_WRITE)))return false;
        ComPtr<IWICBitmapEncoder> encoder;wicFactory_->CreateEncoder(GUID_ContainerFormatPng,nullptr,encoder.GetAddressOf());encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);
        ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> bag;encoder->CreateNewFrame(frame.GetAddressOf(),bag.GetAddressOf());frame->Initialize(bag.Get());frame->SetSize(576,749);WICPixelFormatGUID format=GUID_WICPixelFormat32bppPBGRA;frame->SetPixelFormat(&format);frame->WriteSource(bitmap.Get(),nullptr);frame->Commit();encoder->Commit();return true;
    }
    void DrawExportCard(ID2D1RenderTarget* rt,ID2D1SolidColorBrush* b,const SlotRecord& slot,int index,float x,float y){
        FillRound(rt,b,Rect(x,y,244,195),3,Hex(0xffffff,.03f));Text(rt,b,L"声骸 "+std::to_wstring(index+1),Rect(x+12,y+12,100,22),version_.Get(),Hex(0xffffff));Text(rt,b,L"小计",Rect(x+169,y+12,38,22),version_.Get(),Hex(0xffffff));Text(rt,b,FormatScore(slot.subtotal),Rect(x+205,y+12,27,22),version_.Get(),ScoreColor(slot.subtotal),DWRITE_TEXT_ALIGNMENT_TRAILING);
        for(int r=0;r<5;++r){const float ry=y+42+r*29;FillRound(rt,b,Rect(x+12,ry,220,25),1,Hex(0xffffff,.06f));const auto&sel=slot.rows[r];const auto&rule=rules_[sel.attribute];Text(rt,b,L"✦",Rect(x+18,ry,12,25),body11_.Get(),Hex(0xffffff));Text(rt,b,ShortName(rule.name),Rect(x+33,ry,120,25),body14_.Get(),Hex(0xffffff));Text(rt,b,rule.values[sel.value],Rect(x+153,ry,43,25),body14_.Get(),Hex(0xffffff),DWRITE_TEXT_ALIGNMENT_TRAILING);Text(rt,b,FormatScore(rule.scores[sel.value]),Rect(x+196,ry,30,25),body14_.Get(),ScoreColor(rule.scores[sel.value]),DWRITE_TEXT_ALIGNMENT_TRAILING);}
    }

private:
    HWND hwnd_ = nullptr;
    HWND edit_ = nullptr;
    HFONT editFont_ = nullptr;
    float dpi_ = 96.0f;
    ComPtr<ID2D1Factory> d2dFactory_;
    ComPtr<IDWriteFactory> dwriteFactory_;
    ComPtr<IWICImagingFactory> wicFactory_;
    ComPtr<ID2D1HwndRenderTarget> renderTarget_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IWICBitmap> imageWic_;
    ComPtr<ID2D1Bitmap> imageD2D_;
    UINT imageW_ = 0, imageH_ = 0;
    std::wstring imageName_;
    ComPtr<IDWriteTextFormat> title_,version_,body14_,body12_,body12Bold_,body11_,body10_,body8_,stat20_;
    std::vector<AttributeRule> rules_;
    std::array<RowSelection,5> rows_{};
    std::array<SlotRecord,5> slots_{};
    int selectedSlot_ = 0;
    int pendingSlot_ = -1;
    bool topmost_ = false;
    bool statusError_ = false;
    bool exportTitleInvalid_ = false;
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
    case WM_COMMAND:if(LOWORD(wParam)==kEditId&&HIWORD(wParam)==EN_CHANGE)g_app->EditChanged();return 0;
    case WM_DESTROY:delete g_app;g_app=nullptr;PostQuitMessage(0);return 0;
    default:return DefWindowProcW(hwnd,msg,wParam,lParam);
    }
    return DefWindowProcW(hwnd,msg,wParam,lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int showCommand){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 1;
    WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance;wc.lpfnWndProc=WindowProc;wc.lpszClassName=kWindowClass;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));wc.hIconSm=wc.hIcon;wc.hbrBackground=nullptr;
    if(!RegisterClassExW(&wc)){CoUninitialize();return 1;}
    const auto size=WindowSizeForDpi(GetDpiForSystem());const int x=std::max(0,(GetSystemMetrics(SM_CXSCREEN)-size.cx)/2),y=std::max(0,(GetSystemMetrics(SM_CYSCREEN)-size.cy)/2);
    HWND hwnd=CreateWindowExW(0,kWindowClass,kWindowTitle,kWindowStyle,x,y,size.cx,size.cy,nullptr,nullptr,instance,nullptr);if(!hwnd){CoUninitialize();return 1;}
    ShowWindow(hwnd,showCommand==SW_SHOWMINIMIZED?SW_SHOWMINIMIZED:SW_SHOWNORMAL);UpdateWindow(hwnd);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){
        if(g_app&&g_app->ExportModalOpen()&&msg.hwnd==g_app->EditHwnd()&&msg.message==WM_KEYDOWN){if(msg.wParam==VK_RETURN){g_app->ExportEnter();continue;}if(msg.wParam==VK_ESCAPE){g_app->ExportEscape();continue;}}
        TranslateMessage(&msg);DispatchMessageW(&msg);
    }
    CoUninitialize();return static_cast<int>(msg.wParam);
}
