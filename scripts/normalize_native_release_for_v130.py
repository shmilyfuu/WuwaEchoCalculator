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

count = text.count(multiline)
if count != 1:
    raise RuntimeError(f'OpenExport normalization expected one match, found {count}')
text = text.replace(multiline, normalized, 1)
PATH.write_text(text, encoding='utf-8')
print('Normalized native/app_release.cpp for v1.3.0 patching')
