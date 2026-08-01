from pathlib import Path

SOURCE = Path('native/app.cpp')
TARGET = Path('native/app_generated.cpp')
text = SOURCE.read_text(encoding='utf-8')


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected one match, found {count}')
    text = text.replace(old, new, 1)


replace_once('#include <utility>\n', '#include <utility>\n#include "icon_assets.h"\n', 'icon include')
replace_once('    ~App() { if (editFont_) DeleteObject(editFont_); }',
             '    ~App() { if (editFont_) DeleteObject(editFont_); if (editBrush_) DeleteObject(editBrush_); }',
             'destructor')
replace_once('        imageD2D_.Reset();\n        brush_.Reset();',
             '        imageD2D_.Reset();\n        iconArrow_.Reset(); iconStar_.Reset(); iconNormal_.Reset(); iconAbnormal_.Reset(); iconManual_.Reset();\n        brush_.Reset();',
             'device reset')
replace_once('        RecreateD2DBitmap();\n        return S_OK;',
             '        RecreateD2DBitmap();\n        LoadUiIcons();\n        return S_OK;',
             'load icons')

replace_once('''    HBRUSH EditColor(HDC dc) {
        SetTextColor(dc, RGB(255,255,255));
        SetBkMode(dc, TRANSPARENT);
        return static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    }''', '''    HBRUSH EditColor(HDC dc) {
        SetTextColor(dc, RGB(255,255,255));
        SetBkColor(dc, RGB(21,21,21));
        SetBkMode(dc, OPAQUE);
        return editBrush_;
    }''', 'edit color')

replace_once('''    void CreateEditControl() {
        edit_ = CreateWindowExW(WS_EX_TRANSPARENT, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
                                0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kEditId),
                                GetModuleHandleW(nullptr), nullptr);
        SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(10,10));
        UpdateEditFont();
    }''', '''    void CreateEditControl() {
        editBrush_ = CreateSolidBrush(RGB(21,21,21));
        edit_ = CreateWindowExW(WS_EX_NOPARENTNOTIFY, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL | ES_NOHIDESEL,
                                0,0,0,0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditId)),
                                GetModuleHandleW(nullptr), nullptr);
        SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(9,9));
        SendMessageW(edit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"请输入标题（可选）"));
        UpdateEditFont();
    }''', 'edit creation')

replace_once('''        SetWindowPos(edit_, HWND_TOP, px(r.left + 2), px(r.top + 1), px(r.right-r.left-4), px(r.bottom-r.top-2),
                     SWP_SHOWWINDOW | SWP_NOACTIVATE);''', '''        SetWindowPos(edit_, HWND_TOP, px(r.left + 1), px(r.top + 1), px(r.right-r.left-2), px(r.bottom-r.top-2),
                     SWP_SHOWWINDOW | SWP_NOACTIVATE);''', 'edit position')

replace_once('''    void DrawTopBar(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {
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
    }''', '''    void DrawTopBar(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {
        Text(rt,b,L"鸣潮声骸计算器",Rect(20,20,180,28),title_.Get(),Hex(0xffffff));
        Text(rt,b,L"v1.3.0-native",Rect(200,22,130,24),version_.Get(),Hex(0xffffff));
        Text(rt,b,L"鸣潮声骸的本地识别评分工具",Rect(20,48,260,20),body14_.Get(),Hex(0xffffff,.60f));
        Text(rt,b,L"置顶",Rect(1094,20,32,20),body14_.Get(),Hex(0xffffff));
        const auto track = Rect(1130,21,38,18);
        FillRound(rt,b,track,9, topmost_ ? Hex(0x4cc2ff) : Hex(0x000000,.10f));
        StrokeRound(rt,b,track,9,Hex(0xffffff,.06f));
        const float knobX = topmost_ ? 1153.0f : 1133.0f;
        b->SetColor(topmost_ ? Hex(0x000000) : Hex(0xffffff,.60f));
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX+6,30),6,6),b);
    }''', 'top bar')

replace_once('''            b->SetColor(rows_[i].attribute>=0?Hex(0xff99a4):Hex(0xffffff,.25f));
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(783,y+23),4,4),b);''', '''            if(rows_[i].attribute>=0) DrawIcon(rt,iconManual_.Get(),Rect(773,y+17,20,12));
            else { b->SetColor(Hex(0xffffff,.25f)); rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(783,y+23),4,4),b); }''', 'row status icon')

