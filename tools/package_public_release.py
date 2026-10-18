"""Package allowlisted public source and a separately staged Windows kit."""
import argparse,hashlib,json,zipfile,re
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SKIP_DIRS={'.git','.pio','.venv','bin','obj','__pycache__','payload','runtime','runtime-clock','backups','release','dist','build'}
SKIP_SUFFIX={'.pyc','.log','.pdb','.exe','.zip','.db','.sqlite','.spec'}
PRIVATE_NAMES={'auth.json','config.json','.env','worker.pid'}

def source_files():
    for p in ROOT.rglob('*'):
        if not p.is_file():continue
        rel=p.relative_to(ROOT)
        if any(x in SKIP_DIRS for x in rel.parts) or p.suffix.lower() in SKIP_SUFFIX or p.name in PRIVATE_NAMES:continue
        yield p

def checksums(root,files):
    lines=[]
    for p in sorted(files):
        if p.name=='SHA256SUMS.txt' and p.parent==root:continue
        lines.append(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(root).as_posix()+'\n')
    (root/'SHA256SUMS.txt').write_text(''.join(lines),encoding='utf-8')

def pack(destination,root,files,prefix):
    with zipfile.ZipFile(destination,'w',zipfile.ZIP_DEFLATED,6) as archive:
        for p in sorted(files):archive.write(p,prefix+'/'+p.relative_to(root).as_posix())

def main():
    parser=argparse.ArgumentParser()
    group=parser.add_mutually_exclusive_group()
    group.add_argument('--windows-dir',type=Path)
    group.add_argument('--firmware-dir',type=Path)
    parser.add_argument('--output',type=Path,default=ROOT.parent/'artifacts')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    version=re.search(r'kVersion\[\] = "(\d+\.\d+\.\d+)',(ROOT/'firmware/include/config.h').read_text(encoding='utf-8'))[1]
    kit=args.windows_dir or args.firmware_dir
    if kit:
        kit=kit.resolve()
        required=['DeskHubFlash.exe','firmware/manifest.json']
        if args.windows_dir:required+=['DeskHubSetup.exe','drivers/CH340']
        for name in required:
            if not (kit/name).exists():raise SystemExit('Kit missing '+name)
        if not json.loads((kit/'firmware/manifest.json').read_text(encoding='utf-8'))['version'].startswith(version+'-'):
            raise SystemExit('Kit/source firmware version mismatch')
    files=list(source_files());checksums(ROOT,files);files=list(source_files())
    source_zip=out/f'DeskHub-Source-{version}.zip'
    pack(source_zip,ROOT,files,'GitHub-Source')
    artifacts=[source_zip]
    if kit:
        prefix=f'DeskHub-{"Windows" if args.windows_dir else "Firmware"}-{version}'
        checksums(kit,[p for p in kit.rglob('*') if p.is_file()])
        kit_zip=out/(prefix+'.zip')
        pack(kit_zip,kit,[p for p in kit.rglob('*') if p.is_file()],prefix)
        artifacts.append(kit_zip)
    checksums(out,artifacts)
    print(json.dumps({'source_files':len(files),'artifacts':[{'name':p.name,'bytes':p.stat().st_size} for p in artifacts]}))

if __name__=='__main__':main()
