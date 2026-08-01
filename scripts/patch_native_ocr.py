from pathlib import Path

SOURCE = Path('native/app_generated.cpp')
TARGET = Path('native/app_ocr.cpp')
text = SOURCE.read_text(encoding='utf-8')


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected one match, found {count}')
    text = text.replace(old, new, 1)


replace_once('#include "icon_assets.h"\n', '#include "icon_assets.h"\n#include "ocr_engine.h"\n#include <atomic>\n#include <thread>\n#include <memory>\n#include <cwctype>\n', 'ocr includes')
replace_once('constexpr UINT kEditId = 7001;\n', 'constexpr UINT kEditId = 7001;\nconstexpr UINT kOcrCompleteMessage = WM_APP + 101;\n', 'ocr message')
replace_once('    ~App() { if (editFont_) DeleteObject(editFont_); if (editBrush_) DeleteObject(editBrush_); }',
             '    ~App() { ocrCancel_.store(true); if(ocrThread_.joinable()) ocrThread_.join(); if (editFont_) DeleteObject(editFont_); if (editBrush_) DeleteObject(editBrush_); }',
             'ocr destructor')

replace_once('''        CreateTextFormats();
        CreateEditControl();
        DragAcceptFiles(hwnd_, TRUE);
        status_ = L"原生界面已就绪 · OCR 模块待迁移";
        return S_OK;''', '''        CreateTextFormats();
        CreateEditControl();
        DragAcceptFiles(hwnd_, TRUE);
        wchar_t modulePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr,modulePath,MAX_PATH);
        std::wstring ocrError;
        const auto modelDirectory=(std::filesystem::path(modulePath).parent_path()/L"models").wstring();
        ocrReady_=ocr_.Initialize(modelDirectory,ocrError);
        statusError_=!ocrReady_;
        status_=ocrReady_?L"PP-OCRv5 本地模型已就绪":ocrError;
        return S_OK;''', 'ocr initialize')

replace_once('''    void ExportEscape() { if (modal_ == ModalKind::Export) CloseModal(); }

private:''', '''    void ExportEscape() { if (modal_ == ModalKind::Export) CloseModal(); }
    void OcrCompleted(NativeOcrJobResult* rawResult) {
        std::unique_ptr<NativeOcrJobResult> result(rawResult);
        if(ocrThread_.joinable()) ocrThread_.join();
        ocrRunning_=false;
        if(!result){statusError_=true;status_=L"OCR 返回结果为空";InvalidateRect(hwnd_,nullptr,FALSE);return;}
        if(result->cancelled){statusError_=false;status_=L"识别已停止";InvalidateRect(hwnd_,nullptr,FALSE);return;}
        if(!result->error.empty()){statusError_=true;status_=result->error;InvalidateRect(hwnd_,nullptr,FALSE);return;}
        ApplyOcrLines(result->lines);
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

private:''', 'ocr completion public')

helpers = r'''    static std::wstring CompactOcrText(std::wstring value) {
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
        struct Group{int center=0;int height=0;std::wstring text;float confidence=0;int count=0;};
        std::vector<Group> groups;
        for(const auto& line:lines){
            const int center=(line.top+line.bottom)/2; const int height=std::max(1,line.bottom-line.top);
            Group* target=nullptr;
            for(auto& group:groups) if(std::abs(group.center-center)<=std::max(9,(group.height+height)/3)){target=&group;break;}
            if(!target){groups.push_back({center,height,line.text,line.confidence,1});}
            else{target->text+=L" "+line.text;target->confidence+=line.confidence;target->count++;target->center=(target->center+center)/2;target->height=std::max(target->height,height);}
        }
        std::sort(groups.begin(),groups.end(),[](const Group&a,const Group&b){return a.center<b.center;});
        std::array<RowSelection,5> found{}; std::array<int,5> confidence{}; int used=0;
        std::array<bool,32> seen{};
        for(const auto& group:groups){
            const int attribute=AttributeFromText(group.text); if(attribute<0||seen[attribute]) continue;
            const int value=ValueFromText(attribute,group.text); if(value<0) continue;
            found[used]={attribute,value};
            const float score=group.count?group.confidence/group.count:0.0f;
            confidence[used]=score>=0.72f?1:2;
            seen[attribute]=true; if(++used==5) break;
        }
        rows_={}; rowConfidence_.fill(0);
        for(int i=0;i<used;++i){rows_[i]=found[i];rowConfidence_[i]=confidence[i];}
        statusError_=used==0;
        status_=used==5?L"识别完成，请核对五条属性":used>0?L"识别到 "+std::to_wstring(used)+L" 条属性，请补充并核对":L"未识别到有效属性，请调整截图或手动选择";
    }
    bool CopyCurrentImage(std::vector<std::uint8_t>& pixels,int& width,int& height,int& stride){
        if(!imageWic_||imageW_==0||imageH_==0) return false;
        width=static_cast<int>(imageW_);height=static_cast<int>(imageH_);stride=width*4;
        pixels.resize(static_cast<size_t>(stride)*height);
        return SUCCEEDED(imageWic_->CopyPixels(nullptr,stride,static_cast<UINT>(pixels.size()),pixels.data()));
    }
    void StartOcr(){
        if(!ocrReady_){statusError_=true;status_=L"OCR 模型尚未就绪";InvalidateRect(hwnd_,nullptr,FALSE);return;}
        if(ocrRunning_){ocrCancel_.store(true);if(ocrThread_.joinable())ocrThread_.join();ocrRunning_=false;}
        std::vector<std::uint8_t> pixels;int width=0,height=0,stride=0;
        if(!CopyCurrentImage(pixels,width,height,stride)){statusError_=true;status_=L"无法读取当前图片像素";InvalidateRect(hwnd_,nullptr,FALSE);return;}
        ocrCancel_.store(false);ocrRunning_=true;statusError_=false;status_=L"正在识别声骸属性";rowConfidence_.fill(0);InvalidateRect(hwnd_,nullptr,FALSE);
        ocrThread_=std::thread([this,pixels=std::move(pixels),width,height,stride]() mutable {
            auto* result=new NativeOcrJobResult(ocr_.Recognize(pixels,width,height,stride,ocrCancel_));
            PostMessageW(hwnd_,kOcrCompleteMessage,0,reinterpret_cast<LPARAM>(result));
        });
    }
    void StopOcr(){if(!ocrRunning_)return;ocrCancel_.store(true);statusError_=false;status_=L"正在停止识别";InvalidateRect(hwnd_,nullptr,FALSE);}

'''
replace_once('private:\n    void CreateTextFormats()', 'private:\n' + helpers + '    void CreateTextFormats()', 'ocr helpers')

