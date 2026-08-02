#define NOMINMAX
#include "ocr_engine.h"

#include <onnxruntime_cxx_api.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numeric>
#include <queue>
#include <sstream>
#include <cstdio>

namespace {

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring output(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), count);
    return output;
}

std::wstring OrtError(const Ort::Exception& error) {
    return Utf8ToWide(error.what());
}

int RoundTo32(int value) {
    return std::max(32, static_cast<int>(std::round(value / 32.0)) * 32);
}

float SampleChannel(const std::vector<std::uint8_t>& pixels, int width, int height, int stride,
                    float x, float y, int channel) {
    x = std::clamp(x, 0.0f, static_cast<float>(width - 1));
    y = std::clamp(y, 0.0f, static_cast<float>(height - 1));
    const int x0 = static_cast<int>(x);
    const int y0 = static_cast<int>(y);
    const int x1 = std::min(width - 1, x0 + 1);
    const int y1 = std::min(height - 1, y0 + 1);
    const float fx = x - x0;
    const float fy = y - y0;
    const auto at = [&](int px, int py) {
        return static_cast<float>(pixels[static_cast<std::size_t>(py) * stride + px * 4 + channel]);
    };
    const float a = at(x0, y0) * (1.0f - fx) + at(x1, y0) * fx;
    const float b = at(x0, y1) * (1.0f - fx) + at(x1, y1) * fx;
    return a * (1.0f - fy) + b * fy;
}

struct PreparedImage {
    std::vector<std::uint8_t> pixels;
    int width = 0;
    int height = 0;
    int stride = 0;
};

PreparedImage PreprocessLikeMain(const std::vector<std::uint8_t>& bgra,
                                 int width, int height, int stride) {
    const int shortest = std::min(width, height);
    const int longest = std::max(width, height);
    float scale = 1.0f;
    if (shortest < 720) scale = std::min(3.2f, 720.0f / static_cast<float>(std::max(1, shortest)));
    if (static_cast<float>(longest) * scale > 1900.0f) scale = 1900.0f / static_cast<float>(longest);
    const int scaledWidth = std::max(1, static_cast<int>(std::lround(width * scale)));
    const int scaledHeight = std::max(1, static_cast<int>(std::lround(height * scale)));
    const int padding = std::max(20, static_cast<int>(std::lround(24.0f * scale)));

    PreparedImage output;
    output.width = scaledWidth + padding * 2;
    output.height = scaledHeight + padding * 2;
    output.stride = output.width * 4;
    output.pixels.resize(static_cast<std::size_t>(output.stride) * output.height);

    // Canvas background used by main: #101a25, stored as BGRA.
    for (int y = 0; y < output.height; ++y) {
        auto* row = output.pixels.data() + static_cast<std::size_t>(y) * output.stride;
        for (int x = 0; x < output.width; ++x) {
            row[x * 4 + 0] = 0x25;
            row[x * 4 + 1] = 0x1a;
            row[x * 4 + 2] = 0x10;
            row[x * 4 + 3] = 0xff;
        }
    }

    const float sourceScaleX = static_cast<float>(width) / scaledWidth;
    const float sourceScaleY = static_cast<float>(height) / scaledHeight;
    for (int y = 0; y < scaledHeight; ++y) {
        auto* destination = output.pixels.data() + static_cast<std::size_t>(y + padding) * output.stride + padding * 4;
        for (int x = 0; x < scaledWidth; ++x) {
            const float sourceX = (x + .5f) * sourceScaleX - .5f;
            const float sourceY = (y + .5f) * sourceScaleY - .5f;
            destination[x * 4 + 0] = static_cast<std::uint8_t>(std::clamp(std::lround(SampleChannel(bgra, width, height, stride, sourceX, sourceY, 0)), 0l, 255l));
            destination[x * 4 + 1] = static_cast<std::uint8_t>(std::clamp(std::lround(SampleChannel(bgra, width, height, stride, sourceX, sourceY, 1)), 0l, 255l));
            destination[x * 4 + 2] = static_cast<std::uint8_t>(std::clamp(std::lround(SampleChannel(bgra, width, height, stride, sourceX, sourceY, 2)), 0l, 255l));
            destination[x * 4 + 3] = 0xff;
        }
    }
    return output;
}

