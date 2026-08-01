from pathlib import Path

SOURCE = Path('native/app_final.cpp')
TARGET = Path('native/app_release.cpp')
text = SOURCE.read_text(encoding='utf-8')


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected one match, found {count}')
    text = text.replace(old, new, 1)


# Figma input text area: x=12, y=6, right=21, height=20 within a 336x32 frame.
replace_once(
    'const int height = -MulDiv(13, static_cast<int>(dpi_), 96);',
    'const int height = -MulDiv(14, static_cast<int>(dpi_), 96);',
    'edit font size',
)
replace_once('SetBkColor(dc, RGB(21,21,21));', 'SetBkColor(dc, RGB(32,32,32));', 'edit background color')
replace_once('editBrush_ = CreateSolidBrush(RGB(21,21,21));', 'editBrush_ = CreateSolidBrush(RGB(32,32,32));', 'edit brush color')
replace_once(
    '''        SetWindowPos(edit_, HWND_TOP, px(r.left + 8), px(r.top + 5), px(r.right-r.left-16), px(r.bottom-r.top-10),
                     SWP_SHOWWINDOW | SWP_NOACTIVATE);
        RedrawWindow(edit_,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);''',
    '''        SetWindowPos(edit_, HWND_TOP, px(r.left + 12), px(r.top + 6), px(r.right-r.left-33), px(20.0f),
                     SWP_SHOWWINDOW | SWP_NOACTIVATE);
        RedrawWindow(edit_,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);''',
    'edit text bounds',
)

# Reproduce the supplied 384x198 Figma modal. The length warning exists only
# while the weighted title length exceeds 12 characters.
replace_once(
    'Text(rt,b,L"导出记录",Rect(426,307,336,28),title_.Get(),Hex(0xffffff));',
    'Text(rt,b,L"导出记录",Rect(426,311,336,28),title_.Get(),Hex(0xffffff));',
    'export title position',
)
replace_once(
    '''            const auto input=ExportInputRect();
            FillRound(rt,b,input,4,Hex(0x151515));
            StrokeRound(rt,b,input,4,Hex(0xffffff,.10f));
            Fill(rt,b,Rect(input.left,input.bottom-1,input.right-input.left,1),exportTitleInvalid_?Hex(0xfce100):Hex(0x4cc2ff));
            const auto hint=Rect(438,375,292,20);
            FillRound(rt,b,hint,3,Hex(0x454545));
            Text(rt,b,L"最多可输入12个字符，超过最大长度限制，请重新编辑",hint,body10_.Get(),exportTitleInvalid_?Hex(0xfce100):Hex(0xffffff,.45f),DWRITE_TEXT_ALIGNMENT_CENTER);''',
    '''            const auto input=ExportInputRect();
            const auto inputBase=Rect(input.left,input.top+1,input.right-input.left,30);
            FillRound(rt,b,inputBase,4,Hex(0x000000,.30f));
            StrokeRound(rt,b,inputBase,4,Hex(0xffffff,.10f));
            Fill(rt,b,Rect(input.left,input.top+30,input.right-input.left,1),Hex(0x4cc2ff));
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
            }''',
    'figma export input and warning',
)

# The UI icon bitmap belongs to the HWND render target. Reusing it on the WIC
# export target causes D2DERR_WRONG_RESOURCE_DOMAIN at EndDraw. Keep the window
# card icon, and use a target-independent text glyph in the exported image.
replace_once(
    'DrawIcon(rt,iconStar_.Get(),Rect(x+18,ry+7,11,11));',
    'Text(rt,b,L"✦",Rect(x+18,ry,12,25),body11_.Get(),Hex(0xffffff));',
    'export star resource domain',
)
replace_once(
    '''        hr=rt->EndDraw();
        if(FAILED(hr)){exportError_=L"绘制导出图片失败";return false;}
        rt.Reset();b.Reset();''',
    '''        hr=rt->EndDraw();
        if(FAILED(hr)){
            wchar_t code[32]{};
            swprintf_s(code,L"（0x%08X）",static_cast<unsigned int>(hr));
            exportError_=L"绘制导出图片失败"+std::wstring(code);
            return false;
        }
        b.Reset();rt.Reset();''',
    'export draw diagnostic and release order',
)

if 'DrawIcon(rt,iconStar_.Get(),Rect(x+18,ry+7,11,11));' in text:
    raise RuntimeError('export card still references the HWND icon bitmap')

# The following v1.3.0 patch replaces this method again and expects its compact
# historical form. Normalize it here so every build entry uses the same chain.
multiline_open_export = '''    void OpenExport(){
        modal_=ModalKind::Export;exportTitleInvalid_=false;SetWindowTextW(edit_,L"");
        InvalidateRect(hwnd_,nullptr,FALSE);UpdateWindow(hwnd_);
        PositionEditControl();ShowWindow(edit_,SW_SHOW);SetFocus(edit_);
        RedrawWindow(edit_,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);
    }'''
compact_open_export = '    void OpenExport(){modal_=ModalKind::Export;exportTitleInvalid_=false;SetWindowTextW(edit_,L"");PositionEditControl();ShowWindow(edit_,SW_SHOW);SetFocus(edit_);InvalidateRect(hwnd_,nullptr,FALSE);}'
replace_once(multiline_open_export, compact_open_export, 'v1.3.0 export initializer normalization')

TARGET.write_text(text, encoding='utf-8')
print(f'Generated {TARGET} ({len(text)} chars)')
