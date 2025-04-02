import os
from PIL import Image
import numpy as np
import argparse
import sys

parser = argparse.ArgumentParser(
                    prog='AlphabetMaker',
                    description='Convert folder of images to a Graphics.Typer representation')
parser.add_argument('folder', nargs='?')
parser.add_argument('-t', '--threshold', type=int, default=128)
parser.add_argument('-i', '--invert', action='store_const', default=False, const=True)

inputs = parser.parse_args()

height = 0
stride = 0
imglist = bytearray()

directory = os.fsencode(inputs.folder)

for file in os.listdir(directory):
    filename = os.fsdecode(file)
    if not filename.endswith(".png"): 
        continue
    print(f"Opening {filename}...")

    img = Image.open(os.path.join(directory, file)).convert('L')
    ary = np.transpose(np.array(img))
    temp_width = len(ary)
    temp_height = len(ary[0])
    temp_stride = (temp_height+7)//8
    print(f"{filename} has {temp_width}x{temp_height}")
    if temp_width>200 or temp_height>200:
        print(f"Image ({filename}) too big, discarding...")
        continue
    if height==0:
        height = temp_height
        stride = temp_stride
        print(f"Height/Stride seems to be {height}/{stride}")

    if temp_height!=height:
        print(f"Image ({filename}) with wrong height, discarding...")
        continue
    
    rawname = filename.split('.')[0]
    codename = bytearray()
    if len(rawname)==0: continue
    if len(rawname)!=1:
        if rawname[0]=='!':
            code = int(rawname[1:], 16)
            if code<=0x7F:
                codename = bytearray([code])
            elif code<=0x7FF:
                codename = bytearray([0xC0|(code>>6), 0x80|(code&0x3F) ])
            elif code<=0xFFF:
                codename = bytearray([0xE0|(code>>12), 0x80|((code>>6)&0x3F), 0x80|(code&0x3F) ])
            elif code<=0x10FFFF:
                codename = bytearray([0xF0|(code>>18), 0x80|((code>>12)&0x3F), 0x80|((code>>6)&0x3F), 0x80|(code&0x3F) ])
            else:
                print(f"invalid utf8 {rawname}, continuing...")
                continue
    else:
        codename = bytearray(rawname, 'utf8')

    # imgdata = codename+bytearray([temp_width])
    rawdata = bytearray()
    for col in ary:
        for i in range(stride):
            byte = 0
            for j in range(8):
                if i*8+j<height:
                    if bool(inputs.invert) == bool(col[i*8+j]<inputs.threshold):
                        byte = byte | (1<<j)
            rawdata.append(byte)
    prepad = 0
    postpad = 0
    check = True
    while check and prepad<15 and len(rawdata)>stride:
        for i in range(stride):
            if rawdata[i]!=0:
                check = False
        if check:
            rawdata = rawdata[stride:]
            prepad = prepad+1
    
    check = True
    while check and postpad<15 and len(rawdata)>stride:
        for i in range(stride):
            if rawdata[len(rawdata)-(i+1)]!=0:
                check = False
        if check:
            rawdata = rawdata[:len(rawdata)-stride]
            postpad = postpad+1
    
    # imgdata = codename+bytearray([(prepad<<4)|postpad, temp_width])
    imglist = imglist+codename+bytearray([(prepad<<4)|postpad, temp_width-(prepad+postpad)])+rawdata

print("")
print("")
print("")
print(f"Height: {height}")
print(f"Stride: {stride}")
print(repr(bytes(imglist)))
