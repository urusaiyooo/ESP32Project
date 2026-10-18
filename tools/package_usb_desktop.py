"""Bundle only runtimes and source: never include user auth, history, cache or tokens."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import sys
import zipfile

root=Path(__file__).resolve().parents[1]
if sys.version_info[:2]!=(3,12):raise RuntimeError('Build the portable runtime with Python 3.12')
payload=root/'desktop-usb/payload'
runtime=payload/'python'
runtime.mkdir(parents=True,exist_ok=True)
python=Path(sys.executable).parent
for name in ('python.exe','pythonw.exe','python312.dll','python3.dll','vcruntime140.dll','vcruntime140_1.dll','LICENSE.txt'):
    if (python/name).exists():shutil.copy2(python/name,runtime/name)
(runtime/'DLLs').mkdir(exist_ok=True)
for path in (python/'DLLs').iterdir():
    if path.suffix.lower() in ('.dll','.pyd') and not path.name.startswith(('_test','_ctypes_test','_tkinter','tcl','tk')):shutil.copy2(path,runtime/'DLLs'/path.name)
skip={'site-packages','test','tests','idlelib','ensurepip','tkinter','turtledemo','__pycache__'}
with zipfile.ZipFile(runtime/'python312.zip','w',zipfile.ZIP_DEFLATED,6) as archive:
    for path in (python/'Lib').rglob('*.py'):
        relative=path.relative_to(python/'Lib')
        if not any(part in skip for part in relative.parts):archive.write(path,relative.as_posix())
import serial
target=runtime/'Lib/site-packages/serial'
shutil.copytree(Path(serial.__file__).parent,target,dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__'))
(runtime/'python312._pth').write_text('python312.zip\n.\nDLLs\nLib/site-packages\n../worker\n../tools\n',encoding='ascii')
for folder in ('worker','tools','codex'):(payload/folder).mkdir(exist_ok=True)
for path in (root/'desktop-usb/worker').glob('*.py'):
    if not path.name.startswith('test_'):shutil.copy2(path,payload/'worker'/path.name)
shutil.copy2(root/'tools/clock_sync.py',payload/'tools/clock_sync.py')

shutil.copytree(root/'third_party/licenses',payload/'licenses',dirs_exist_ok=True)
shutil.copy2(root/'LICENSE',payload/'licenses/DeskHub-LICENSE.txt')
codex=os.environ.get('DESKHUB_CODEX_EXE') or shutil.which('codex.exe') or shutil.which('codex')
if not codex:raise RuntimeError('Codex executable not available for account interface bundle')
if Path(codex).read_bytes()[:2]!=b'MZ':raise RuntimeError('DESKHUB_CODEX_EXE must point to the native Windows codex.exe, not an npm cmd shim')
shutil.copy2(codex,payload/'codex/codex.exe')
# This CLI is used for login and account reads only; no user .codex directory is packaged.
manifest=[]
for path in payload.rglob('*'):
    if path.is_file() and '__pycache__' not in path.parts and path.suffix!='.pdb' and path.name!='bundle-manifest.json' and 'bridge' not in path.relative_to(payload).parts and not path.name.startswith('test_'):
        manifest.append({'file':path.relative_to(payload).as_posix(),'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
(payload/'bundle-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
with zipfile.ZipFile(root/'desktop-usb/payload.zip','w',zipfile.ZIP_DEFLATED,6) as archive:
    for item in manifest:archive.write(payload/item['file'],item['file'])
    archive.write(payload/'bundle-manifest.json','bundle-manifest.json')
print('Self-contained desktop bundle:',len(manifest),'files; no credentials or user history included.')
