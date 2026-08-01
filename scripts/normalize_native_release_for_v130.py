from pathlib import Path

PATH = Path('native/app_release.cpp')
text = PATH.read_text(encoding='utf-8')

multiline = '''    void OpenExport(){
        modal_=ModalKind::Export;exportTitleInvalid_=false;SetWindowTextW(edit_,L"");
        InvalidateRect(hwnd_,nullptr,FALSE);UpdateWindow(hwnd_);
        PositionEditControl();ShowWindow(edit_,SW_SHOW);SetFocus(edit_);
        RedrawWindow(edit_,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);
    }'''
normalized = '    void OpenExport(){modal_=ModalKind::Export;exportTitleInvalid_=false;SetWindowTextW(edit_,L"");PositionEditControl();ShowWindow(edit_,SW_SHOW);SetFocus(edit_);InvalidateRect(hwnd_,nullptr,FALSE);}'

multiline_count = text.count(multiline)
normalized_count = text.count(normalized)
if multiline_count == 1 and normalized_count == 0:
    text = text.replace(multiline, normalized, 1)
elif multiline_count == 0 and normalized_count == 1:
    pass
else:
    raise RuntimeError(f'OpenExport normalization state invalid: multiline={multiline_count}, normalized={normalized_count}')
PATH.write_text(text, encoding='utf-8')
print('Native app_release.cpp is normalized for v1.3.0 patching')
