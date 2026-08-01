#include "ocr_parser.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <string_view>
#include <utility>

namespace {

struct AttributeDefinition {
    std::wstring name;
    std::vector<std::wstring> values;
    std::vector<std::wstring> aliases;
    bool percent = false;
};

const std::vector<AttributeDefinition>& Definitions() {
    static const std::vector<AttributeDefinition> definitions{
        {L"生命百分比", {L"6.4%",L"7.1%",L"7.9%",L"8.6%",L"9.4%",L"10.1%",L"10.9%",L"11.6%"}, {L"生命百分比",L"生命百份比",L"生命白分比",L"生命百分",L"生命%"}, true},
        {L"生命", {L"320",L"360",L"390",L"430",L"470",L"510",L"540",L"580"}, {L"生命",L"生俞"}, false},
        {L"防御百分比", {L"8.1%",L"9.0%",L"10.0%",L"10.9%",L"11.8%",L"12.8%",L"13.8%",L"14.7%"}, {L"防御百分比",L"防御百份比",L"防御白分比",L"防御百分",L"防御%"}, true},
        {L"防御", {L"40",L"50",L"60",L"70"}, {L"防御",L"防御力"}, false},
        {L"暴击", {L"6.3%",L"6.9%",L"7.5%",L"8.1%",L"8.7%",L"9.3%",L"9.9%",L"10.5%"}, {L"暴击",L"暴撃"}, true},
        {L"暴击伤害", {L"12.6%",L"13.8%",L"15.0%",L"16.2%",L"17.4%",L"18.6%",L"19.8%",L"21.0%"}, {L"暴击伤害",L"暴击损害",L"暴击伤書",L"暴伤"}, true},
        {L"攻击百分比", {L"6.4%",L"7.1%",L"7.9%",L"8.6%",L"9.4%",L"10.1%",L"10.9%",L"11.6%"}, {L"攻击百分比",L"攻击百份比",L"攻击白分比",L"攻击百分",L"攻击%"}, true},
        {L"攻击", {L"30",L"40",L"50",L"60"}, {L"攻击",L"攻去"}, false},
        {L"共鸣效率", {L"6.8%",L"7.6%",L"8.4%",L"9.2%",L"10.0%",L"10.8%",L"11.6%",L"12.4%"}, {L"共鸣效率",L"共鸣效串",L"共鸣效宰"}, true},
        {L"普攻伤害加成", {L"6.4%",L"7.1%",L"7.9%",L"8.6%",L"9.4%",L"10.1%",L"10.9%",L"11.6%"}, {L"普攻伤害加成",L"普攻伤害",L"普攻加成"}, true},
        {L"重击伤害加成", {L"6.4%",L"7.1%",L"7.9%",L"8.6%",L"9.4%",L"10.1%",L"10.9%",L"11.6%"}, {L"重击伤害加成",L"重击伤害",L"重击加成"}, true},
        {L"共鸣技能伤害加成", {L"6.4%",L"7.1%",L"7.9%",L"8.6%",L"9.4%",L"10.1%",L"10.9%",L"11.6%"}, {L"共鸣技能伤害加成",L"共鸣技能伤害",L"共鸣技能加成"}, true},
        {L"共鸣解放伤害加成", {L"6.4%",L"7.1%",L"7.9%",L"8.6%",L"9.4%",L"10.1%",L"10.9%",L"11.6%"}, {L"共鸣解放伤害加成",L"共鸣解放伤害",L"共鸣解放加成"}, true},
    };
    return definitions;
}

const AttributeDefinition* FindDefinition(std::wstring_view name) {
    const auto& definitions = Definitions();
    const auto it = std::find_if(definitions.begin(), definitions.end(), [&](const auto& item) { return item.name == name; });
    return it == definitions.end() ? nullptr : &*it;
}

wchar_t NormalizeCharacter(wchar_t ch) {
    switch (ch) {
    case L'Ｏ': case L'〇': case L'○': case L'O': case L'o': return L'0';
    case L'Ｉ': case L'I': case L'l': case L'丨': case L'|': return L'1';
    case L'，': case L',': case L'．': case L'·': case L'•': return L'.';
    case L'％': case L'﹪': return L'%';
    default: return ch;
    }
}

std::wstring NormalizeText(std::wstring_view value) {
    std::wstring output;
    output.reserve(value.size());
    for (wchar_t ch : value) {
        if (iswspace(ch)) continue;
        output.push_back(NormalizeCharacter(ch));
    }
    return output;
}

bool IsDecoration(wchar_t ch) {
    switch (ch) {
    case L'+': case L'✦': case L'★': case L'☆': case L'◆': case L'◇': case L'·': case L'•': return true;
    default: return false;
    }
}

std::wstring CleanText(std::wstring_view value) {
    std::wstring output;
    output.reserve(value.size());
    bool pendingSpace = false;
    for (wchar_t ch : value) {
        if (IsDecoration(ch) || iswspace(ch)) {
            pendingSpace = !output.empty();
            continue;
        }
        if (pendingSpace && !output.empty()) output.push_back(L' ');
        pendingSpace = false;
        output.push_back(ch);
    }
    while (!output.empty() && output.back() == L' ') output.pop_back();
    return output;
}

int Levenshtein(const std::wstring& a, const std::wstring& b) {
    std::vector<int> previous(b.size() + 1), current(b.size() + 1);
    std::iota(previous.begin(), previous.end(), 0);
    for (std::size_t i = 1; i <= a.size(); ++i) {
        current[0] = static_cast<int>(i);
        for (std::size_t j = 1; j <= b.size(); ++j) {
            current[j] = std::min({
                previous[j] + 1,
                current[j - 1] + 1,
                previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1),
            });
        }
        previous.swap(current);
    }
    return previous.back();
}

