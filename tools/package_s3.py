"""Package S3 images separately; keep the original ESP32 desktop bundle intact."""
from pathlib import Path
import os
import hashlib
import json
import re
import shutil
import struct

root=Path(__file__).resolve().parents[1]
build=Path(os.environ['TEMP'])/'DeskHub-S3-GitHub-build/esp32s3_n16r8'
destination=root/'prebuilt/firmware-s3'
destination.mkdir(parents=True,exist_ok=True)
images=[('bootloader.bin',0),('partitions.bin',0x8000),('boot_app0.bin',0xe000),('firmware.bin',0x10000),('video.bin',0x650000)]
manifest=[]
for name,address in images:
    source=build/name if name!='boot_app0.bin' else Path.home()/'.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin'
    if name=='video.bin':
        source=destination/name
        from pack_video import validate
        validate(source.read_bytes())
    original=source.read_bytes()
    if name in ('bootloader.bin','firmware.bin'):
        if len(original)<24 or original[0]!=0xE9 or struct.unpack_from('<H',original,12)[0]!=9:
            raise RuntimeError('Refusing a non-S3 image: '+str(source))
    if source.resolve()!=(destination/name).resolve():shutil.copy2(source,destination/name)
    data=(destination/name).read_bytes()
    manifest.append({'name':name,'address':address,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()})
config=(root/'firmware/include/display_config.h').read_text(encoding='utf-8')
pins={key:int(re.search(r'constexpr int '+key+r' = (-?\d+);',config)[1]) for key in
      ['kCs','kReset','kDc','kMosi','kSck','kMiso','kTouchSck','kTouchCs','kTouchMosi','kTouchMiso','kTouchIrq']}
version=re.search(r'kVersion\[\] = "([^"]+)"',(root/'firmware/include/config.h').read_text(encoding='utf-8'))[1]
spi_hz=int(re.search(r'constexpr uint32_t kSpiWriteHz = (\d+);',config)[1])
(destination/'manifest.json').write_text(json.dumps({'chip':'esp32s3','flash_size':'16MB','boot_flash_mode':'dio','runtime_memory_type':'qio_opi','psram':'8MB OPI','version':version,'lcd_spi_hz':spi_hz,'pins':pins,'images':manifest},indent=2),encoding='utf-8')
(destination/'SHA256SUMS.txt').write_text(''.join(f"{entry['sha256']}  {entry['name']}\n" for entry in manifest),encoding='ascii')
print('Packaged S3 N16R8 images in prebuilt/firmware-s3')