struct Box {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    float score = 0.0f;
};

}  // namespace

struct NativeOcrEngine::Impl {
    Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "WuwaEchoCalculator"};
    Ort::SessionOptions options;
    std::unique_ptr<Ort::Session> detector;
    std::unique_ptr<Ort::Session> recognizer;
    std::string detectorInput;
    std::string detectorOutput;
    std::string recognizerInput;
    std::string recognizerOutput;
    std::vector<std::wstring> dictionary;
    bool ready = false;

    Impl() {
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        options.SetIntraOpNumThreads(std::max(1u, std::thread::hardware_concurrency() / 2));
        options.SetInterOpNumThreads(1);
    }

    static std::string InputName(Ort::Session& session, std::size_t index) {
        Ort::AllocatorWithDefaultOptions allocator;
        auto name = session.GetInputNameAllocated(index, allocator);
        return name ? std::string(name.get()) : std::string();
    }

    static std::string OutputName(Ort::Session& session, std::size_t index) {
        Ort::AllocatorWithDefaultOptions allocator;
        auto name = session.GetOutputNameAllocated(index, allocator);
        return name ? std::string(name.get()) : std::string();
    }

    bool LoadDictionary(const std::filesystem::path& path, std::wstring& error) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            error = L"缺少 OCR 字符表：" + path.wstring();
            return false;
        }
        dictionary.clear();
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            dictionary.push_back(Utf8ToWide(line));
        }
        if (dictionary.empty()) {
            error = L"OCR 字符表为空";
            return false;
        }
        return true;
    }

    std::vector<float> BuildDetectorInput(const std::vector<std::uint8_t>& bgra,
                                          int width, int height, int stride,
                                          int& targetWidth, int& targetHeight) {
        const float scale = std::min(1.0f, 1600.0f / static_cast<float>(std::max(width, height)));
        targetWidth = RoundTo32(static_cast<int>(std::round(width * scale)));
        targetHeight = RoundTo32(static_cast<int>(std::round(height * scale)));
        std::vector<float> tensor(static_cast<std::size_t>(3) * targetWidth * targetHeight);
        constexpr float mean[3] = {0.485f, 0.456f, 0.406f};
        constexpr float invStd[3] = {1.0f / 0.229f, 1.0f / 0.224f, 1.0f / 0.225f};
        const float sx = static_cast<float>(width) / targetWidth;
        const float sy = static_cast<float>(height) / targetHeight;
        const std::size_t plane = static_cast<std::size_t>(targetWidth) * targetHeight;
        for (int y = 0; y < targetHeight; ++y) {
            for (int x = 0; x < targetWidth; ++x) {
                const float sourceX = (x + 0.5f) * sx - 0.5f;
                const float sourceY = (y + 0.5f) * sy - 0.5f;
                const std::size_t index = static_cast<std::size_t>(y) * targetWidth + x;
                for (int channel = 0; channel < 3; ++channel) {
                    const float value = SampleChannel(bgra, width, height, stride, sourceX, sourceY, channel) / 255.0f;
                    tensor[static_cast<std::size_t>(channel) * plane + index] = (value - mean[channel]) * invStd[channel];
                }
            }
        }
        return tensor;
    }

    std::vector<Box> Detect(const std::vector<std::uint8_t>& bgra, int width, int height, int stride,
                            std::atomic_bool& cancelFlag, std::wstring& error) {
        int inputWidth = 0;
        int inputHeight = 0;
        auto input = BuildDetectorInput(bgra, width, height, stride, inputWidth, inputHeight);
        if (cancelFlag.load()) return {};

        const std::array<std::int64_t, 4> shape{1, 3, inputHeight, inputWidth};
        auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        auto tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(), shape.data(), shape.size());
        const char* inputNames[] = {detectorInput.c_str()};
        const char* outputNames[] = {detectorOutput.c_str()};
        Ort::RunOptions runOptions;
        std::fprintf(stderr,"[ocr] detector input=%dx%d\n",inputWidth,inputHeight);
        auto outputs = detector->Run(runOptions, inputNames, &tensor, 1, outputNames, 1);
        std::fprintf(stderr,"[ocr] detector run complete outputs=%zu\n",outputs.size());
        if (outputs.empty() || !outputs[0].IsTensor()) {
            error = L"文字检测模型没有返回张量";
            return {};
        }
        auto info = outputs[0].GetTensorTypeAndShapeInfo();
        const auto dimensions = info.GetShape();
        if (dimensions.size() < 3) {
            error = L"文字检测模型输出尺寸异常";
            return {};
        }
        const int mapHeight = static_cast<int>(dimensions[dimensions.size() - 2]);
        const int mapWidth = static_cast<int>(dimensions[dimensions.size() - 1]);
        std::fprintf(stderr,"[ocr] detector output=%dx%d type=%d\n",mapWidth,mapHeight,static_cast<int>(info.GetElementType()));
        const float* probability = outputs[0].GetTensorData<float>();
        const std::size_t mapSize = static_cast<std::size_t>(mapWidth) * mapHeight;
        std::vector<std::uint8_t> visited(mapSize, 0);
        std::vector<Box> boxes;
        std::queue<int> queue;
        constexpr float threshold = 0.25f;
        constexpr int dx[8] = {1,-1,0,0,1,1,-1,-1};
        constexpr int dy[8] = {0,0,1,-1,1,-1,1,-1};

        for (int y = 0; y < mapHeight; ++y) {
            if (cancelFlag.load()) return {};
            for (int x = 0; x < mapWidth; ++x) {
                const int start = y * mapWidth + x;
                if (visited[start] || probability[start] < threshold) continue;
                visited[start] = 1;
                queue.push(start);
                int minX = x, maxX = x, minY = y, maxY = y, count = 0;
                double scoreSum = 0.0;
                while (!queue.empty()) {
                    const int current = queue.front();
                    queue.pop();
                    const int cx = current % mapWidth;
                    const int cy = current / mapWidth;
                    minX = std::min(minX, cx); maxX = std::max(maxX, cx);
                    minY = std::min(minY, cy); maxY = std::max(maxY, cy);
                    scoreSum += probability[current];
                    ++count;
                    for (int direction = 0; direction < 8; ++direction) {
                        const int nx = cx + dx[direction];
                        const int ny = cy + dy[direction];
                        if (nx < 0 || ny < 0 || nx >= mapWidth || ny >= mapHeight) continue;
                        const int next = ny * mapWidth + nx;
                        if (!visited[next] && probability[next] >= threshold) {
                            visited[next] = 1;
                            queue.push(next);
                        }
                    }
                }
                const int boxWidth = maxX - minX + 1;
                const int boxHeight = maxY - minY + 1;
                const float score = count ? static_cast<float>(scoreSum / count) : 0.0f;
                if (count < 4 || boxWidth < 2 || boxHeight < 2 || score < 0.32f) continue;
                const float scaleX = static_cast<float>(width) / mapWidth;
                const float scaleY = static_cast<float>(height) / mapHeight;
                const float paddingX = boxWidth * scaleX * 0.35f + 3.0f;
                const float paddingY = boxHeight * scaleY * 0.35f + 3.0f;
                Box box;
                box.left = std::max(0, static_cast<int>(std::floor(minX * scaleX - paddingX)));
                box.top = std::max(0, static_cast<int>(std::floor(minY * scaleY - paddingY)));
                box.right = std::min(width, static_cast<int>(std::ceil((maxX + 1) * scaleX + paddingX)));
                box.bottom = std::min(height, static_cast<int>(std::ceil((maxY + 1) * scaleY + paddingY)));
                box.score = score;
                if (box.right - box.left >= 8 && box.bottom - box.top >= 6) boxes.push_back(box);
            }
        }

        std::sort(boxes.begin(), boxes.end(), [](const Box& a, const Box& b) {
            const int ay = (a.top + a.bottom) / 2;
            const int by = (b.top + b.bottom) / 2;
            if (std::abs(ay - by) > 8) return ay < by;
            return a.left < b.left;
        });
        if (boxes.size() > 120) boxes.resize(120);
        std::fprintf(stderr,"[ocr] detector boxes=%zu\n",boxes.size());
        return boxes;
    }

    std::vector<float> BuildRecognizerInput(const std::vector<std::uint8_t>& bgra,
                                            int width, int height, int stride,
                                            const Box& box, int& targetWidth) {
        const int cropWidth = std::max(1, box.right - box.left);
        const int cropHeight = std::max(1, box.bottom - box.top);
        targetWidth = std::clamp(static_cast<int>(std::ceil(48.0 * cropWidth / cropHeight)), 32, 960);
        targetWidth = ((targetWidth + 7) / 8) * 8;
        constexpr int targetHeight = 48;
        std::vector<float> tensor(static_cast<std::size_t>(3) * targetWidth * targetHeight);
        const float sx = static_cast<float>(cropWidth) / targetWidth;
        const float sy = static_cast<float>(cropHeight) / targetHeight;
        const std::size_t plane = static_cast<std::size_t>(targetWidth) * targetHeight;
        for (int y = 0; y < targetHeight; ++y) {
            for (int x = 0; x < targetWidth; ++x) {
                const float sourceX = box.left + (x + 0.5f) * sx - 0.5f;
                const float sourceY = box.top + (y + 0.5f) * sy - 0.5f;
                const std::size_t index = static_cast<std::size_t>(y) * targetWidth + x;
                for (int channel = 0; channel < 3; ++channel) {
                    const float value = SampleChannel(bgra, width, height, stride, sourceX, sourceY, channel) / 255.0f;
                    tensor[static_cast<std::size_t>(channel) * plane + index] = (value - 0.5f) / 0.5f;
                }
            }
        }
        return tensor;
    }

    NativeOcrLine RecognizeBox(const std::vector<std::uint8_t>& bgra, int width, int height, int stride,
                               const Box& box, std::wstring& error) {
        int inputWidth = 0;
        auto input = BuildRecognizerInput(bgra, width, height, stride, box, inputWidth);
        const std::array<std::int64_t, 4> shape{1, 3, 48, inputWidth};
        auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        auto tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(), shape.data(), shape.size());
        const char* inputNames[] = {recognizerInput.c_str()};
        const char* outputNames[] = {recognizerOutput.c_str()};
        Ort::RunOptions runOptions;
        std::fprintf(stderr,"[ocr] recognizer box=(%d,%d)-(%d,%d) inputWidth=%d\n",box.left,box.top,box.right,box.bottom,inputWidth);
        auto outputs = recognizer->Run(runOptions, inputNames, &tensor, 1, outputNames, 1);
        std::fprintf(stderr,"[ocr] recognizer run complete outputs=%zu\n",outputs.size());
        NativeOcrLine line;
        line.left = box.left; line.top = box.top; line.right = box.right; line.bottom = box.bottom;
        if (outputs.empty() || !outputs[0].IsTensor()) {
            error = L"文字识别模型没有返回张量";
            return line;
        }
        auto info = outputs[0].GetTensorTypeAndShapeInfo();
        const auto dimensions = info.GetShape();
        if (dimensions.size() < 3) {
            error = L"文字识别模型输出尺寸异常";
            return line;
        }
        const int steps = static_cast<int>(dimensions[dimensions.size() - 2]);
        const int classes = static_cast<int>(dimensions[dimensions.size() - 1]);
        std::fprintf(stderr,"[ocr] recognizer output steps=%d classes=%d type=%d\n",steps,classes,static_cast<int>(info.GetElementType()));
        const float* logits = outputs[0].GetTensorData<float>();
        int previous = -1;
        float confidenceSum = 0.0f;
        int confidenceCount = 0;
        for (int step = 0; step < steps; ++step) {
            const float* row = logits + static_cast<std::size_t>(step) * classes;
            float maximum = -std::numeric_limits<float>::infinity();
            int best = 0;
            for (int index = 0; index < classes; ++index) {
                if (row[index] > maximum) { maximum = row[index]; best = index; }
            }
            bool normalizedProbability = true;
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
            }
            if (best != 0 && best != previous && best - 1 < static_cast<int>(dictionary.size())) {
                line.text += dictionary[static_cast<std::size_t>(best - 1)];
                confidenceSum += probability;
                ++confidenceCount;
            }
            previous = best;
        }
        line.confidence = confidenceCount ? confidenceSum / confidenceCount : 0.0f;
        return line;
    }
};