float Similarity(std::wstring_view a, std::wstring_view b) {
    const std::wstring x = NormalizeText(a);
    const std::wstring y = NormalizeText(b);
    if (x.empty() || y.empty()) return 0.0f;
    if (x.find(y) != std::wstring::npos || y.find(x) != std::wstring::npos) {
        const float small = static_cast<float>(std::min(x.size(), y.size()));
        const float large = static_cast<float>(std::max(x.size(), y.size()));
        return small / large * 0.3f + 0.7f;
    }
    return 1.0f - static_cast<float>(Levenshtein(x, y)) / static_cast<float>(std::max(x.size(), y.size()));
}

bool ContainsAny(std::wstring_view text, std::initializer_list<std::wstring_view> values) {
    for (const auto value : values) if (text.find(value) != std::wstring_view::npos) return true;
    return false;
}

struct AttributeMatch {
    std::wstring name;
    float score = 0.0f;
    bool valid = false;
};

AttributeMatch MatchAttribute(std::wstring_view text) {
    std::wstring raw = CleanText(text);
    raw.erase(std::remove(raw.begin(), raw.end(), L' '), raw.end());
    const bool resonance = ContainsAny(raw, {L"共鸣", L"共鳴"});
    if (resonance && raw.find(L"解放") != std::wstring::npos) return {L"共鸣解放伤害加成", .99f, true};
    if (resonance && raw.find(L"技能") != std::wstring::npos) return {L"共鸣技能伤害加成", .99f, true};
    if (resonance && ContainsAny(raw, {L"效率", L"效串", L"效宰", L"敩率", L"敩串", L"敩宰"})) return {L"共鸣效率", .99f, true};
    if (raw.find(L"普攻") != std::wstring::npos) return {L"普攻伤害加成", .99f, true};
    if (ContainsAny(raw, {L"重击", L"重擊"})) return {L"重击伤害加成", .99f, true};
    const bool critical = ContainsAny(raw, {L"暴击", L"暴擊"});
    if (critical && ContainsAny(raw, {L"伤", L"傷", L"损", L"損", L"書"})) return {L"暴击伤害", .99f, true};
    if (critical) return {L"暴击", .99f, true};
    if (ContainsAny(raw, {L"防御", L"防禦"}) && ContainsAny(raw, {L"百分", L"%"})) return {L"防御百分比", .99f, true};
    if (ContainsAny(raw, {L"攻击", L"攻擊"}) && ContainsAny(raw, {L"百分", L"%"})) return {L"攻击百分比", .99f, true};
    if (raw.find(L"生命") != std::wstring::npos && ContainsAny(raw, {L"百分", L"%"})) return {L"生命百分比", .99f, true};

    AttributeMatch best;
    for (const auto& definition : Definitions()) {
        for (const auto& alias : definition.aliases) {
            const float score = Similarity(raw, alias);
            if (!best.valid || score > best.score) best = {definition.name, score, true};
        }
    }
    if (!best.valid || best.score < .42f) return {};
    return best;
}

