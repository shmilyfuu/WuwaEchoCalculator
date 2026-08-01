from pathlib import Path
import re

SOURCE = Path('native/app_release.cpp')
TARGET = Path('native/app_v130_ui.cpp')
text = SOURCE.read_text(encoding='utf-8')


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected one match, found {count}')
    text = text.replace(old, new, 1)


def replace_block(start: str, next_start: str, replacement: str, label: str) -> None:
    global text
    begin = text.find(start)
    if begin < 0:
        raise RuntimeError(f'{label}: start marker missing')
    end = text.find(next_start, begin + len(start))
    if end < 0:
        raise RuntimeError(f'{label}: end marker missing')
    text = text[:begin] + replacement.rstrip() + '\n\n' + text[end:]


# Public version and new top-bar controls.
replace_once(
    'constexpr wchar_t kWindowTitle[] = L"鸣潮声骸计算器 · 原生预览";',
    'constexpr wchar_t kWindowTitle[] = L"鸣潮声骸计算器";\nconstexpr wchar_t kAppVersion[] = L"1.3.0";',
    'application version',
)
replace_once(
    'None, DropZone, Stop, Again, Record, Clear, Export, ModalAccept, ModalCancel,',
    'None, DropZone, Stop, Again, Record, Clear, Export, Settings, Update, ModalAccept, ModalCancel,',
    'top bar control ids',
)

# Add a 24px export metric format.
replace_once(
    'CreateFormat(20, DWRITE_FONT_WEIGHT_SEMI_BOLD, stat20_.GetAddressOf());',
    'CreateFormat(20, DWRITE_FONT_WEIGHT_SEMI_BOLD, stat20_.GetAddressOf());\n        CreateFormat(24, DWRITE_FONT_WEIGHT_SEMI_BOLD, exportStat24_.GetAddressOf());',
    'export metric format creation',
)
replace_once(
    'ComPtr<IDWriteTextFormat> title_,version_,body14_,body12_,body12Bold_,body11_,body10_,body8_,stat20_;',
    'ComPtr<IDWriteTextFormat> title_,version_,body14_,body12_,body12Bold_,body11_,body10_,body8_,stat20_,exportStat24_;',
    'export metric format field',
)

# Keep the real EDIT control and add a reliable placeholder painted by its subclass.
replace_once('#include <utility>\n', '#include <utility>\n#include <chrono>\n', 'chrono include')
replace_once(
    'using Microsoft::WRL::ComPtr;\n\nnamespace {',
    '''using Microsoft::WRL::ComPtr;

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
}''',
    'edit subclass function',
)
replace_once(
    'SendMessageW(edit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"请输入标题（可选）"));\n        UpdateEditFont();',
    'SendMessageW(edit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"请输入标题（可选）"));\n        g_originalEditProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(edit_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ExportEditProc)));\n        UpdateEditFont();',
    'edit subclass installation',
)
replace_once(
    'void EditChanged() {\n        if (modal_ != ModalKind::Export) return;\n        exportTitleInvalid_ = WeightedTitleLength(GetEditText()) > 12.0f;\n        InvalidateRect(hwnd_, nullptr, FALSE);\n    }',
    '''void EditChanged() {
        if (modal_ != ModalKind::Export) return;
        exportTitleInvalid_ = WeightedTitleLength(GetEditText()) > 12.0f;
        InvalidateRect(edit_, nullptr, TRUE);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void EditFocusChanged(bool focused) {
        exportEditFocused_ = focused;
        InvalidateRect(edit_, nullptr, TRUE);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }''',
    'edit focus state',
)
replace_once(
    'case WM_COMMAND:if(LOWORD(wParam)==kEditId&&HIWORD(wParam)==EN_CHANGE)g_app->EditChanged();return 0;',
    '''case WM_COMMAND:
        if(LOWORD(wParam)==kEditId){
            if(HIWORD(wParam)==EN_CHANGE)g_app->EditChanged();
            else if(HIWORD(wParam)==EN_SETFOCUS)g_app->EditFocusChanged(true);
            else if(HIWORD(wParam)==EN_KILLFOCUS)g_app->EditFocusChanged(false);
        }
        return 0;''',
    'edit command handling',
)
replace_once(
    'bool exportTitleInvalid_ = false;',
    'bool exportTitleInvalid_ = false;\n    bool exportEditFocused_ = false;\n    bool updateReady_ = false;\n    bool updateBusy_ = false;',
    'new ui state fields',
)

