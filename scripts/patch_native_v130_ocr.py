from pathlib import Path

SOURCE = Path('native/app_v130_ui.cpp')
TARGET = Path('native/app_v130_ocr.cpp')
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


replace_once('#include "ocr_engine.h"\n', '#include "ocr_engine.h"\n#include "ocr_parser.h"\n', 'parser include')

replace_block(
    '    void ApplyOcrLines(const std::vector<NativeOcrLine>& lines){',
    '    bool CopyCurrentImage(',
    '''    void ApplyOcrLines(const std::vector<NativeOcrLine>& lines){
        const auto selected=SelectMainCompatibleOcrRows(lines);
        rows_={};rowConfidence_.fill(0);
        int used=0;
        for(const auto& parsed:selected){
            if(used>=5)break;
            int attribute=-1;
            for(int index=0;index<static_cast<int>(rules_.size());++index){
                if(rules_[index].name==parsed.attribute){attribute=index;break;}
            }
            if(attribute<0)continue;
            int value=-1;
            for(int index=0;index<static_cast<int>(rules_[attribute].values.size());++index){
                if(rules_[attribute].values[index]==parsed.value){value=index;break;}
            }
            if(value<0)continue;
            rows_[used]={attribute,value};
            rowConfidence_[used]=parsed.confidence==ParsedOcrConfidence::High?1:2;
            ++used;
        }
        statusError_=used==0;
        const int medium=static_cast<int>(std::count(rowConfidence_.begin(),rowConfidence_.end(),2));
        if(used==5&&medium==0)status_=L"已识别五条，请核对后写入";
        else if(used==5)status_=L"已识别五条，其中 "+std::to_wstring(medium)+L" 条建议重点核对";
        else if(used>0)status_=L"当前识别到 "+std::to_wstring(used)+L" 条，请通过下拉框补齐";
        else status_=L"未识别到有效属性，请调整截图或手动选择";
    }''',
    'main-compatible OCR application',
)

TARGET.write_text(text, encoding='utf-8')
print(f'Generated {TARGET} ({len(text)} chars)')
