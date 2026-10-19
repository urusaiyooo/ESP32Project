"""Single USB owner: Windows data collection only; all display rendering is on S3."""
from __future__ import annotations
import argparse
import base64
import ctypes
import json
import logging
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time

ROOT=Path(__file__).resolve().parents[1]
if not (ROOT/"python").exists():
    sys.path.insert(0,str(ROOT.parents[0]/"tools"))
else:
    sys.path.insert(0,str(ROOT/"tools"))
from clock_sync import AccountClient
from dashboard_data import Activity,weather,workbuddy
from usb_link import UsbLink
from music_controls import ControlBridge,playback_payload

RUNTIME=Path(os.environ.get("LOCALAPPDATA",Path.home()))/"DeskHub/runtime"


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--once",action="store_true")
    parser.add_argument("--self-test",action="store_true")
    args=parser.parse_args()
    RUNTIME.mkdir(parents=True,exist_ok=True)
    logging.basicConfig(filename=RUNTIME/"worker.log",level=logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",encoding="utf-8")
    if args.self_test:
        import sqlite3
        print(json.dumps({"python":sys.version.split()[0],"serial":True,"sqlite":sqlite3.sqlite_version,"rendering":"S3"}));return 0
    mutex=None
    if os.name=="nt":
        ctypes.windll.kernel32.CreateMutexW.restype=ctypes.c_void_p
        ctypes.windll.kernel32.CloseHandle.argtypes=[ctypes.c_void_p]
        mutex=ctypes.windll.kernel32.CreateMutexW(None,False,"Local\\DeskHubUsbWorker")
        if ctypes.windll.kernel32.GetLastError()==183:return 0
    (RUNTIME/"worker.pid").write_text(str(os.getpid()),encoding="ascii")
    # A bundled CLI supplies only the account interface; credentials stay in this Windows user profile.
    if (ROOT/"codex/codex.exe").exists():
        os.environ["PATH"]=str(ROOT/"codex")+os.pathsep+os.environ.get("PATH","")
    media_queue=queue.Queue(maxsize=128)
    quota_queue=queue.Queue(maxsize=2)
    shutdown=threading.Event()
    media_process=None
    def media_reader():
        nonlocal media_process
        exe=ROOT/"media/DeskHubMedia.exe"
        while not shutdown.is_set():
            try:
                media_process=subprocess.Popen([str(exe)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,
                    text=True,encoding="utf-8-sig",creationflags=0x08000000 if os.name=="nt" else 0)
                for line in media_process.stdout:
                    if shutdown.is_set():break
                    try:item=json.loads(line)
                    except ValueError:continue
                    if media_queue.full():media_queue.get_nowait()
                    media_queue.put_nowait(item)
            except (OSError,queue.Empty,queue.Full):logging.warning("Music collector unavailable")
            if shutdown.wait(5):break
    def quota_reader():
        client=AccountClient();previous={"five":None,"week":None};updated=0
        try:
            while not shutdown.is_set():
                available=False
                try:previous=client.limits();updated=int(time.time());available=True
                except Exception as e:logging.warning("Codex account unavailable: %s",type(e).__name__)
                packet={"epoch":int(time.time()),"quota_at":updated,"available":available,**previous}
                if quota_queue.full():quota_queue.get_nowait()
                quota_queue.put(packet)
                if shutdown.wait(60):break
        finally:client.close()
    threading.Thread(target=media_reader,daemon=True).start()
    controls=ControlBridge(lambda:media_process);control_results=[]
    threading.Thread(target=quota_reader,daemon=True).start()
    dashboard_queue=queue.Queue(maxsize=2)
    def dashboard_reader():
        last_weather=0;w={'available':False,'at':0};b={'available':False,'at':0}
        while not shutdown.is_set():
            if time.monotonic()-last_weather>600 or not w.get('available'):
                last_weather=time.monotonic()
                try:w=weather()
                except Exception as e:logging.warning('Weather unavailable: %s',type(e).__name__);w={**w,'available':False}
            try:b=workbuddy()
            except Exception as e:logging.warning('WorkBuddy unavailable: %s',type(e).__name__);b={**b,'available':False}
            if dashboard_queue.full():dashboard_queue.get_nowait()
            dashboard_queue.put({'weather':w,'workbuddy':b})
            if shutdown.wait(30):break
    threading.Thread(target=dashboard_reader,daemon=True).start()
    activity=Activity();dashboard={'weather':{'available':False,'at':0},'workbuddy':{'available':False,'at':0}}
    link=None;latest=None;assets={};sent_track=None;sent_lyrics=None;sent_cover=None;idle=False
    last_dashboard=0;last_clock=0;last_playback=0;last_app=None;clock_payload=None;last_uptime=0
    try:
        while not shutdown.is_set():
            now=time.monotonic()
            while not media_queue.empty():
                item=media_queue.get_nowait();kind=item.get("kind")
                if kind=="playback":latest=item
                elif kind=="control_result":control_results.append(item)
                elif kind=="cache_status":logging.info('Music cache: cover_hit=%s lyrics_hit=%s',item.get('cover_hit'),item.get('lyrics_hit'))
                elif kind=="idle":latest=None
                elif kind in ("lyrics","cover"):
                    tid=item.get("track_id")
                    assets.setdefault(tid,{})[kind]=item
                    if len(assets)>4:assets.pop(next(iter(assets)))
            while not quota_queue.empty():clock_payload=quota_queue.get_nowait();last_clock=0
            while not dashboard_queue.empty():dashboard=dashboard_queue.get_nowait();last_dashboard=0
            try:
                if link is None:
                    link=UsbLink.connect();last_app=None;sent_track=None;last_clock=0;last_dashboard=0
                    logging.info("USB connected: %s",link.device.port)
                status=link.status();app=status["app"]
                if status["uptime"]<last_uptime:
                    controls.reset()
                    last_clock=last_dashboard=0;sent_track=None;sent_lyrics=sent_cover=None
                last_uptime=status["uptime"]
                if app!=last_app:
                    logging.info("Device app: %s",app);sent_track=None;sent_lyrics=sent_cover=None;idle=False;last_app=app
                if clock_payload and now-last_clock>30:
                    clock_payload["epoch"]=int(time.time());link.clock(clock_payload);last_clock=now
                    logging.info("Clock synchronized; account_available=%s",clock_payload["available"])
                if now-last_dashboard>1:
                    packet={**dashboard,'activity':activity.scan(),'music':{'title':latest['title'] if latest and latest['playing'] else '', 'playing':bool(latest and latest['playing'])}}
                    if not link.packet('dashboard',packet):raise RuntimeError('Dashboard rejected')
                    last_dashboard=now
                while link.events:
                    result=controls.dispatch(link.events.popleft())
                    if result is not None:control_results.append(result)
                if app=='music':
                    while control_results:
                        result=control_results.pop(0)
                        link.packet('control_result',result)
                        logging.info('Music control result: %s success=%s',result.get('command'),result.get('success'))
                else:control_results.clear()
                if app=="music":
                    if latest is None:
                        if not idle:link.packet("idle",{});idle=True;sent_track=None
                    else:
                        tid=latest["track_id"];idle=False
                        if tid!=sent_track:
                            if link.packet("track",{k:latest[k] for k in ("track_id","title","artist","album","duration_ms")}):
                                sent_track=tid;sent_lyrics=sent_cover=None
                                logging.info("Music text metadata forwarded")
                        if sent_track==tid and now-last_playback>1:
                            link.packet("playback",playback_payload(latest,int(time.time()*1000)));last_playback=now
                        track_assets=assets.get(tid,{})
                        if "lyrics" in track_assets and sent_lyrics!=tid:
                            value=track_assets["lyrics"]
                            if link.packet("lyrics",{k:value[k] for k in ("track_id","lrc","plain")}):logging.info("Raw lyrics forwarded; timeline/rendering on S3")
                            sent_lyrics=tid
                        if "cover" in track_assets and sent_cover!=tid:
                            raw=base64.b64decode(track_assets["cover"]["data"],validate=True)
                            okay=link.packet("cover",raw);sent_cover=tid
                            logging.info("Original cover forwarded; device_decode=%s bytes=%s",okay,len(raw))
                if args.once:return 0
                time.sleep(.3)
            except Exception as e:
                logging.warning("USB reconnect: %s",type(e).__name__)
                if link:link.close()
                link=None
                if args.once:return 1
                shutdown.wait(2)
    finally:
        shutdown.set()
        if link:link.close()
        if media_process and media_process.poll() is None:media_process.terminate()
        if mutex:ctypes.windll.kernel32.CloseHandle(mutex)
    return 0


if __name__=="__main__":raise SystemExit(main())