# Exact Figma top bar layout, including disabled Settings and the future update entry.
replace_block(
    '    void DrawTopBar(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {',
    '    void DrawHeading(',
    '''    void DrawTopBar(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {
        Text(rt,b,L"鸣潮声骸计算器",Rect(20,20,140,28),title_.Get(),Hex(0xffffff));
        Text(rt,b,L"v1.3.0",Rect(165,26,60,22),version_.Get(),Hex(0xffffff));
        Text(rt,b,L"鸣潮声骸的本地识别评分工具",Rect(20,48,220,20),body14_.Get(),Hex(0xffffff,.60f));
        DrawButton(rt,b,ControlId::Settings,Rect(882,15,96,30),L"设置",ButtonKind::Gray,false);
        DrawButton(rt,b,ControlId::Update,Rect(986,15,96,30),updateReady_?L"更新":updateBusy_?L"检查中":L"检查更新",updateReady_?ButtonKind::Blue:ButtonKind::Gray,!updateBusy_);
        Text(rt,b,L"置顶",Rect(1094,20,28,20),body14_.Get(),Hex(0xffffff));
        const auto track = Rect(1130,21,38,18);
        FillRound(rt,b,track,9, topmost_ ? Hex(0x4cc2ff) : Hex(0x000000,.10f));
        StrokeRound(rt,b,track,9,Hex(0xffffff,.06f));
        const float knobX = topmost_ ? 1153.0f : 1133.0f;
        b->SetColor(topmost_ ? Hex(0x000000) : Hex(0xffffff,.60f));
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX+6,30),6,6),b);
    }''',
    'top bar layout',
)

# Review panel: 3% row layer plus 6% child controls, exact Figma coordinates.
replace_block(
    '    void DrawReview(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {',
    '    void DrawSelect(',
    '''    void DrawReview(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {
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
    }''',
    'review panel layout',
)
replace_block(
    '    void DrawSelect(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const D2D1_RECT_F& r, const std::wstring& value, bool enabled) {',
    '    void DrawSide(',
    '''    void DrawSelect(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, const D2D1_RECT_F& r, const std::wstring& value, bool enabled) {
        FillRound(rt,b,r,3,Hex(0xffffff,.06f));
        Text(rt,b,value,Rect(r.left+10,r.top,r.right-r.left-34,r.bottom-r.top),body14_.Get(),enabled?Hex(0xffffff):Hex(0xffffff,.35f));
        DrawIcon(rt,iconArrow_.Get(),Rect(r.right-20,(r.top+r.bottom)/2-5,10,10),enabled?1.0f:.35f);
    }''',
    'select appearance',
)

# Mouse and hit-test coordinates for the exact controls.
text = text.replace('Rect(416, y0 + 7, 170, 32)', 'Rect(416, y0 + 8, 168, 30)')
text = text.replace('Rect(592, y0 + 7, 98, 32)', 'Rect(592, y0 + 8, 96, 30)')
text = text.replace('const auto slotRect = Rect(527, 410, 170, 30);', 'const auto slotRect = Rect(534, 410, 168, 30);')
text = text.replace('if(Contains(Rect(706,410,96,30),x,y))return ControlId::Record;', 'if(Contains(Rect(710,410,96,30),x,y))return ControlId::Record;')
replace_once(
    'if(Contains(Rect(1072,228,96,30),x,y))return ControlId::Export;',
    'if(Contains(Rect(1072,228,96,30),x,y))return ControlId::Export;\n        if(Contains(Rect(882,15,96,30),x,y))return ControlId::Settings;\n        if(Contains(Rect(986,15,96,30),x,y))return ControlId::Update;',
    'top bar hit tests',
)
replace_once(
    'if(id==ControlId::Export)return RecordCount()==5;',
    'if(id==ControlId::Export)return RecordCount()==5;\n        if(id==ControlId::Settings)return false;\n        if(id==ControlId::Update)return !updateBusy_;',
    'top bar enabled state',
)
replace_once(
    'if(id==ControlId::Export){OpenExport();return;}',
    'if(id==ControlId::Export){OpenExport();return;}\n        if(id==ControlId::Update){updateBusy_=true;statusError_=false;status_=L"正在检查更新";InvalidateRect(hwnd_,nullptr,FALSE);return;}',
    'temporary update activation',
)

# Record cards: exact header and fixed numeric columns with room for signed two-digit scores.
replace_block(
    '    void DrawRecords(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {',
    '    void DrawStatus(',
    '''    void DrawRecords(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {
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
                Text(rt,b,FormatScore(rule.scores[sel.value]),Rect(x+182,ry,26,23),body12_.Get(),ScoreColor(rule.scores[sel.value]),DWRITE_TEXT_ALIGNMENT_TRAILING);
            }
            DrawButton(rt,b,static_cast<ControlId>(static_cast<int>(ControlId::CardEdit0)+i),Rect(x+12,y+176,94,24),L"编辑",ButtonKind::Gray,true,12);
            DrawButton(rt,b,static_cast<ControlId>(static_cast<int>(ControlId::CardDelete0)+i),Rect(x+114,y+176,94,24),L"删除",ButtonKind::Red,true,12);
        }
    }''',
    'record card layout',
)

