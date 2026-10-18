"""ESP32-S3-only flasher. Validates all images before opening a serial port."""
from __future__ import annotations
import argparse,hashlib,json,os,struct,subprocess,sys,io,re,contextlib,time
from pathlib import Path
from serial.tools import list_ports

KNOWN_USB={(0x303a,0x1001),(0x1a86,0x7523),(0x1a86,0x7522),(0x10c4,0xea60)}
EXPECTED={'bootloader.bin':0,'partitions.bin':0x8000,'boot_app0.bin':0xe000,'firmware.bin':0x10000}

def default_folder():
    base=Path(sys.executable).resolve().parent if getattr(sys,'frozen',False) else Path(__file__).resolve().parents[1]
    for candidate in (base/'firmware',base/'prebuilt/firmware-s3',base/'release/firmware-s3'):
        if (candidate/'manifest.json').is_file():return candidate
    raise ValueError('Firmware manifest missing. Keep the firmware folder next to DeskHubFlash.exe.')

def validate(folder):
    folder=Path(folder).resolve();manifest=json.loads((folder/'manifest.json').read_text(encoding='utf-8-sig'))
    if manifest.get('chip')!='esp32s3' or manifest.get('flash_size')!='16MB':raise ValueError('Only ESP32-S3 16MB images are supported')
    images=manifest.get('images',[])
    if len(images)!=4 or {x['name']:x['address'] for x in images}!=EXPECTED:raise ValueError('Invalid flash layout')
    verified=[]
    for entry in images:
        path=folder/entry['name'];data=path.read_bytes()
        if len(data)!=entry['bytes'] or hashlib.sha256(data).hexdigest()!=entry['sha256']:raise ValueError('Checksum failed: '+path.name)
        if path.name in ('bootloader.bin','firmware.bin') and (len(data)<24 or data[0]!=0xe9 or struct.unpack_from('<H',data,12)[0]!=9):raise ValueError('Image is not ESP32-S3: '+path.name)
        verified.append((entry['address'],path))
    return manifest,sorted(verified)

def choose_port(explicit=None):
    ports=list(list_ports.comports())
    if explicit:
        if not any(p.device.upper()==explicit.upper() for p in ports):raise ValueError('Serial port not found: '+explicit)
        return explicit
    candidates=[p for p in ports if (p.vid,p.pid) in KNOWN_USB]
    if not candidates:raise ValueError('No S3/CH340 serial port. Check USB data cable and driver; see docs/FLASHING.md.')
    if len(candidates)==1:return candidates[0].device
    print('Choose the S3 serial port:')
    for i,p in enumerate(candidates,1):print(f'  {i}: {p.device} - {p.description}')
    answer=input('Port number: ').strip()
    if not answer.isdigit() or not 1<=int(answer)<=len(candidates):raise ValueError('Invalid port selection')
    return candidates[int(answer)-1].device

def powershell(script):
    return subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-Command',script],capture_output=True,text=True,encoding='utf-8',creationflags=0x08000000)

def stop_worker():
    if os.name!='nt':return False
    root=Path(os.environ.get('LOCALAPPDATA',''))/'DeskHub'
    expected=str(root/'python/pythonw.exe')
    # Query executable AND command line; never kill unrelated Python processes.
    script=r'''[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$taskExe=Join-Path $env:LOCALAPPDATA 'DeskHub\python\pythonw.exe'
@(Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -eq $taskExe -and $_.CommandLine -like '*deskhub_worker.py*' } | Select-Object -ExpandProperty ProcessId) | ConvertTo-Json -Compress'''
    result=powershell(script)
    if result.returncode:raise RuntimeError('Could not check DeskHub background. Close DeskHub before flashing.')
    pids=json.loads(result.stdout.strip() or '[]')
    if isinstance(pids,int):pids=[pids]
    for pid in pids:
        subprocess.run(['taskkill.exe','/PID',str(int(pid)),'/T','/F'],check=True,stdout=subprocess.DEVNULL,creationflags=0x08000000)
    return bool(pids)