text = text.replace('DrawButton(rt,b,ControlId::Stop,Rect(20,410,156,30),L"停止识别",ButtonKind::Red,false);',
                    'DrawButton(rt,b,ControlId::Stop,Rect(20,410,156,30),L"停止识别",ButtonKind::Red,ocrRunning_);')
text = text.replace('L"2. 原生 OCR 接入前可手动选择属性"', 'L"2. 识别后核对属性与数值"')
replace_once('''            if(rows_[i].attribute>=0) DrawIcon(rt,iconManual_.Get(),Rect(773,y+17,20,12));
            else { b->SetColor(Hex(0xffffff,.25f)); rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(783,y+23),4,4),b); }''', '''            if(rows_[i].attribute>=0){
                if(rowConfidence_[i]==1) DrawIcon(rt,iconNormal_.Get(),Rect(776,y+16,14,14));
                else if(rowConfidence_[i]==2) DrawIcon(rt,iconAbnormal_.Get(),Rect(776,y+16,14,14));
                else DrawIcon(rt,iconManual_.Get(),Rect(773,y+17,20,12));
            } else { b->SetColor(Hex(0xffffff,.25f)); rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(783,y+23),4,4),b); }''', 'ocr row status')

text = text.replace('if(id==ControlId::Stop)return false;', 'if(id==ControlId::Stop)return ocrRunning_;')
text = text.replace('if(id==ControlId::Again)return imageW_>0;', 'if(id==ControlId::Again)return imageW_>0&&!ocrRunning_;')
replace_once('''        if(id==ControlId::DropZone){PickImageFile();return;}
        if(id==ControlId::Again){statusError_=false;status_=L"原生 OCR 尚未接入，当前请手动核对属性";return;}''', '''        if(id==ControlId::DropZone){PickImageFile();return;}
        if(id==ControlId::Stop){StopOcr();return;}
        if(id==ControlId::Again){StartOcr();return;}''', 'ocr button activation')

text = text.replace('if(dropdown_.kind==DropdownKind::Attribute){rows_[dropdown_.row].attribute=index;rows_[dropdown_.row].value=-1;}',
                    'if(dropdown_.kind==DropdownKind::Attribute){rows_[dropdown_.row].attribute=index;rows_[dropdown_.row].value=-1;rowConfidence_[dropdown_.row]=3;}')
text = text.replace('else if(dropdown_.kind==DropdownKind::Value){rows_[dropdown_.row].value=index;}',
                    'else if(dropdown_.kind==DropdownKind::Value){rows_[dropdown_.row].value=index;rowConfidence_[dropdown_.row]=3;}')

replace_once('''        imageWic_->GetSize(&imageW_,&imageH_);imageName_=name;RecreateD2DBitmap();statusError_=false;status_=L"已导入 "+name+L" · 原生 OCR 待接入";InvalidateRect(hwnd_,nullptr,FALSE);''', '''        imageWic_->GetSize(&imageW_,&imageH_);imageName_=name;RecreateD2DBitmap();statusError_=false;status_=L"已导入 "+name;InvalidateRect(hwnd_,nullptr,FALSE);StartOcr();''', 'auto ocr')

replace_once('''    std::array<RowSelection,5> rows_{};
    std::array<SlotRecord,5> slots_{};''', '''    std::array<RowSelection,5> rows_{};
    std::array<int,5> rowConfidence_{};
    std::array<SlotRecord,5> slots_{};''', 'confidence field')
replace_once('''    bool topmost_ = false;
    bool statusError_ = false;''', '''    NativeOcrEngine ocr_;
    std::thread ocrThread_;
    std::atomic_bool ocrCancel_{false};
    bool ocrReady_ = false;
    bool ocrRunning_ = false;
    bool topmost_ = false;
    bool statusError_ = false;''', 'ocr fields')

replace_once('''    case WM_COMMAND:if(LOWORD(wParam)==kEditId&&HIWORD(wParam)==EN_CHANGE)g_app->EditChanged();return 0;
    case WM_DESTROY:''', '''    case WM_COMMAND:if(LOWORD(wParam)==kEditId&&HIWORD(wParam)==EN_CHANGE)g_app->EditChanged();return 0;
    case kOcrCompleteMessage:g_app->OcrCompleted(reinterpret_cast<NativeOcrJobResult*>(lParam));return 0;
    case WM_DESTROY:''', 'ocr message handler')

TARGET.write_text(text,encoding='utf-8')
print(f'Generated {TARGET} ({len(text)} chars)')
