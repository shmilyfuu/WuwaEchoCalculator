#define UNICODE
#define _UNICODE
#include "ocr_parser.h"

#include <iostream>
#include <vector>

int wmain() {
    std::vector<NativeOcrLine> lines{
        {L"共鸣效率", .96f, 10, 10, 130, 34}, {L"8.4%", .97f, 180, 10, 230, 34},
        {L"攻击", .95f, 10, 50, 70, 74}, {L"40", .98f, 180, 50, 215, 74},
        {L"共鸣技能伤害加成", .94f, 10, 90, 180, 114}, {L"7.1%", .98f, 190, 90, 245, 114},
        {L"暴击伤害", .97f, 10, 130, 110, 154}, {L"17.4%", .98f, 180, 130, 245, 154},
        {L"暴击", .98f, 10, 170, 70, 194}, {L"6.9%", .98f, 180, 170, 235, 194},
    };
    const auto rows = SelectMainCompatibleOcrRows(lines);
    if (rows.size() != 5) {
        std::wcerr << L"expected 5 rows, got " << rows.size() << L"\n";
        return 1;
    }
    const std::vector<std::pair<std::wstring,std::wstring>> expected{
        {L"共鸣效率",L"8.4%"}, {L"攻击",L"40"}, {L"共鸣技能伤害加成",L"7.1%"},
        {L"暴击伤害",L"17.4%"}, {L"暴击",L"6.9%"},
    };
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (rows[i].attribute != expected[i].first || rows[i].value != expected[i].second) {
            std::wcerr << L"row " << i << L" mismatch: " << rows[i].attribute << L" " << rows[i].value << L"\n";
            return 2;
        }
    }
    std::wcout << L"main-compatible OCR parser smoke test passed\n";
    return 0;
}