NativeOcrEngine::NativeOcrEngine() : impl_(std::make_unique<Impl>()) {}
NativeOcrEngine::~NativeOcrEngine() = default;

bool NativeOcrEngine::Initialize(const std::wstring& modelDirectory, std::wstring& error) {
    try {
        const std::filesystem::path directory(modelDirectory);
        const auto detectorPath = directory / L"det.onnx";
        const auto recognizerPath = directory / L"rec.onnx";
        if (!std::filesystem::exists(detectorPath) || !std::filesystem::exists(recognizerPath)) {
            error = L"缺少 PP-OCRv5 模型文件";
            return false;
        }
        if (!impl_->LoadDictionary(directory / L"dict.txt", error)) return false;
        impl_->detector = std::make_unique<Ort::Session>(impl_->environment, detectorPath.c_str(), impl_->options);
        impl_->recognizer = std::make_unique<Ort::Session>(impl_->environment, recognizerPath.c_str(), impl_->options);
        impl_->detectorInput = Impl::InputName(*impl_->detector, 0);
        impl_->detectorOutput = Impl::OutputName(*impl_->detector, 0);
        impl_->recognizerInput = Impl::InputName(*impl_->recognizer, 0);
        impl_->recognizerOutput = Impl::OutputName(*impl_->recognizer, 0);
        impl_->ready = !impl_->detectorInput.empty() && !impl_->detectorOutput.empty() &&
                       !impl_->recognizerInput.empty() && !impl_->recognizerOutput.empty();
        if (!impl_->ready) error = L"无法读取 OCR 模型输入输出名称";
        return impl_->ready;
    } catch (const Ort::Exception& exception) {
        error = L"OCR 模型加载失败：" + OrtError(exception);
        impl_->ready = false;
        return false;
    } catch (const std::exception& exception) {
        error = L"OCR 初始化失败：" + Utf8ToWide(exception.what());
        impl_->ready = false;
        return false;
    }
}

