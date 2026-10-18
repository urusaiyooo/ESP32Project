"""Pack baseline JPEG frames with a bounded index into the dedicated video partition."""
import argparse,struct,zlib
from pathlib import Path

CAPACITY=0x9a0000
HEADER=struct.Struct('<8s14I')

def pack(data,width=320,height=180,fps=12):
    frames=[];offset=0
    while offset<len(data):
        if data[offset:offset+2]!=b'\xff\xd8':raise ValueError('Invalid JPEG frame boundary')
        end=data.find(b'\xff\xd9',offset+2)
        if end<0:raise ValueError('Truncated JPEG frame')
        end+=2;frame=data[offset:end]
        # FFmpeg writes a baseline SOF0; this release supports only this fixed size.
        sof=frame.find(b'\xff\xc0')
        if sof<0 or len(frame)<sof+9 or struct.unpack('>HH',frame[sof+5:sof+9])!=(height,width):
            raise ValueError('Expected baseline JPEG at configured dimensions')
        frames.append((offset,end-offset));offset=end
    if not frames or width!=320 or height!=180 or fps!=12:raise ValueError('Expected 320x180 at 12 fps')
    index=b''.join(struct.pack('<II',*item) for item in frames)
    payload_offset=(64+len(index)+4095)//4096*4096
    header=HEADER.pack(b'DHVIDEO1',1,width,height,fps,1,len(frames),64,payload_offset,len(data),max(n for _,n in frames),zlib.crc32(data),zlib.crc32(index),0,0)
    image=header+index+b'\xff'*(payload_offset-64-len(index))+data
    if len(image)>CAPACITY:raise ValueError('Video exceeds dedicated 9.625 MiB partition')
    return image

def validate(image):
    if len(image)<64 or len(image)>CAPACITY:raise ValueError('Video image size')
    magic,version,w,h,fn,fd,count,io,po,size,maximum,crc,icrc,r0,r1=HEADER.unpack_from(image)
    if (magic,version,w,h,fn,fd,io,r0,r1)!=(b'DHVIDEO1',1,320,180,12,1,64,0,0):raise ValueError('Video header')
    if not count or count>10000 or 64+count*8>po or po+size!=len(image) or not 0<maximum<=65536:raise ValueError('Video bounds')
    index=image[io:io+count*8];payload=image[po:]
    if zlib.crc32(index)!=icrc or zlib.crc32(payload)!=crc:raise ValueError('Video CRC')
    previous=0;largest=0
    for offset,length in struct.iter_unpack('<II',index):
        if offset!=previous or not 4<=length<=maximum or offset+length>size:raise ValueError('Frame bounds')
        frame=payload[offset:offset+length]
        if not frame.startswith(b'\xff\xd8') or not frame.endswith(b'\xff\xd9'):raise ValueError('JPEG boundary')
        previous=offset+length;largest=max(largest,length)
    if previous!=size or largest!=maximum:raise ValueError('Video completeness')
    return {'frames':count,'fps':fn/fd,'duration_seconds':count*fd/fn,'width':w,'height':h,'bytes':len(image)}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args();image=pack(args.input.read_bytes());print(validate(image))
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_bytes(image)
