from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

out = Path('build-tests/ocr_smoke.png')
out.parent.mkdir(parents=True, exist_ok=True)

image = Image.new('RGB', (1000, 700), '#202932')
draw = ImageDraw.Draw(image)
font_path = Path(r'C:\Windows\Fonts\msyh.ttc')
if not font_path.exists():
    font_path = Path(r'C:\Windows\Fonts\msyhbd.ttc')
font = ImageFont.truetype(str(font_path), 34)
small = ImageFont.truetype(str(font_path), 25)

draw.rounded_rectangle((80, 70, 920, 630), radius=16, fill='#303b45')
draw.text((120, 95), '声骸属性', font=font, fill='white')
rows = [
    ('生命百分比', '7.1%'),
    ('生命', '390'),
    ('防御百分比', '9.0%'),
    ('暴击伤害', '16.2%'),
    ('攻击百分比', '10.1%'),
]
for index, (name, value) in enumerate(rows):
    top = 175 + index * 82
    draw.rounded_rectangle((110, top, 890, top + 58), radius=6, fill='#46515a')
    draw.text((145, top + 8), name, font=small, fill='white')
    value_box = draw.textbbox((0, 0), value, font=small)
    value_width = value_box[2] - value_box[0]
    draw.text((840 - value_width, top + 8), value, font=small, fill='white')

image.save(out)
print(out)