def resume_worker():
    root=Path(os.environ.get('LOCALAPPDATA',''))/'DeskHub';exe=root/'python/pythonw.exe';script=root/'worker/deskhub_worker.py'
    if exe.exists() and script.exists():subprocess.Popen([str(exe),str(script)],cwd=str(root),creationflags=0x08000000)

def calibrate(port):
    import serial
    item=next(p for p in list_ports.comports() if p.device.upper()==port.upper())
    device=serial.Serial(port=None,baudrate=460800,timeout=.05,write_timeout=3)
    device.dtr=item.vid==0x303a;device.rts=False;device.port=port;device.open()
    try:
        device.write(b'HUB_STATUS\n');device.flush();pending=b'';deadline=time.monotonic()+5;identified=False
        while time.monotonic()<deadline:
            pending+=device.read(device.in_waiting or 1)
            if b'version=1.4.' in pending:identified=True;break
        if not identified:raise ValueError('DeskHub 1.4 firmware did not acknowledge. Flash firmware first.')
        device.write(b'HUB_TOUCH_CALIBRATE\n');device.flush()
        print('Touch calibration: press each displayed corner cross steadily, then RELEASE. Four corners total.')
        print('Waiting up to 3 minutes. The clock returns automatically when calibration is saved.')
        deadline=time.monotonic()+180;pending=b''
        while time.monotonic()<deadline:
            pending+=device.read(device.in_waiting or 1)
            if b'TOUCH_CALIBRATED' in pending:print('Calibration saved.');return
            if b'TOUCH_ERROR' in pending:raise ValueError('Device could not save calibration')
            pending=pending[-1024:]
        raise TimeoutError('Calibration timed out; press the board RST button to leave calibration.')
    finally:device.close()

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',help='Explicit COM port, e.g. COM5')
    parser.add_argument('--firmware-dir',type=Path)
    parser.add_argument('--check',action='store_true',help='Validate bundle only; never opens or writes a device')
    parser.add_argument('--list-ports',action='store_true')
    parser.add_argument('--probe',action='store_true',help='Read chip/flash identity only; resets the board but writes nothing')
    parser.add_argument('--calibrate',action='store_true',help='Open and save four-corner touch calibration; does not reflash')
    parser.add_argument('--baud',type=int,choices=(115200,460800,921600),default=460800)
    args=parser.parse_args(argv)
    if args.list_ports:
        for p in list_ports.comports():print(f'{p.device}: {p.description}')
        return 0
    manifest,images=validate(args.firmware_dir or default_folder())
    print('Validated firmware:',manifest['version'],'(ESP32-S3, 16MB, four SHA256 checks passed)')
    if args.check:return 0
    port=choose_port(args.port);stopped=False
    try:
        stopped=stop_worker()
        if args.calibrate:calibrate(port);return 0
        import esptool
        command=['--chip','esp32s3','--port',port,'--baud',str(args.baud)]
        if args.probe:command+=['flash-id']
        else:
            # Also verify physical capacity before writing a 16MB partition table.
            identity=io.StringIO()
            with contextlib.redirect_stdout(identity):esptool.main(command+['flash-id'])
            report=identity.getvalue();print(report)
            if not re.search(r'Detected flash size:\s*16\s*MB',report,re.I):
                raise ValueError('The board does not report 16MB flash; no firmware was written.')
            command+=['write-flash','--flash-mode','dio','--flash-freq','80m','--flash-size','16MB']
            for address,path in images:command += [hex(address),str(path)]
        print('Port:',port,'Read-only probe' if args.probe else 'Flashing application; NVS is preserved (no erase-all).')
        esptool.main(command)
        print('Completed. On first use, calibrate touch as described in docs/FLASHING.md.')
        return 0
    finally:
        if stopped:resume_worker()

if __name__=='__main__':
    if hasattr(sys.stdout,'reconfigure'):sys.stdout.reconfigure(encoding='utf-8')
    if hasattr(sys.stderr,'reconfigure'):sys.stderr.reconfigure(encoding='utf-8')
    try:raise SystemExit(main())
    except (OSError,ValueError,RuntimeError) as error:print('ERROR:',error,file=sys.stderr);raise SystemExit(1)
