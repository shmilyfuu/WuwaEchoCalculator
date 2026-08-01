from pathlib import Path

SOURCE = Path('native/app_v130_ocr.cpp')
TARGET = Path('native/app_v130_update.cpp')
text = SOURCE.read_text(encoding='utf-8')


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected one match, found {count}')
    text = text.replace(old, new, 1)


def insert_before(marker: str, insertion: str, label: str) -> None:
    global text
    position = text.find(marker)
    if position < 0:
        raise RuntimeError(f'{label}: marker missing')
    text = text[:position] + insertion + text[position:]


replace_once('#include "ocr_parser.h"\n', '#include "ocr_parser.h"\n#include "update_manager.h"\n', 'update include')
replace_once('constexpr UINT kOcrCompleteMessage = WM_APP + 101;\n',
             'constexpr UINT kOcrCompleteMessage = WM_APP + 101;\nconstexpr UINT kUpdateCompleteMessage = WM_APP + 102;\n',
             'update message')
replace_once('enum class ModalKind { None, Confirm, Export };',
             'enum class ModalKind { None, Confirm, Export, UpdateAvailable, UpdateProgress, UpdateReady, UpdateResult };',
             'update modal kinds')

# Add a wrapped text format for release notes.
replace_once(
    'CreateFormat(24, DWRITE_FONT_WEIGHT_SEMI_BOLD, exportStat24_.GetAddressOf());',
    '''CreateFormat(24, DWRITE_FONT_WEIGHT_SEMI_BOLD, exportStat24_.GetAddressOf());
        CreateFormat(12, DWRITE_FONT_WEIGHT_NORMAL, updateBody_.GetAddressOf());
        updateBody_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);''',
    'update text format',
)
replace_once(
    'ComPtr<IDWriteTextFormat> title_,version_,body14_,body12_,body12Bold_,body11_,body10_,body8_,stat20_,exportStat24_;',
    'ComPtr<IDWriteTextFormat> title_,version_,body14_,body12_,body12Bold_,body11_,body10_,body8_,stat20_,exportStat24_,updateBody_;',
    'update text format field',
)

# Initialize the updater beside OCR. A cached verified package is exposed immediately.
replace_once(
    '''        statusError_=!ocrReady_;
        status_=ocrReady_?L"PP-OCRv5 本地模型已就绪":ocrError;
        return S_OK;''',
    '''        statusError_=!ocrReady_;
        status_=ocrReady_?L"PP-OCRv5 本地模型已就绪":ocrError;
        std::wstring updateError;
        updateManager_.Initialize(std::filesystem::path(modulePath).parent_path().wstring(),kAppVersion,
            [this](const NativeUpdateSnapshot& snapshot){
                auto* copy=new NativeUpdateSnapshot(snapshot);
                if(!PostMessageW(hwnd_,kUpdateCompleteMessage,0,reinterpret_cast<LPARAM>(copy))) delete copy;
            },updateError);
        if(!updateError.empty()){statusError_=true;status_=updateError;}
        updateReady_=updateManager_.HasPreparedUpdate();
        if(updateReady_){
            updateState_.phase=NativeUpdatePhase::Ready;
            updateState_.latest=updateManager_.LatestInfo();
            updateState_.latest.version=updateManager_.PreparedVersion();
            updateState_.message=L"检测到已下载的更新包";
            modal_=ModalKind::UpdateReady;
        }
        updateManager_.Check(false);
        return S_OK;''',
    'updater initialize',
)