struct NumberToken {
    std::wstring raw;
    double numeric = 0.0;
    bool hasPercent = false;
};

std::vector<NumberToken> ExtractNumbers(std::wstring_view source) {
    const std::wstring text = NormalizeText(source);
    std::vector<NumberToken> output;
    std::size_t index = 0;
    while (index < text.size()) {
        if (!iswdigit(text[index])) { ++index; continue; }
        const std::size_t start = index;
        int integerDigits = 0;
        while (index < text.size() && iswdigit(text[index]) && integerDigits < 4) { ++index; ++integerDigits; }
        bool dot = false;
        int decimalDigits = 0;
        if (index < text.size() && text[index] == L'.') {
            dot = true; ++index;
            while (index < text.size() && iswdigit(text[index]) && decimalDigits < 2) { ++index; ++decimalDigits; }
            if (decimalDigits == 0) { dot = false; --index; }
        }
        bool percent = false;
        if (index < text.size() && text[index] == L'%') { percent = true; ++index; }
        const std::wstring raw = text.substr(start, index - start);
        std::wstring numericText = raw;
        numericText.erase(std::remove(numericText.begin(), numericText.end(), L'%'), numericText.end());
        try {
            const double numeric = std::stod(numericText);
            if (std::isfinite(numeric)) output.push_back({raw, numeric, percent || dot});
        } catch (...) {
        }
    }
    return output;
}

std::wstring NormalizeAttributeByValue(std::wstring attribute, const NumberToken& number) {
    if (attribute == L"生命" && number.hasPercent) return L"生命百分比";
    if (attribute == L"防御" && number.hasPercent) return L"防御百分比";
    if (attribute == L"攻击" && number.hasPercent) return L"攻击百分比";
    if (attribute == L"生命百分比" && !number.hasPercent && number.numeric >= 100.0) return L"生命";
    if (attribute == L"防御百分比" && !number.hasPercent && number.numeric >= 30.0) return L"防御";
    if (attribute == L"攻击百分比" && !number.hasPercent && number.numeric >= 25.0) return L"攻击";
    return attribute;
}

double NumericValue(std::wstring value) {
    value.erase(std::remove(value.begin(), value.end(), L'%'), value.end());
    try { return std::stod(value); } catch (...) { return 0.0; }
}

struct ValueMatch {
    std::wstring value;
    double distance = 0.0;
    bool exact = false;
    bool valid = false;
};

ValueMatch MatchValue(std::wstring_view attribute, const NumberToken& number) {
    const auto* definition = FindDefinition(attribute);
    if (!definition) return {};
    ValueMatch best;
    for (const auto& value : definition->values) {
        const double distance = std::abs(NumericValue(value) - number.numeric);
        if (!best.valid || distance < best.distance) best = {value, distance, distance < .001, true};
    }
    if (!best.valid) return {};
    const double limit = definition->percent ? .16 : 2.1;
    if (!best.exact && best.distance > limit) return {};
    return best;
}

struct OcrBox {
    int index = 0;
    std::wstring text;
    float score = 0.0f;
    float minX = 0.0f;
    float maxX = 0.0f;
    float centerY = 0.0f;
    float height = 1.0f;
};

struct OcrGroup {
    std::vector<OcrBox> items;
    float centerY = 0.0f;
};

struct Candidate : ParsedOcrRow {
    float ocrScore = 0.0f;
    bool exactValue = false;
};

float Median(std::vector<float> values) {
    if (values.empty()) return 18.0f;
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    return values.size() % 2 ? values[middle] : (values[middle - 1] + values[middle]) / 2.0f;
}

std::vector<OcrBox> BuildBoxes(const std::vector<NativeOcrLine>& lines) {
    std::vector<OcrBox> boxes;
    boxes.reserve(lines.size());
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto& line = lines[index];
        std::wstring text = CleanText(line.text);
        if (text.empty()) continue;
        boxes.push_back({
            static_cast<int>(index), text, line.confidence,
            static_cast<float>(line.left), static_cast<float>(line.right),
            (line.top + line.bottom) / 2.0f,
            static_cast<float>(std::max(1, line.bottom - line.top)),
        });
    }
    return boxes;
}