replace_once('''    void DrawSelect(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const D2D1_RECT_F& r, const std::wstring& value, bool enabled) {
        FillRound(rt,b,r,4, enabled?Hex(0x202020):Hex(0x202020,.55f));
        Text(rt,b,value,Rect(r.left+11,r.top,r.right-r.left-34,r.bottom-r.top),body14_.Get(),enabled?Hex(0xffffff):Hex(0xffffff,.35f));
        b->SetColor(enabled?Hex(0xffffff,.75f):Hex(0xffffff,.25f));
        const float cx=r.right-15,cy=(r.top+r.bottom)/2;
        rt->DrawLine(D2D1::Point2F(cx-4,cy-2),D2D1::Point2F(cx,cy+2),b,1.2f);
        rt->DrawLine(D2D1::Point2F(cx,cy+2),D2D1::Point2F(cx+4,cy-2),b,1.2f);
    }''', '''    void DrawSelect(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const D2D1_RECT_F& r, const std::wstring& value, bool enabled) {
        FillRound(rt,b,r,4, enabled?Hex(0x202020):Hex(0x202020,.55f));
        Text(rt,b,value,Rect(r.left+11,r.top,r.right-r.left-34,r.bottom-r.top),body14_.Get(),enabled?Hex(0xffffff):Hex(0xffffff,.35f));
        DrawIcon(rt,iconArrow_.Get(),Rect(r.right-20,(r.top+r.bottom)/2-5,10,10),enabled?1.0f:.35f);
    }''', 'select arrow')

text = text.replace('DrawMetric(rt,b,852,128,', 'DrawMetric(rt,b,858,133,')
text = text.replace('DrawMetric(rt,b,956,128,', 'DrawMetric(rt,b,962,133,')
text = text.replace('DrawMetric(rt,b,1060,128,', 'DrawMetric(rt,b,1066,133,')
text = text.replace('Rect(960,228,96,30)', 'Rect(968,228,96,30)')
text = text.replace('Rect(1064,228,96,30)', 'Rect(1072,228,96,30)')

replace_once('''        b->SetColor(Hex(0x6ccb5f));rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(855,420),7,7),b);
        Text(rt,b,L"识别准确",Rect(867,409,65,22),body10_.Get(),Hex(0xffffff,.60f));
        b->SetColor(Hex(0xfce100));rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(952,420),7,7),b);
        Text(rt,b,L"建议核对",Rect(964,409,65,22),body10_.Get(),Hex(0xffffff,.60f));
        FillRound(rt,b,Rect(1043,414,20,12),2,Hex(0xff99a4));
        Text(rt,b,L"手动选择",Rect(1068,409,72,22),body10_.Get(),Hex(0xffffff,.60f));''', '''        DrawIcon(rt,iconNormal_.Get(),Rect(848,413,14,14));
        Text(rt,b,L"识别准确",Rect(867,409,65,22),body10_.Get(),Hex(0xffffff,.60f));
        DrawIcon(rt,iconAbnormal_.Get(),Rect(945,413,14,14));
        Text(rt,b,L"建议核对",Rect(964,409,65,22),body10_.Get(),Hex(0xffffff,.60f));
        DrawIcon(rt,iconManual_.Get(),Rect(1043,414,20,12));
        Text(rt,b,L"手动选择",Rect(1068,409,72,22),body10_.Get(),Hex(0xffffff,.60f));''', 'guide icons')

text = text.replace('Text(rt,b,L"✦",Rect(x+18,ry,12,23),body11_.Get(),Hex(0xffffff));',
                    'DrawIcon(rt,iconStar_.Get(),Rect(x+18,ry+6,11,11));')
text = text.replace('Text(rt,b,L"✦",Rect(x+18,ry,12,25),body11_.Get(),Hex(0xffffff));',
                    'DrawIcon(rt,iconStar_.Get(),Rect(x+18,ry+7,11,11));')

