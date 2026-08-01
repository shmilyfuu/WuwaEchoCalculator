from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter, ImageOps

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "public" / "assets"
NATIVE = ROOT / "native"
ASSETS.mkdir(parents=True, exist_ok=True)
NATIVE.mkdir(parents=True, exist_ok=True)

size = 1024
base = Image.new("RGBA", (size, size), (0, 0, 0, 0))
margin = 58

shade = Image.linear_gradient("L").resize((size, size)).rotate(-45, expand=False)
tile = ImageOps.colorize(shade, black="#0c3885", white="#2b8fff").convert("RGBA")
mask = Image.new("L", (size, size), 0)
ImageDraw.Draw(mask).rounded_rectangle((margin, margin, size - margin, size - margin), radius=220, fill=255)
tile.putalpha(mask)

shadow = Image.new("RGBA", (size, size), (0, 0, 0, 0))
ImageDraw.Draw(shadow).rounded_rectangle(
    (margin + 8, margin + 24, size - margin + 8, size - margin + 24),
    radius=220,
    fill=(0, 0, 0, 105),
)
base.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(34)))
base.alpha_composite(tile)

draw = ImageDraw.Draw(base)
center = (512, 500)
for radius, width, color in (
    (330, 34, (210, 239, 255, 245)),
    (245, 40, (255, 255, 255, 255)),
    (160, 44, (157, 224, 255, 255)),
):
    box = (center[0] - radius, center[1] - radius, center[0] + radius, center[1] + radius)
    for start, end in ((-52, 38), (52, 142), (158, 248), (262, 352)):
        draw.arc(box, start=start, end=end, fill=color, width=width)

draw.polygon(((512, 355), (657, 500), (512, 645), (367, 500)), fill="white")
draw.polygon(((512, 408), (604, 500), (512, 592), (420, 500)), fill="#2c84eb")
draw.ellipse((475, 463, 549, 537), fill="white")
for index in range(5):
    x = 382 + index * 65
    radius = 19 if index == 2 else 15
    draw.ellipse((x - radius, 770 - radius, x + radius, 770 + radius), fill=(220, 244, 255, 240))
draw.rounded_rectangle((margin, margin, size - margin, size - margin), radius=220, outline=(255, 255, 255, 58), width=10)

base.save(ASSETS / "app-icon.png")
base.save(
    NATIVE / "app.ico",
    format="ICO",
    sizes=[(16, 16), (20, 20), (24, 24), (32, 32), (40, 40), (48, 48), (64, 64), (128, 128), (256, 256)],
)
