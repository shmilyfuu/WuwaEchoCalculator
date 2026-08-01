from pathlib import Path

SOURCE = Path('native/ocr_engine_fixed.cpp')
TARGET = Path('native/ocr_engine_v130.cpp')
text = SOURCE.read_text(encoding='utf-8')


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected one match, found {count}')
    text = text.replace(old, new, 1)


prepared_image = r'''
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
'''
replace_once('\nstruct Box {', prepared_image + '\nstruct Box {', 'main image preprocessor')

# main sends textDetLimitSideLen=1600 after its Canvas preprocessing.
replace_once(
    'const float scale = std::min(1.0f, 1280.0f / static_cast<float>(std::max(width, height)));',
    'const float scale = std::min(1.0f, 1600.0f / static_cast<float>(std::max(width, height)));',
    'detector limit side',
)

# Use 8-connected components; diagonal strokes in Chinese glyphs remain in the same DB region.
replace_once(
    'constexpr int dx[4] = {1, -1, 0, 0};\n        constexpr int dy[4] = {0, 0, 1, -1};',
    'constexpr int dx[8] = {1,-1,0,0,1,1,-1,-1};\n        constexpr int dy[8] = {0,0,1,-1,1,-1,1,-1};',
    'eight connected neighborhood',
)
replace_once('for (int direction = 0; direction < 4; ++direction) {', 'for (int direction = 0; direction < 8; ++direction) {', 'eight connected loop')

# Run the detector and recognizer on the same resized, padded pixels used by main.
replace_once(
    '''        std::fprintf(stderr,"[ocr] recognize begin image=%dx%d stride=%d\\n",width,height,stride);
        auto boxes = impl_->Detect(bgra, width, height, stride, cancelFlag, result.error);''',
    '''        auto prepared = PreprocessLikeMain(bgra, width, height, stride);
        if (cancelFlag.load()) { result.cancelled = true; return result; }
        std::fprintf(stderr,"[ocr] recognize begin original=%dx%d prepared=%dx%d stride=%d\\n",width,height,prepared.width,prepared.height,prepared.stride);
        auto boxes = impl_->Detect(prepared.pixels, prepared.width, prepared.height, prepared.stride, cancelFlag, result.error);''',
    'prepared detector input',
)
replace_once(
    'auto line = impl_->RecognizeBox(bgra, width, height, stride, box, error);',
    'auto line = impl_->RecognizeBox(prepared.pixels, prepared.width, prepared.height, prepared.stride, box, error);',
    'prepared recognizer input',
)

TARGET.write_text(text, encoding='utf-8')
print(f'Generated {TARGET} ({len(text)} chars)')
