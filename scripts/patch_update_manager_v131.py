from pathlib import Path

PATH = Path('native/update_manager_fixed.cpp')
text = PATH.read_text(encoding='utf-8')
count = text.count('1.3.0')
if count < 1:
    raise RuntimeError('update manager does not contain the expected v1.3.0 user-agent literal')
text = text.replace('1.3.0', '1.3.1')
PATH.write_text(text, encoding='utf-8')
print(f'Updated {PATH} to v1.3.1 ({count} replacements)')
