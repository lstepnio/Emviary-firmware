# Diagnostic dashboard assets

Noto Sans variable font (Google Fonts, SIL Open Font License) was rasterized
offline to crisp one-bit glyphs: regular 14/22px and medium 32px. The generated
Latin/Latin-1 subset lives in `../diagnostic_assets.h`; no runtime font engine or
font file is needed. The original license is retained in `OFL.txt`.

Font source: https://github.com/google/fonts/tree/main/ofl/notosans

Regenerate with Pillow/qrcode installed:

```
python3 scripts/generate_diagnostic_assets.py /path/to/NotoSans.ttf main/diagnostic_assets.h
```

The fixed QR encodes exactly `https://emviary.majjix.com/`, medium error
correction, 29 content modules plus four quiet modules on every side. C draws
its 37x37 matrix at integer scale six in both supported orientations.

Host previews use the actual C layout and pixel callback, not a separate UI:

```
cc -Imain host_tests/test_diagnostic_layout.c main/diagnostic_layout.c -o /tmp/diagnostic-layout
/tmp/diagnostic-layout /tmp/diagnostic
python3 scripts/test_diagnostic_preview.py /tmp/diagnostic
```

The optional decoder requires Pillow and opencv-python-headless. Regular CTest
needs neither the font file nor Python imaging packages. Unsupported Unicode
codepoints become one `?`; UTF-8 Latin-1 names remain readable. All model fields
are bounded and truncated by pixel width with ellipses.