# Public update callback, delivered on the UI thread.
ocr_completion_end = '''        ApplyOcrLines(result->lines);
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

private:'''
update_callback = '''        ApplyOcrLines(result->lines);
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
            statusError_=false;status_=L"正在检查更新";
        }else if(phase==NativeUpdatePhase::Available){
            statusError_=false;status_=L"发现新版本 v"+snapshot->latest.version;
            modal_=ModalKind::UpdateAvailable;ShowWindow(edit_,SW_HIDE);
        }else if(phase==NativeUpdatePhase::UpToDate){
            statusError_=false;status_=L"当前已是最新版本";
            if(snapshot->manual){modal_=ModalKind::UpdateResult;ShowWindow(edit_,SW_HIDE);}
        }else if(phase==NativeUpdatePhase::Preparing||phase==NativeUpdatePhase::Downloading||phase==NativeUpdatePhase::Verifying){
            statusError_=false;status_=snapshot->message;modal_=ModalKind::UpdateProgress;ShowWindow(edit_,SW_HIDE);
        }else if(phase==NativeUpdatePhase::Ready){
            updateReady_=true;statusError_=false;status_=L"更新包已准备完成";
            modal_=ModalKind::UpdateReady;ShowWindow(edit_,SW_HIDE);
        }else if(phase==NativeUpdatePhase::Cancelled){
            statusError_=false;status_=L"已取消下载更新";
            if(modal_==ModalKind::UpdateProgress)modal_=ModalKind::None;
        }else if(phase==NativeUpdatePhase::Error){
            statusError_=true;status_=snapshot->error.empty()?snapshot->message:snapshot->error;
            if(snapshot->manual||modal_==ModalKind::UpdateProgress||modal_==ModalKind::UpdateAvailable)
                modal_=ModalKind::UpdateResult;
        }else if(phase==NativeUpdatePhase::LaunchingInstaller){
            statusError_=false;status_=L"正在启动更新助手";
        }
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

private:'''
replace_once(ocr_completion_end, update_callback, 'update callback')

# Utility text and geometry for the update dialog.
utilities = r'''    static std::wstring FormatTransferSize(std::uint64_t bytes){
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
    D2D1_RECT_F UpdatePrimaryRect() const {
        return modal_==ModalKind::UpdateResult?Rect(546,513,96,30):Rect(572,513,104,30);
    }
    D2D1_RECT_F UpdateSecondaryRect() const { return Rect(684,513,104,30); }

'''
insert_before('    void CreateTextFormats()', utilities, 'update utilities')

# Use the current updater label in the top bar and fix the complete topmost hit area.
replace_once('updateReady_?L"更新":updateBusy_?L"检查中":L"检查更新"', 'UpdateButtonLabel()', 'update button label')
replace_once('if (Contains(Rect(1110, 20, 58, 20), x, y)) {', 'if (Contains(Rect(1094, 20, 74, 20), x, y)) {', 'topmost hit area')
replace_once('if (modal_ != ModalKind::None) { CloseModal(); return; }',
             'if (modal_ != ModalKind::None) { CancelModal(); return; }',
             'escape cancels modal')

# Draw update dialogs before the existing confirm/export branches.
replace_once(
    '''    void DrawModal(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {
        Fill(rt,b,Rect(0,0,kClientWidth,kClientHeight),Hex(0x000000,.55f));
        if(modal_==ModalKind::Confirm){''',
    '''    void DrawModal(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b) {
        Fill(rt,b,Rect(0,0,kClientWidth,kClientHeight),Hex(0x000000,.55f));
        if(modal_==ModalKind::UpdateAvailable||modal_==ModalKind::UpdateProgress||
           modal_==ModalKind::UpdateReady||modal_==ModalKind::UpdateResult){
            DrawUpdateModal(rt,b);return;
        }
        if(modal_==ModalKind::Confirm){''',
    'update modal dispatch',
)

