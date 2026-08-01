from pathlib import Path
import sys
import yaml

source = Path(sys.argv[1])
target = Path(sys.argv[2])
data = yaml.safe_load(source.read_text(encoding='utf-8'))
characters = data.get('PostProcess', {}).get('character_dict')
if not isinstance(characters, list) or not characters:
    raise RuntimeError('character_dict was not found in inference.yml')
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text('\n'.join(str(item) for item in characters) + '\n', encoding='utf-8')
print(f'Extracted {len(characters)} OCR characters to {target}')
