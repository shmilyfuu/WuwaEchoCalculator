from pathlib import Path

SOURCE = Path('native/app_ocr.cpp')
TARGET = Path('native/app_final.cpp')
text = SOURCE.read_text(encoding='utf-8')


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected one match, found {count}')
    text = text.replace(old, new, 1)


# Keep the real Win32 edit inside the Direct2D frame. The previous child window
# covered almost the entire fake input, including its rounded edge and underline.
replace_once(
    'SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(9,9));',
    'SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0,0));',
    'edit margins',
)
replace_once(
    'const int height = -MulDiv(14, static_cast<int>(dpi_), 96);',
    'const int height = -MulDiv(13, static_cast<int>(dpi_), 96);',
    'edit font size',
)
replace_once(
    '''        SetWindowPos(edit_, HWND_TOP, px(r.left + 1), px(r.top + 1), px(r.right-r.left-2), px(r.bottom-r.top-2),
                     SWP_SHOWWINDOW | SWP_NOACTIVATE);''',
    '''        SetWindowPos(edit_, HWND_TOP, px(r.left + 8), px(r.top + 5), px(r.right-r.left-16), px(r.bottom-r.top-10),
                     SWP_SHOWWINDOW | SWP_NOACTIVATE);
        RedrawWindow(edit_,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);''',
    'edit position',
)
replace_once(
    '''    void OpenExport(){modal_=ModalKind::Export;exportTitleInvalid_=false;SetWindowTextW(edit_,L"");PositionEditControl();ShowWindow(edit_,SW_SHOW);SetFocus(edit_);InvalidateRect(hwnd_,nullptr,FALSE);}''',
    '''    void OpenExport(){
        modal_=ModalKind::Export;exportTitleInvalid_=false;SetWindowTextW(edit_,L"");
        InvalidateRect(hwnd_,nullptr,FALSE);UpdateWindow(hwnd_);
        PositionEditControl();ShowWindow(edit_,SW_SHOW);SetFocus(edit_);
        RedrawWindow(edit_,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);
    }''',
    'open export ordering',
)

# Report the actual encoder failure instead of a generic message.
replace_once(
    '''            if(SaveExportPng(GetEditText())){statusError_=false;status_=L"记录图片已导出";}else{statusError_=true;status_=L"导出已取消或保存失败";}''',
    '''            if(SaveExportPng(GetEditText())){statusError_=false;status_=L"记录图片已导出";}else{statusError_=true;status_=exportError_.empty()?L"导出已取消或保存失败":exportError_;}''',
    'export status',
)

