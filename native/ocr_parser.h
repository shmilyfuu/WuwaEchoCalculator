#pragma once

#include "ocr_engine.h"

#include <string>
#include <vector>

enum class ParsedOcrConfidence {
    High,
    Medium,
};

struct ParsedOcrRow {
    std::wstring attribute;
    std::wstring value;
    std::wstring sourceText;
    float confidenceScore = 0.0f;
    ParsedOcrConfidence confidence = ParsedOcrConfidence::Medium;
    float y = 0.0f;
};

std::vector<ParsedOcrRow> SelectMainCompatibleOcrRows(const std::vector<NativeOcrLine>& lines);