bool NativeOcrEngine::IsReady() const { return impl_ && impl_->ready; }

NativeOcrJobResult NativeOcrEngine::Recognize(const std::vector<std::uint8_t>& bgra,
                                              int width, int height, int stride,
                                              std::atomic_bool& cancelFlag) {
    NativeOcrJobResult result;
    if (!IsReady()) {
        result.error = L"OCR 模型尚未就绪";
        return result;
    }
    if (bgra.empty() || width <= 0 || height <= 0 || stride < width * 4) {
        result.error = L"OCR 输入图片无效";
        return result;
    }
    try {
        auto prepared = PreprocessLikeMain(bgra, width, height, stride);
        if (cancelFlag.load()) { result.cancelled = true; return result; }
        std::fprintf(stderr,"[ocr] recognize begin original=%dx%d prepared=%dx%d stride=%d\n",width,height,prepared.width,prepared.height,prepared.stride);
        auto boxes = impl_->Detect(prepared.pixels, prepared.width, prepared.height, prepared.stride, cancelFlag, result.error);
        if (cancelFlag.load()) { result.cancelled = true; return result; }
        if (!result.error.empty()) return result;
        if (boxes.empty()) {
            result.error = L"未检测到文字区域";
            return result;
        }
        for (const auto& box : boxes) {
            if (cancelFlag.load()) { result.cancelled = true; result.lines.clear(); return result; }
            std::wstring error;
            auto line = impl_->RecognizeBox(prepared.pixels, prepared.width, prepared.height, prepared.stride, box, error);
            if (!error.empty()) { result.error = error; return result; }
            if (!line.text.empty() && line.confidence >= 0.20f) result.lines.push_back(std::move(line));
        }
        if (result.lines.empty()) {
            result.error = L"检测到 " + std::to_wstring(boxes.size()) + L" 个文字区域，但识别结果为空";
            return result;
        }
        std::fprintf(stderr,"[ocr] accepted lines=%zu\n",result.lines.size());
        std::sort(result.lines.begin(), result.lines.end(), [](const NativeOcrLine& a, const NativeOcrLine& b) {
            const int ay = (a.top + a.bottom) / 2;
            const int by = (b.top + b.bottom) / 2;
            if (std::abs(ay - by) > 8) return ay < by;
            return a.left < b.left;
        });
    } catch (const Ort::Exception& exception) {
        result.error = L"OCR 推理失败：" + OrtError(exception);
    } catch (const std::exception& exception) {
        result.error = L"OCR 处理失败：" + Utf8ToWide(exception.what());
    }
    return result;
}
