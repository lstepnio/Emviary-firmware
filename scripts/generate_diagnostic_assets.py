#!/usr/bin/env python3
"""Offline asset generator. Requires Pillow/qrcode and an OFL Noto Sans font file."""
import argparse,math
from pathlib import Path
from PIL import Image,ImageFont,ImageDraw
import qrcode
p=argparse.ArgumentParser();p.add_argument('font');p.add_argument('output');a=p.parse_args()
out=['/* Generated offline from OFL Noto Sans. See diagnostic_assets/OFL.txt. */']
for name,size,weight in [('label',14,400),('value',22,400),('title',32,500)]:
 f=ImageFont.truetype(a.font,size);f.set_variation_by_axes([weight,100]);h=sum(f.getmetrics());data=[];glyphs=[]
 for code in list(range(32,127))+list(range(160,256)):
  c=chr(code);advance=math.ceil(f.getlength(c));w=advance+2;stride=(w+7)//8
  im=Image.new('1',(w,h),0);ImageDraw.Draw(im).text((1,0),c,font=f,fill=1)
  glyphs.append((len(data),w,advance,stride))
  for y in range(h):
   for block in range(stride):
    byte=0
    for bit in range(8):
     x=block*8+bit
     if x<w and im.getpixel((x,y)):byte|=1<<(7-bit)
    data.append(byte)
 out.append(f'static const unsigned char {name}_bits[]={{'+','.join(map(str,data))+'};')
 out.append(f'static const diagnostic_glyph_t {name}_glyphs[]={{'+','.join('{%d,%d,%d,%d}'%g for g in glyphs)+'};')
 out.append(f'static const diagnostic_font_t font_{name}={{ {name}_bits,{name}_glyphs,{h} }};')
q=qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_M,box_size=1,border=4)
q.add_data('https://emviary.majjix.com/');q.make(fit=True);m=q.get_matrix();assert len(m)==37
out.append('#define DIAGNOSTIC_QR_SIZE 37')
out.append('static const unsigned char diagnostic_qr[]={' + ','.join(str(int(v)) for row in m for v in row)+'};')
Path(a.output).write_text('\n'.join(out)+'\n')
