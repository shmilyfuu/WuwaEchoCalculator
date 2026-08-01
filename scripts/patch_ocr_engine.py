from pathlib import Path

SOURCE = Path('native/ocr_engine.cpp')
TARGET = Path('native/ocr_engine_fixed.cpp')
text = SOURCE.read_text(encoding='utf-8')


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected one match, found {count}')
    text = text.replace(old, new, 1)


# Match the parameters already used by the WebView2 release.
replace_once(
    'const float scale = std::min(1.0f, 960.0f / static_cast<float>(std::max(width, height)));',
    'const float scale = std::min(1.0f, 1280.0f / static_cast<float>(std::max(width, height)));',
    'detector max side',
)
replace_once('constexpr float threshold = 0.30f;', 'constexpr float threshold = 0.25f;', 'detector threshold')
replace_once(
    'if (count < 12 || boxWidth < 4 || boxHeight < 4 || score < 0.45f) continue;',
    'if (count < 4 || boxWidth < 2 || boxHeight < 2 || score < 0.32f) continue;',
    'detector box filter',
)
replace_once(
    'const float paddingX = boxWidth * scaleX * 0.14f + 2.0f;\n                const float paddingY = boxHeight * scaleY * 0.28f + 2.0f;',
    'const float paddingX = boxWidth * scaleX * 0.35f + 3.0f;\n                const float paddingY = boxHeight * scaleY * 0.35f + 3.0f;',
    'detector unclip',
)

# PP-OCRv5 recognition ONNX already returns normalized probabilities. The old
# implementation applied Softmax for a second time, reducing every confidence
# value to roughly 1 / class_count and filtering every recognized line.
old_confidence = '''            double denominator = 0.0;
            for (int index = 0; index < classes; ++index) denominator += std::exp(static_cast<double>(row[index] - maximum));
            const float probability = denominator > 0.0 ? static_cast<float>(1.0 / denominator) : 0.0f;'''
new_confidence = '''            bool normalizedProbability = true;
            double probabilitySum = 0.0;
            for (int index = 0; index < classes; ++index) {
                const float value = row[index];
                if (!std::isfinite(value) || value < -0.0001f || value > 1.0001f) normalizedProbability = false;
                probabilitySum += value;
            }
            float probability = 0.0f;
            if (normalizedProbability && probabilitySum > 0.80 && probabilitySum < 1.20) {
                probability = std::clamp(row[best], 0.0f, 1.0f);
            } else {
                double denominator = 0.0;
                for (int index = 0; index < classes; ++index) denominator += std::exp(static_cast<double>(row[index] - maximum));
                probability = denominator > 0.0 ? static_cast<float>(1.0 / denominator) : 0.0f;
            }'''
replace_once(old_confidence, new_confidence, 'recognizer confidence')
replace_once(
    'if (!line.text.empty() && line.confidence >= 0.18f) result.lines.push_back(std::move(line));',
    'if (!line.text.empty() && line.confidence >= 0.20f) result.lines.push_back(std::move(line));',
    'recognizer score threshold',
)

# Give a useful diagnostic instead of reporting a generic empty match.
replace_once(
    '''        auto boxes = impl_->Detect(bgra, width, height, stride, cancelFlag, result.error);
        if (cancelFlag.load()) { result.cancelled = true; return result; }
        if (!result.error.empty()) return result;
        for (const auto& box : boxes) {''',
    '''        auto boxes = impl_->Detect(bgra, width, height, stride, cancelFlag, result.error);
        if (cancelFlag.load()) { result.cancelled = true; return result; }
        if (!result.error.empty()) return result;
        if (boxes.empty()) {
            result.error = L"未检测到文字区域";
            return result;
        }
        for (const auto& box : boxes) {''',
    'empty detector diagnostic',
)
replace_once(
    '''        std::sort(result.lines.begin(), result.lines.end(), [](const NativeOcrLine& a, const NativeOcrLine& b) {''',
    '''        if (result.lines.empty()) {
            result.error = L"检测到 " + std::to_wstring(boxes.size()) + L" 个文字区域，但识别结果为空";
            return result;
        }
        std::sort(result.lines.begin(), result.lines.end(), [](const NativeOcrLine& a, const NativeOcrLine& b) {''',
    'empty recognition diagnostic',
)

TARGET.write_text(text, encoding='utf-8')
print(f'Generated {TARGET} ({len(text)} chars)')
