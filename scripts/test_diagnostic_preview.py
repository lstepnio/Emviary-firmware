#!/usr/bin/env python3
"""Decode QR in host previews rendered by actual diagnostic_layout.c geometry.
Optional preview verification requires Pillow and opencv-python-headless.
"""
import argparse
from pathlib import Path
import cv2
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('prefix');a=p.parse_args()
for orientation in ('landscape','portrait','offline-landscape','offline-portrait'):
    path=Path(a.prefix+'-'+orientation+'.pbm')
    im=Image.open(path);output=path.with_suffix('.png');im.save(output)
    decoded,_,_=cv2.QRCodeDetector().detectAndDecode(cv2.imread(str(output)))
    assert decoded=='https://emviary.majjix.com/',(orientation,decoded)
    # Same geometry after the physical framebuffer rotation remains scannable.
    rotated=cv2.rotate(cv2.imread(str(output)),cv2.ROTATE_90_CLOCKWISE)
    decoded,_,_=cv2.QRCodeDetector().detectAndDecode(rotated)
    assert decoded=='https://emviary.majjix.com/',(orientation,decoded)
    print(output,decoded)