std::vector<OcrGroup> GroupRows(const std::vector<OcrBox>& input) {
    std::vector<OcrBox> boxes = input;
    std::vector<float> heights; heights.reserve(boxes.size());
    for (const auto& box : boxes) heights.push_back(box.height);
    const float threshold = std::max(9.0f, Median(heights) * .62f);
    std::sort(boxes.begin(), boxes.end(), [](const auto& a, const auto& b) {
        if (a.centerY != b.centerY) return a.centerY < b.centerY;
        return a.minX < b.minX;
    });
    std::vector<OcrGroup> groups;
    for (const auto& box : boxes) {
        auto it = std::find_if(groups.begin(), groups.end(), [&](const auto& group) { return std::abs(group.centerY - box.centerY) <= threshold; });
        if (it == groups.end()) {
            groups.push_back({{box}, box.centerY});
        } else {
            it->items.push_back(box);
            float total = 0.0f;
            for (const auto& item : it->items) total += item.centerY;
            it->centerY = total / static_cast<float>(it->items.size());
        }
    }
    for (auto& group : groups) std::sort(group.items.begin(), group.items.end(), [](const auto& a, const auto& b) { return a.minX < b.minX; });
    std::sort(groups.begin(), groups.end(), [](const auto& a, const auto& b) { return a.centerY < b.centerY; });
    return groups;
}

Candidate ParseLine(const std::wstring& text, float lineScore, float centerY) {
    const AttributeMatch attribute = MatchAttribute(text);
    if (!attribute.valid) return {};
    const auto numbers = ExtractNumbers(text);
    Candidate best;
    bool hasBest = false;
    for (const auto& number : numbers) {
        const std::wstring normalizedAttribute = NormalizeAttributeByValue(attribute.name, number);
        const ValueMatch value = MatchValue(normalizedAttribute, number);
        if (!value.valid) continue;
        const float confidenceScore = attribute.score * .46f + lineScore * .24f + (value.exact ? .30f : .17f);
        Candidate candidate;
        candidate.attribute = normalizedAttribute;
        candidate.value = value.value;
        candidate.confidenceScore = confidenceScore;
        candidate.confidence = confidenceScore >= .76f && value.exact ? ParsedOcrConfidence::High : ParsedOcrConfidence::Medium;
        candidate.sourceText = text;
        candidate.y = centerY;
        candidate.ocrScore = lineScore;
        candidate.exactValue = value.exact;
        if (!hasBest || candidate.confidenceScore > best.confidenceScore) { best = std::move(candidate); hasBest = true; }
    }
    if (!hasBest) return {};
    return best;
}

std::vector<Candidate> BuildCandidates(const std::vector<NativeOcrLine>& lines) {
    const auto boxes = BuildBoxes(lines);
    const auto groups = GroupRows(boxes);
    std::vector<Candidate> output;
    for (const auto& group : groups) {
        std::wstring text;
        float score = 0.0f;
        for (const auto& item : group.items) {
            if (!text.empty()) text.push_back(L' ');
            text += item.text;
            score += item.score;
        }
        if (!group.items.empty()) score /= static_cast<float>(group.items.size());
        Candidate parsed = ParseLine(text, score, group.centerY);
        if (!parsed.attribute.empty()) output.push_back(std::move(parsed));
    }

    struct AttributeBox { OcrBox box; AttributeMatch match; };
    std::vector<AttributeBox> attributes;
    std::vector<OcrBox> numbers;
    for (const auto& box : boxes) {
        const AttributeMatch match = MatchAttribute(box.text);
        if (match.valid) attributes.push_back({box, match});
        if (!ExtractNumbers(box.text).empty()) numbers.push_back(box);
    }
    for (const auto& item : attributes) {
        std::vector<OcrBox> candidates;
        for (const auto& number : numbers) {
            if (number.index == item.box.index || number.minX < item.box.minX) continue;
            const float tolerance = std::max(22.0f, std::max(number.height, item.box.height) * 1.15f);
            if (std::abs(number.centerY - item.box.centerY) <= tolerance) candidates.push_back(number);
        }
        std::sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
            const float ay = std::abs(a.centerY - item.box.centerY);
            const float by = std::abs(b.centerY - item.box.centerY);
            if (ay != by) return ay < by;
            return a.minX < b.minX;
        });
        const std::size_t count = std::min<std::size_t>(2, candidates.size());
        for (std::size_t index = 0; index < count; ++index) {
            const auto& number = candidates[index];
            Candidate parsed = ParseLine(item.box.text + L" " + number.text,
                (item.box.score + number.score) / 2.0f,
                (item.box.centerY + number.centerY) / 2.0f);
            if (!parsed.attribute.empty()) output.push_back(std::move(parsed));
        }
    }
    return output;
}

