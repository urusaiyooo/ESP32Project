from pathlib import Path
from PIL import Image
import io
root=Path(__file__).resolve().parents[1]
canvas=Image.new('RGB',(474,106),'white')
for name,x in [('character-left.png',0),('character-right.png',286)]:
    image=Image.open(root/'assets'/name).convert('RGB')
    image.thumbnail((188,106),Image.Resampling.LANCZOS)
    canvas.paste(image,(x,106-image.height))
buffer=io.BytesIO();canvas.save(buffer,format='JPEG',quality=92)
header='#pragma once\n#include <Arduino.h>\nstatic const uint8_t clockArt[] PROGMEM={\n'+','.join(map(str,buffer.getvalue()))+'\n};\n'
(root/'firmware/src/clock_art.h').write_text(header,encoding='ascii')
print('Regenerated clock_art.h')
