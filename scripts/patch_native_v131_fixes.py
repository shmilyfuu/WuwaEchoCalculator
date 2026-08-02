from pathlib import Path

SOURCE = Path('native/app_v130_update.cpp')
TARGET = Path('native/app_v131.cpp')
text = SOURCE.read_text(encoding='utf-8')


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected one match, found {count}')
    text = text.replace(old, new, 1)


# A newly imported image must not display the previous image's selections as
# manual edits while the background OCR job is still running.
replace_once(
    'ocrCancel_.store(false);ocrRunning_=true;statusError_=false;status_=L"正在识别声骸属性";rowConfidence_.fill(0);InvalidateRect(hwnd_,nullptr,FALSE);',
    'ocrCancel_.store(false);ocrRunning_=true;rows_={};rowConfidence_.fill(0);statusError_=false;status_=L"正在识别声骸属性";InvalidateRect(hwnd_,nullptr,FALSE);',
    'clear review rows before OCR',
)

# Clearing every record starts a fresh five-card sequence at card 1.
replace_once(
    'if(confirmAction_==ConfirmAction::ClearAll){for(auto&s:slots_)s={};status_=L"已清空全部记录";statusError_=false;}',
    'if(confirmAction_==ConfirmAction::ClearAll){for(auto&s:slots_)s={};selectedSlot_=0;status_=L"已清空全部记录";statusError_=false;}',
    'reset record target after clear all',
)

# The PNG encoder may return a compatible unpremultiplied pixel format after
# SetPixelFormat. Convert to the exact format accepted by the encoder instead
# of requiring it to preserve 32bppPBGRA.
replace_once(
    '''        WICPixelFormatGUID format=GUID_WICPixelFormat32bppPBGRA;hr=frame->SetPixelFormat(&format);if(FAILED(hr)||format!=GUID_WICPixelFormat32bppPBGRA){exportError_=L"PNG 像素格式转换失败";return false;}
        if(FAILED(frame->WriteSource(bitmap.Get(),nullptr))||FAILED(frame->Commit())||FAILED(encoder->Commit())){exportError_=L"完成 PNG 文件失败";return false;}''',
    '''        WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
        hr=frame->SetPixelFormat(&format);
        if(FAILED(hr)){exportError_=L"PNG 像素格式设置失败";return false;}
        ComPtr<IWICBitmapSource> encoderSource;
        hr=WICConvertBitmapSource(format,bitmap.Get(),encoderSource.GetAddressOf());
        if(FAILED(hr)){
            wchar_t code[32]{};swprintf_s(code,L"（0x%08X）",static_cast<unsigned int>(hr));
            exportError_=L"PNG 像素格式转换失败"+std::wstring(code);return false;
        }
        if(FAILED(frame->WriteSource(encoderSource.Get(),nullptr))||FAILED(frame->Commit())||FAILED(encoder->Commit())){exportError_=L"完成 PNG 文件失败";return false;}''',
    'encoder accepted PNG pixel format',
)

# Public v1.3.1 version used by the title bar, update comparison and export.
text = text.replace('1.3.0', '1.3.1')
if '1.3.0' in text:
    raise RuntimeError('old v1.3.0 literal remains in generated application')
if 'WICConvertBitmapSource' not in text:
    raise RuntimeError('PNG pixel conversion fix is missing')
if 'selectedSlot_=0;status_=L"已清空全部记录"' not in text:
    raise RuntimeError('clear-all slot reset is missing')
if 'ocrRunning_=true;rows_={};rowConfidence_.fill(0)' not in text:
    raise RuntimeError('OCR transient manual-state fix is missing')

TARGET.write_text(text, encoding='utf-8')
print(f'Generated {TARGET} ({len(text)} chars)')