std::vector<Candidate> Deduplicate(std::vector<Candidate> candidates) {
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        if (a.y != b.y) return a.y < b.y;
        return a.confidenceScore > b.confidenceScore;
    });
    std::vector<Candidate> output;
    for (const auto& candidate : candidates) {
        auto it = std::find_if(output.begin(), output.end(), [&](const auto& row) {
            return row.attribute == candidate.attribute && row.value == candidate.value && std::abs(row.y - candidate.y) < 18.0f;
        });
        if (it == output.end()) output.push_back(candidate);
        else if (candidate.confidenceScore > it->confidenceScore) *it = candidate;
    }
    return output;
}

float CombinationScore(std::vector<Candidate> rows) {
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.y < b.y; });
    std::set<std::wstring> unique;
    float confidence = 0.0f, lower = 0.0f;
    std::vector<float> gaps;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        unique.insert(rows[i].attribute);
        confidence += rows[i].confidenceScore;
        lower += rows[i].y;
        if (i > 0 && rows[i].y > rows[i - 1].y) gaps.push_back(rows[i].y - rows[i - 1].y);
    }
    const float mean = gaps.empty() ? 0.0f : std::accumulate(gaps.begin(), gaps.end(), 0.0f) / static_cast<float>(gaps.size());
    float variance = 0.0f;
    for (float gap : gaps) variance += (gap - mean) * (gap - mean);
    if (!gaps.empty()) variance /= static_cast<float>(gaps.size());
    const float spacingPenalty = mean > 0.0f ? std::sqrt(variance) / mean : 1.0f;
    const float uniqueCount = static_cast<float>(unique.size());
    return confidence + uniqueCount * 1.4f - (5.0f - uniqueCount) * 3.0f - spacingPenalty * 1.1f + (lower / 5.0f) * .0008f;
}

std::vector<Candidate> ChooseBestFive(std::vector<Candidate> candidates) {
    candidates = Deduplicate(std::move(candidates));
    if (candidates.size() <= 5) {
        std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.y < b.y; });
        return candidates;
    }
    if (candidates.size() > 24) {
        std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.confidenceScore > b.confidenceScore; });
        candidates.resize(24);
        std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.y < b.y; });
    }
    float bestScore = -std::numeric_limits<float>::infinity();
    std::vector<Candidate> best;
    std::vector<Candidate> chosen;
    std::function<void(std::size_t)> pick = [&](std::size_t start) {
        if (chosen.size() == 5) {
            const float score = CombinationScore(chosen);
            if (score > bestScore) { bestScore = score; best = chosen; }
            return;
        }
        const std::size_t needed = 5 - chosen.size();
        for (std::size_t index = start; index + needed <= candidates.size(); ++index) {
            chosen.push_back(candidates[index]);
            pick(index + 1);
            chosen.pop_back();
        }
    };
    pick(0);
    if (best.empty()) best.assign(candidates.end() - 5, candidates.end());
    std::sort(best.begin(), best.end(), [](const auto& a, const auto& b) { return a.y < b.y; });
    return best;
}

}  // namespace

std::vector<ParsedOcrRow> SelectMainCompatibleOcrRows(const std::vector<NativeOcrLine>& lines) {
    const auto candidates = BuildCandidates(lines);
    const auto selected = ChooseBestFive(candidates);
    std::vector<ParsedOcrRow> output;
    output.reserve(selected.size());
    for (const auto& candidate : selected) output.push_back(candidate);
    return output;
}
