from PIL import Image
import numpy as np
import argparse
import sys

parser = argparse.ArgumentParser(
                    prog='SpriteMaker',
                    description='Convert images to their Graphics.Sprite representations')
parser.add_argument('img', nargs='?', type=argparse.FileType('rb'))
parser.add_argument('-t', '--threshold', type=int, default=128)
parser.add_argument('-i', '--invert', action='store_const', default=False, const=True)

inputs = parser.parse_args(['/mnt/c/Users/Makara/Downloads/a.png','-i'])


img = Image.open(inputs.img).convert('L')
ary = np.transpose(np.array(img))
width = len(ary)
height = len(ary[0])
stride = (height+7)//8
if width>200 or height>200:
    print("Image too big!")
    sys.exit()

data = bytearray([width,height,stride])
for col in ary:
    for i in range(stride):
        byte = 0
        for j in range(8):
            if i*8+j<height:
                if bool(inputs.invert) == bool(col[i*8+j]<inputs.threshold):
                    byte = byte | (1<<j)
        data.append(byte)

print(repr(bytes(data)))