update_modal = r'''    void DrawUpdateModal(ID2D1RenderTarget* rt,ID2D1SolidColorBrush* b){
        const auto box=Rect(364,205,460,362);
        FillRound(rt,b,box,7,Hex(0x202020));
        FillRound(rt,b,Rect(364,205,460,284),7,Hex(0xffffff,.06f));
        Fill(rt,b,Rect(364,482,460,7),Hex(0x202020));
        if(modal_==ModalKind::UpdateAvailable){
            Text(rt,b,L"发现新版本",Rect(388,229,412,28),title_.Get(),Hex(0xffffff));
            Text(rt,b,L"v1.3.0  →  v"+updateState_.latest.version,Rect(388,269,412,22),version_.Get(),Hex(0x4cc2ff));
            Text(rt,b,L"下载来源："+(updateState_.latest.source.empty()?L"Gitee / GitHub":updateState_.latest.source),Rect(388,299,412,20),body12_.Get(),Hex(0xffffff,.60f));
            Text(rt,b,L"更新内容",Rect(388,331,412,20),body12Bold_.Get(),Hex(0xffffff));
            const std::wstring notes=updateState_.latest.notes.empty()?L"该版本未提供更新说明。":updateState_.latest.notes;
            Text(rt,b,notes,Rect(388,357,412,102),updateBody_.Get(),Hex(0xffffff,.70f),DWRITE_TEXT_ALIGNMENT_LEADING,DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            DrawButton(rt,b,ControlId::ModalAccept,UpdatePrimaryRect(),L"下载更新",ButtonKind::Blue,true);
            DrawButton(rt,b,ControlId::ModalCancel,UpdateSecondaryRect(),L"稍后",ButtonKind::Gray,true);
        }else if(modal_==ModalKind::UpdateProgress){
            Text(rt,b,L"正在下载更新",Rect(388,229,412,28),title_.Get(),Hex(0xffffff));
            const std::wstring version=updateState_.latest.version.empty()?L"":L"v1.3.0  →  v"+updateState_.latest.version;
            Text(rt,b,version,Rect(388,269,412,22),version_.Get(),Hex(0x4cc2ff));
            Text(rt,b,updateState_.message.empty()?L"正在准备更新包":updateState_.message,Rect(388,310,412,20),body14_.Get(),Hex(0xffffff));
            const auto track=Rect(388,348,412,8);FillRound(rt,b,track,4,Hex(0xffffff,.08f));
            float progress=0.0f;
            if(updateState_.totalBytes>0)progress=std::clamp(static_cast<float>(updateState_.downloadedBytes)/static_cast<float>(updateState_.totalBytes),0.0f,1.0f);
            else if(updateState_.phase==NativeUpdatePhase::Verifying)progress=1.0f;
            if(progress>0)FillRound(rt,b,Rect(388,348,412*progress,8),4,Hex(0x4cc2ff));
            const int percent=updateState_.totalBytes>0?static_cast<int>(progress*100.0f):(updateState_.phase==NativeUpdatePhase::Verifying?100:0);
            Text(rt,b,std::to_wstring(percent)+L"%",Rect(388,366,412,20),body12Bold_.Get(),Hex(0xffffff),DWRITE_TEXT_ALIGNMENT_TRAILING);
            const std::wstring transferred=FormatTransferSize(updateState_.downloadedBytes)+L" / "+(updateState_.totalBytes?FormatTransferSize(updateState_.totalBytes):L"未知大小");
            Text(rt,b,transferred,Rect(388,394,250,20),body12_.Get(),Hex(0xffffff,.60f));
            Text(rt,b,updateState_.bytesPerSecond?FormatTransferSize(updateState_.bytesPerSecond)+L"/s":L"",Rect(650,394,150,20),body12_.Get(),Hex(0xffffff,.60f),DWRITE_TEXT_ALIGNMENT_TRAILING);
            Text(rt,b,L"来源："+(updateState_.latest.source.empty()?L"Gitee / GitHub":updateState_.latest.source),Rect(388,426,412,20),body12_.Get(),Hex(0xffffff,.60f));
            DrawButton(rt,b,ControlId::ModalCancel,UpdateSecondaryRect(),L"取消下载",ButtonKind::Gray,updateState_.phase==NativeUpdatePhase::Downloading||updateState_.phase==NativeUpdatePhase::Preparing);
        }else if(modal_==ModalKind::UpdateReady){
            Text(rt,b,L"更新已准备完成",Rect(388,229,412,28),title_.Get(),Hex(0xffffff));
            const std::wstring version=updateState_.latest.version.empty()?updateManager_.PreparedVersion():updateState_.latest.version;
            Text(rt,b,L"新版本 v"+version+L" 已下载，可以立即安装。",Rect(388,277,412,24),body14_.Get(),Hex(0xffffff));
            Text(rt,b,L"立即更新会关闭计算器，替换程序文件并自动重新启动。声骸记录和设置文件会保留。",Rect(388,321,412,70),updateBody_.Get(),Hex(0xffffff,.70f),DWRITE_TEXT_ALIGNMENT_LEADING,DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            Text(rt,b,L"选择稍后后，顶部按钮会变为蓝色“更新”。",Rect(388,412,412,20),body12_.Get(),Hex(0xffffff,.60f));
            DrawButton(rt,b,ControlId::ModalAccept,UpdatePrimaryRect(),L"立即更新",ButtonKind::Blue,true);
            DrawButton(rt,b,ControlId::ModalCancel,UpdateSecondaryRect(),L"稍后",ButtonKind::Gray,true);
        }else{
            const bool failed=updateState_.phase==NativeUpdatePhase::Error;
            Text(rt,b,failed?L"更新操作失败":L"检查更新",Rect(388,229,412,28),title_.Get(),Hex(0xffffff));
            Text(rt,b,failed?(updateState_.error.empty()?updateState_.message:updateState_.error):L"当前已是最新版本 v1.3.0",Rect(388,285,412,108),updateBody_.Get(),failed?Hex(0xff99a4):Hex(0xffffff,.75f),DWRITE_TEXT_ALIGNMENT_LEADING,DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            DrawButton(rt,b,ControlId::ModalAccept,UpdatePrimaryRect(),L"确定",ButtonKind::Blue,true);
        }
    }

'''
insert_before('    D2D1_RECT_F ExportInputRect()', update_modal, 'update modal renderer')

