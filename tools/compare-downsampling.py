#!/usr/bin/env python3
"""Offline filter comparison. Requires Pillow; does not change live rendering."""
import argparse
from pathlib import Path
from PIL import Image, ImageDraw

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('image', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--ratio', type=float, default=0.625)
a = p.parse_args()
if not 0 < a.ratio < 1:
    p.error('--ratio must be between 0 and 1')
a.output.mkdir(parents=True, exist_ok=True)
source = Image.open(a.image).convert('RGB')
size = tuple(round(v * a.ratio) for v in source.size)
methods = {
    'nearest': Image.Resampling.NEAREST,
    'bilinear-prefiltered': Image.Resampling.BILINEAR,
    'box': Image.Resampling.BOX,
    'bicubic': Image.Resampling.BICUBIC,
    'lanczos': Image.Resampling.LANCZOS,
}
# A fixed-footprint bilinear sample resembles GL_LINEAR more closely than
# Pillow.resize(BILINEAR), which widens its kernel when reducing an image.
images = {'bilinear-gpu-like': source.transform(
    size, Image.Transform.AFFINE,
    (source.width / size[0], 0, 0, 0, source.height / size[1], 0),
    resample=Image.Resampling.BILINEAR)}
images.update({name: source.resize(size, method) for name, method in methods.items()})
for name, result in images.items():
    result.save(a.output / f'{name}.png')
# Relative regions suit the supplied login screenshot: title and login text.
regions = [(0.008, 0.008, 0.20, 0.040), (0.42, 0.29, 0.57, 0.42)]
sheet = Image.new('RGB', (1100, 70 + 400 * len(images)), '#303030')
draw = ImageDraw.Draw(sheet)
draw.text((16, 12), f'{source.size} -> {size}; ratio {a.ratio}; screenshot input, not raw buffer', fill='white')
draw.text((220, 38), 'Output pixels at 1:1', fill='white')
draw.text((570, 38), 'Same output enlarged 2x with nearest (inspection)', fill='white')
for row, (name, result) in enumerate(images.items()):
    y = 70 + row * 400
    draw.text((12, y + 8), name, fill='white')
    for region, dy in zip(regions, [0, 55]):
        bounds = tuple(round(v * result.size[i % 2]) for i, v in enumerate(region))
        crop = result.crop(bounds)
        sheet.paste(crop, (220, y + dy))
        sheet.paste(crop.resize((crop.width * 2, crop.height * 2), Image.Resampling.NEAREST), (570, y + dy * 2))
sheet.save(a.output / 'comparison.png')
print(a.output / 'comparison.png')
