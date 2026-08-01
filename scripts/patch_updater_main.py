from pathlib import Path

SOURCE = Path('native/updater_main.cpp')
TARGET = Path('native/updater_main_fixed.cpp')
text = SOURCE.read_text(encoding='utf-8')
if '#include <dwmapi.h>' not in text:
    text = text.replace('#include <shellapi.h>\n', '#include <shellapi.h>\n#include <dwmapi.h>\n', 1)
TARGET.write_text(text, encoding='utf-8')
print(f'Generated {TARGET} ({len(text)} chars)')