# Modal hit tests, update activation and cancel behavior.
replace_once(
    '''        if(modal_==ModalKind::Export){
            if(Contains(Rect(562,431,96,30),x,y))return ControlId::ModalAccept;
            if(Contains(Rect(666,431,96,30),x,y))return ControlId::ModalCancel;
            return ControlId::None;
        }''',
    '''        if(modal_==ModalKind::Export){
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
        }''',
    'update modal hit tests',
)
replace_once(
    'if(id==ControlId::Update){updateBusy_=true;statusError_=false;status_=L"正在检查更新";InvalidateRect(hwnd_,nullptr,FALSE);return;}',
    '''if(id==ControlId::Update){
            if(updateReady_){
                updateState_.phase=NativeUpdatePhase::Ready;updateState_.latest=updateManager_.LatestInfo();
                if(updateState_.latest.version.empty())updateState_.latest.version=updateManager_.PreparedVersion();
                modal_=ModalKind::UpdateReady;ShowWindow(edit_,SW_HIDE);
            }else{
                updateBusy_=true;updateState_.phase=NativeUpdatePhase::Checking;statusError_=false;status_=L"正在检查更新";
                updateManager_.Check(true);
            }
            return;
        }''',
    'update button action',
)
replace_once('if(id==ControlId::ModalCancel){CloseModal();return;}', 'if(id==ControlId::ModalCancel){CancelModal();return;}', 'modal cancel action')

# Extend accept/cancel logic while preserving confirm and export behavior.
replace_once(
    '''    void AcceptModal(){
        if(modal_==ModalKind::Export){''',
    '''    void CancelModal(){
        if(modal_==ModalKind::UpdateProgress){
            if(updateState_.phase==NativeUpdatePhase::Downloading||updateState_.phase==NativeUpdatePhase::Preparing)
                updateManager_.CancelDownload();
            return;
        }
        if(modal_==ModalKind::UpdateReady){
            updateReady_=true;statusError_=false;status_=L"更新包已保留，可稍后安装";CloseModal();return;
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
                updateBusy_=true;statusError_=false;status_=L"正在启动更新助手";
                PostMessageW(hwnd_,WM_CLOSE,0,0);
            }else{
                updateState_.phase=NativeUpdatePhase::Error;updateState_.error=error;statusError_=true;status_=error;modal_=ModalKind::UpdateResult;
            }
            InvalidateRect(hwnd_,nullptr,FALSE);return;
        }
        if(modal_==ModalKind::UpdateResult){CloseModal();return;}
        if(modal_==ModalKind::Export){''',
    'update modal accept logic',
)

# Status footer and app fields.
replace_once('Text(rt,b,L"Direct2D 原生预览",Rect(1000,732,168,20),body11_.Get(),Hex(0xffffff,.60f),DWRITE_TEXT_ALIGNMENT_TRAILING);',
             'Text(rt,b,L"v1.3.0 · Direct2D",Rect(1000,732,168,20),body11_.Get(),Hex(0xffffff,.60f),DWRITE_TEXT_ALIGNMENT_TRAILING);',
             'status version label')
replace_once(
    '''    NativeOcrEngine ocr_;
    std::thread ocrThread_;''',
    '''    NativeUpdateManager updateManager_;
    NativeUpdateSnapshot updateState_{};
    NativeOcrEngine ocr_;
    std::thread ocrThread_;''',
    'update fields',
)

# Deliver updater callbacks in the main window procedure.
replace_once(
    'case kOcrCompleteMessage:g_app->OcrCompleted(reinterpret_cast<NativeOcrJobResult*>(lParam));return 0;',
    'case kOcrCompleteMessage:g_app->OcrCompleted(reinterpret_cast<NativeOcrJobResult*>(lParam));return 0;\n    case kUpdateCompleteMessage:g_app->UpdateCompleted(reinterpret_cast<NativeUpdateSnapshot*>(lParam));return 0;',
    'update window message',
)

TARGET.write_text(text, encoding='utf-8')
print(f'Generated {TARGET} ({len(text)} chars)')