modal_start = text.index('    void DrawModal(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {')
modal_end = text.index('\n    D2D1_RECT_F ExportInputRect()', modal_start)
new_modal = '''    void DrawModal(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {
        Fill(rt,b,Rect(0,0,kClientWidth,kClientHeight),Hex(0x000000,.55f));
        if(modal_==ModalKind::Confirm){
            const auto box=Rect(402,287,384,198);
            FillRound(rt,b,box,7,Hex(0x202020));
            FillRound(rt,b,Rect(402,287,384,108),7,Hex(0x2d2d2d));
            Fill(rt,b,Rect(402,294,384,101),Hex(0x2d2d2d));
            Text(rt,b,confirmTitle_,Rect(426,311,336,28),title_.Get(),Hex(0xffffff));
            Text(rt,b,confirmMessage_,Rect(426,351,336,20),body14_.Get(),Hex(0xffffff));
            DrawButton(rt,b,ControlId::ModalAccept,Rect(562,419,96,30),confirmAcceptText_,ButtonKind::Blue,true);
            DrawButton(rt,b,ControlId::ModalCancel,Rect(666,419,96,30),L"取消",ButtonKind::Gray,true);
        } else {
            const auto box=Rect(402,287,384,198);
            FillRound(rt,b,box,7,Hex(0x202020));
            FillRound(rt,b,Rect(402,287,384,120),7,Hex(0x2d2d2d));
            Fill(rt,b,Rect(402,294,384,113),Hex(0x2d2d2d));
            Text(rt,b,L"导出记录",Rect(426,307,336,28),title_.Get(),Hex(0xffffff));
            const auto input=ExportInputRect();
            FillRound(rt,b,input,4,Hex(0x151515));
            StrokeRound(rt,b,input,4,Hex(0xffffff,.10f));
            Fill(rt,b,Rect(input.left,input.bottom-1,input.right-input.left,1),exportTitleInvalid_?Hex(0xfce100):Hex(0x4cc2ff));
            const auto hint=Rect(438,375,292,20);
            FillRound(rt,b,hint,3,Hex(0x454545));
            Text(rt,b,L"最多可输入12个字符，超过最大长度限制，请重新编辑",hint,body10_.Get(),exportTitleInvalid_?Hex(0xfce100):Hex(0xffffff,.45f),DWRITE_TEXT_ALIGNMENT_CENTER);
            DrawButton(rt,b,ControlId::ModalAccept,Rect(562,431,96,30),L"确定",ButtonKind::Blue,true);
            DrawButton(rt,b,ControlId::ModalCancel,Rect(666,431,96,30),L"取消",ButtonKind::Gray,true);
        }
    }
'''
text = text[:modal_start] + new_modal + text[modal_end:]

text = text.replace('if(Contains(Rect(1110, 20, 58, 20), x, y))', 'if(Contains(Rect(1094,20,74,20),x,y))')
text = text.replace('if(Contains(Rect(562,425,96,30),x,y))return ControlId::ModalAccept;', 'if(Contains(Rect(562,419,96,30),x,y))return ControlId::ModalAccept;')
text = text.replace('if(Contains(Rect(666,425,96,30),x,y))return ControlId::ModalCancel;', 'if(Contains(Rect(666,419,96,30),x,y))return ControlId::ModalCancel;')

replace_once('''    void RecreateD2DBitmap(){imageD2D_.Reset();if(renderTarget_&&imageWic_)renderTarget_->CreateBitmapFromWicBitmap(imageWic_.Get(),nullptr,imageD2D_.GetAddressOf());}
''', '''    void RecreateD2DBitmap(){imageD2D_.Reset();if(renderTarget_&&imageWic_)renderTarget_->CreateBitmapFromWicBitmap(imageWic_.Get(),nullptr,imageD2D_.GetAddressOf());}
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
''', 'icon helpers')

replace_once('''    HFONT editFont_ = nullptr;
    float dpi_ = 96.0f;''', '''    HFONT editFont_ = nullptr;
    HBRUSH editBrush_ = nullptr;
    float dpi_ = 96.0f;''', 'edit brush field')
replace_once('''    ComPtr<ID2D1Bitmap> imageD2D_;
    UINT imageW_ = 0, imageH_ = 0;''', '''    ComPtr<ID2D1Bitmap> imageD2D_;
    ComPtr<ID2D1Bitmap> iconArrow_,iconStar_,iconNormal_,iconAbnormal_,iconManual_;
    UINT imageW_ = 0, imageH_ = 0;''', 'icon fields')

TARGET.write_text(text,encoding='utf-8')
print(f'Generated {TARGET} ({len(text)} chars)')