start = text.index('    bool SaveExportPng(const std::wstring& customTitle){')
end = text.index('\n    void DrawExportCard(', start)
new_export = r'''    bool SaveExportPng(const std::wstring& customTitle){
        exportError_.clear();
        wchar_t filename[MAX_PATH]{};
        SYSTEMTIME st{};GetLocalTime(&st);
        swprintf_s(filename,L"鸣潮声骸记录_%04d-%02d-%02d_%02d-%02d-%02d.png",st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);
        OPENFILENAMEW ofn{sizeof(ofn)};
        ofn.hwndOwner=hwnd_;ofn.lpstrFilter=L"PNG 图片\0*.png\0";ofn.lpstrFile=filename;ofn.nMaxFile=MAX_PATH;
        ofn.lpstrDefExt=L"png";ofn.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
        if(!GetSaveFileNameW(&ofn)){exportError_=L"已取消导出";return false;}

        HRESULT hr=S_OK;
        ComPtr<IWICBitmap> bitmap;
        hr=wicFactory_->CreateBitmap(576,749,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,bitmap.GetAddressOf());
        if(FAILED(hr)){exportError_=L"无法创建导出画布";return false;}
        ComPtr<ID2D1RenderTarget> rt;
        const auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        hr=d2dFactory_->CreateWicBitmapRenderTarget(bitmap.Get(),props,rt.GetAddressOf());
        if(FAILED(hr)){exportError_=L"无法创建导出绘制目标";return false;}
        ComPtr<ID2D1SolidColorBrush> b;
        hr=rt->CreateSolidColorBrush(Hex(0xffffff),b.GetAddressOf());
        if(FAILED(hr)){exportError_=L"无法创建导出画刷";return false;}

        rt->BeginDraw();rt->Clear(Hex(0x202020));
        Text(rt.Get(),b.Get(),L"声骸记录",Rect(40,40,180,28),title_.Get(),Hex(0xffffff));
        Text(rt.Get(),b.Get(),L"原生版导出",Rect(40,68,180,20),body14_.Get(),Hex(0xffffff,.60f));
        const std::array<D2D1_POINT_2F,5> pos{{{40,100},{292,100},{40,307},{292,307},{40,514}}};
        for(int i=0;i<5;++i)DrawExportCard(rt.Get(),b.Get(),slots_[i],i,pos[i].x,pos[i].y);
        FillRound(rt.Get(),b.Get(),Rect(292,514,244,155),3,Hex(0xffffff,.03f));
        Text(rt.Get(),b.Get(),L"总分",Rect(320,549,80,22),version_.Get(),Hex(0xffffff),DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(rt.Get(),b.Get(),std::to_wstring(TotalScore()),Rect(320,579,80,34),title_.Get(),Hex(0x6ccb5f),DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(rt.Get(),b.Get(),L"平均分",Rect(428,549,80,22),version_.Get(),Hex(0xffffff),DWRITE_TEXT_ALIGNMENT_CENTER);
        wchar_t avg[32]{};swprintf_s(avg,L"%.1f",TotalScore()/5.0);
        Text(rt.Get(),b.Get(),avg,Rect(428,579,80,34),title_.Get(),Hex(0x6ccb5f),DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(rt.Get(),b.Get(),customTitle.empty()?L"鸣潮声骸计算器 v1.3.0-native":customTitle,Rect(40,687,496,22),version_.Get(),Hex(0xffffff),DWRITE_TEXT_ALIGNMENT_TRAILING);
        hr=rt->EndDraw();
        if(FAILED(hr)){exportError_=L"绘制导出图片失败";return false;}
        rt.Reset();b.Reset();

        // Remove a previous file after the overwrite prompt so WIC can create a
        // clean stream. Ignore FILE_NOT_FOUND, report every other failure later.
        DeleteFileW(filename);
        ComPtr<IWICStream> stream;
        hr=wicFactory_->CreateStream(stream.GetAddressOf());
        if(FAILED(hr)){exportError_=L"无法创建 PNG 文件流";return false;}
        hr=stream->InitializeFromFilename(filename,GENERIC_WRITE);
        if(FAILED(hr)){exportError_=L"无法写入所选路径，请检查目录权限";return false;}

        ComPtr<IWICBitmapEncoder> encoder;
        hr=wicFactory_->CreateEncoder(GUID_ContainerFormatPng,nullptr,encoder.GetAddressOf());
        if(FAILED(hr)){exportError_=L"无法创建 PNG 编码器";return false;}
        hr=encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);
        if(FAILED(hr)){exportError_=L"PNG 编码器初始化失败";return false;}

        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> bag;
        hr=encoder->CreateNewFrame(frame.GetAddressOf(),bag.GetAddressOf());
        if(FAILED(hr)){exportError_=L"无法创建 PNG 图像帧";return false;}
        hr=frame->Initialize(bag.Get());
        if(FAILED(hr)){exportError_=L"PNG 图像帧初始化失败";return false;}
        hr=frame->SetSize(576,749);
        if(FAILED(hr)){exportError_=L"无法设置 PNG 尺寸";return false;}
        frame->SetResolution(96.0,96.0);

        WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
        hr=frame->SetPixelFormat(&format);
        if(FAILED(hr)){exportError_=L"PNG 像素格式设置失败";return false;}
        ComPtr<IWICFormatConverter> converter;
        hr=wicFactory_->CreateFormatConverter(converter.GetAddressOf());
        if(FAILED(hr)){exportError_=L"无法创建 PNG 像素转换器";return false;}
        hr=converter->Initialize(bitmap.Get(),format,WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeCustom);
        if(FAILED(hr)){exportError_=L"PNG 像素格式转换失败";return false;}
        hr=frame->WriteSource(converter.Get(),nullptr);
        if(FAILED(hr)){exportError_=L"写入 PNG 图像数据失败";return false;}
        hr=frame->Commit();
        if(FAILED(hr)){exportError_=L"提交 PNG 图像帧失败";return false;}
        hr=encoder->Commit();
        if(FAILED(hr)){exportError_=L"完成 PNG 文件失败";return false;}
        stream.Reset();encoder.Reset();frame.Reset();converter.Reset();bitmap.Reset();

        const DWORD attributes=GetFileAttributesW(filename);
        if(attributes==INVALID_FILE_ATTRIBUTES){exportError_=L"PNG 编码完成后未找到输出文件";return false;}
        return true;
    }
'''
text = text[:start] + new_export + text[end:]

replace_once(
    '''    bool exportTitleInvalid_ = false;
    std::wstring status_;''',
    '''    bool exportTitleInvalid_ = false;
    std::wstring exportError_;
    std::wstring status_;''',
    'export error field',
)

TARGET.write_text(text,encoding='utf-8')
print(f'Generated {TARGET} ({len(text)} chars)')