# Successful new records advance to the next empty slot; overwrites keep the current slot.
replace_once(
    'void SaveCurrentToSlot(int slot){slots_[slot].used=true;slots_[slot].rows=rows_;slots_[slot].subtotal=CurrentSubtotal();statusError_=false;status_=L"已记录到声骸 "+std::to_wstring(slot+1);}',
    '''void SaveCurrentToSlot(int slot){
        const bool wasUsed=slots_[slot].used;
        slots_[slot].used=true;slots_[slot].rows=rows_;slots_[slot].subtotal=CurrentSubtotal();
        statusError_=false;status_=L"已记录到声骸 "+std::to_wstring(slot+1);
        if(!wasUsed){
            for(int offset=1;offset<=5;++offset){
                const int candidate=(slot+offset)%5;
                if(!slots_[candidate].used){selectedSlot_=candidate;break;}
            }
        }
    }''',
    'record auto advance',
)

# Export modal focus line and transient validation warning.
replace_once(
    'Fill(rt,b,Rect(input.left,input.top+30,input.right-input.left,1),Hex(0x4cc2ff));',
    'if(exportEditFocused_) Fill(rt,b,Rect(input.left,input.top+30,input.right-input.left,2),exportTitleInvalid_?Hex(0xfce100):Hex(0x4cc2ff));',
    'focus-only input underline',
)
replace_once(
    'void OpenExport(){modal_=ModalKind::Export;exportTitleInvalid_=false;SetWindowTextW(edit_,L"");PositionEditControl();ShowWindow(edit_,SW_SHOW);SetFocus(edit_);InvalidateRect(hwnd_,nullptr,FALSE);}',
    'void OpenExport(){modal_=ModalKind::Export;exportTitleInvalid_=false;exportEditFocused_=true;SetWindowTextW(edit_,L"");PositionEditControl();ShowWindow(edit_,SW_SHOW);SetFocus(edit_);InvalidateRect(edit_,nullptr,TRUE);InvalidateRect(hwnd_,nullptr,FALSE);}',
    'open export focus',
)
replace_once(
    'void CloseModal(){modal_=ModalKind::None;confirmAction_=ConfirmAction::None;ShowWindow(edit_,SW_HIDE);SetFocus(hwnd_);InvalidateRect(hwnd_,nullptr,FALSE);}',
    'void CloseModal(){modal_=ModalKind::None;confirmAction_=ConfirmAction::None;exportEditFocused_=false;ShowWindow(edit_,SW_HIDE);SetFocus(hwnd_);InvalidateRect(hwnd_,nullptr,FALSE);}',
    'close modal focus',
)

# Build a target-local star bitmap for the WIC export target.
export_methods = r'''    ComPtr<ID2D1Bitmap> CreateEmbeddedBitmap(ID2D1RenderTarget* target,const char* encoded){
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
        Text(rt.Get(),b.Get(),customTitle.empty()?L"鸣潮声骸计算器 v1.3.0":customTitle,Rect(40,687,496,22),version_.Get(),Hex(0xffffff),DWRITE_TEXT_ALIGNMENT_TRAILING);
        hr=rt->EndDraw();
        if(FAILED(hr)){wchar_t code[32]{};swprintf_s(code,L"（0x%08X）",static_cast<unsigned int>(hr));exportError_=L"绘制导出图片失败"+std::wstring(code);return false;}
        star.Reset();b.Reset();rt.Reset();
        ComPtr<IWICStream> stream;hr=wicFactory_->CreateStream(stream.GetAddressOf());if(FAILED(hr)){exportError_=L"无法创建输出流";return false;}
        hr=stream->InitializeFromFilename(filename,GENERIC_WRITE);if(FAILED(hr)){exportError_=L"无法写入所选路径";return false;}
        ComPtr<IWICBitmapEncoder> encoder;hr=wicFactory_->CreateEncoder(GUID_ContainerFormatPng,nullptr,encoder.GetAddressOf());if(FAILED(hr)){exportError_=L"无法创建 PNG 编码器";return false;}
        hr=encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);if(FAILED(hr)){exportError_=L"无法初始化 PNG 编码器";return false;}
        ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> bag;hr=encoder->CreateNewFrame(frame.GetAddressOf(),bag.GetAddressOf());if(FAILED(hr)){exportError_=L"无法创建 PNG 帧";return false;}
        if(FAILED(frame->Initialize(bag.Get()))||FAILED(frame->SetSize(576,749))){exportError_=L"无法初始化 PNG 帧";return false;}
        WICPixelFormatGUID format=GUID_WICPixelFormat32bppPBGRA;hr=frame->SetPixelFormat(&format);if(FAILED(hr)||format!=GUID_WICPixelFormat32bppPBGRA){exportError_=L"PNG 像素格式转换失败";return false;}
        if(FAILED(frame->WriteSource(bitmap.Get(),nullptr))||FAILED(frame->Commit())||FAILED(encoder->Commit())){exportError_=L"完成 PNG 文件失败";return false;}
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
    }'''
replace_block(
    '    bool SaveExportPng(const std::wstring& customTitle){',
    '\nprivate:\n    HWND hwnd_',
    export_methods,
    'export renderer',
)

TARGET.write_text(text, encoding='utf-8')
print(f'Generated {TARGET} ({len(text)} chars)')
