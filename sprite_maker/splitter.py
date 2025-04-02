from PIL import Image
import numpy as np
import argparse
import os

parser = argparse.ArgumentParser(
                    prog='SpriteMaker',
                    description='Convert images to their Graphics.Sprite representations')
parser.add_argument('img', nargs='?', type=argparse.FileType('rb'))
parser.add_argument('-s', '--graysplit', type=int, default=128)

inputs = parser.parse_args()

# Open the image
img = Image.open(inputs.img)
img_array = np.array(img)

# Ensure image is in RGB mode
if img.mode != "RGB":
    img = img.convert("RGB")
    img_array = np.array(img)

# Get the top row of the image
top_row = img_array[0]

# Define gray color (128, 128, 128)
gray = np.array([inputs.graysplit, inputs.graysplit, inputs.graysplit])

# Find indices where the topmost pixel is gray
split_indices = [i for i in range(len(top_row)) if np.array_equal(top_row[i], gray)]

# Create output folder
base_name = os.path.splitext(os.path.basename(inputs.img.name))[0]
output_folder = os.path.join(os.path.dirname(inputs.img.name), base_name)
os.makedirs(output_folder, exist_ok=True)

# Split and save slices
start = 0
count = 1
for index in split_indices:
    if start < index:
        slice_img = img.crop((start, 0, index, img.height))
        slice_img.save(os.path.join(output_folder, f"slice_{count}.png"))
        count += 1
    start = index + 1

# Save the last slice if there's any remaining part
if start < img.width:
    slice_img = img.crop((start, 0, img.width, img.height))
    slice_img.save(os.path.join(output_folder, f"slice_{count}.png"))
