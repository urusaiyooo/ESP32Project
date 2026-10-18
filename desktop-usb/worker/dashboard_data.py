"""Read-only dashboard inputs. Never forward credentials or answer text."""
import json,math,os,time,urllib.request,urllib.parse
from pathlib import Path
from datetime import datetime,timedelta

def get_json(url,key=''):
    headers={'User-Agent':'DeskHub/1.4'}
    if key:headers['Authorization']='Bearer '+key
    with urllib.request.urlopen(urllib.request.Request(url,headers=headers),timeout=12) as r:
        return json.load(r)

def workbuddy(config_path=None):
    configured=os.environ.get('DESKHUB_WORKBUDDY_CONFIG','')
    candidates=[Path(config_path or configured)] if config_path or configured else sorted((Path.home()/'Desktop').glob('wb2api-panel*/config.json'))
    if not candidates:raise FileNotFoundError('WorkBuddy configuration not found')
    config=json.loads(candidates[-1].read_text(encoding='utf-8-sig'))
    # Always loopback; never send a local credential to a configurable remote host.
    d=get_json('http://127.0.0.1:7863/panel/api/overview',str(config.get('api_key','')))
    accounts=d['accounts']
    remaining=sum(float(x['credits']) for x in accounts)
    total=sum(float(x['credits_total']) for x in accounts)
    if not all(math.isfinite(x) and x>=0 for x in (remaining,total)):raise ValueError('Invalid pool totals')
    return {'available':True,'at':int(time.time()),'remaining':remaining,'total':total,'accounts':len(accounts)}

def weather():
    query=urllib.parse.urlencode({'latitude':39.74025,'longitude':116.32693,'current':'temperature_2m,relative_humidity_2m,weather_code','daily':'weather_code','forecast_days':2,'timezone':'Asia/Shanghai','timeformat':'unixtime'})
    d=get_json('https://api.open-meteo.com/v1/forecast?'+query)
    c=d['current'];now=int(time.time())
    result={'available':True,'at':now,'observed_at':c['time'],'city':'北京大兴','temperature':c['temperature_2m'],'humidity':c['relative_humidity_2m'],'today':c['weather_code'],'tomorrow':d['daily']['weather_code'][1],'tomorrow_date':d['daily']['time'][1],'aqi':None,'aqi_at':0,'source':'Open-Meteo','aqi_standard':'US'}
    try:
        a=get_json('https://air-quality-api.open-meteo.com/v1/air-quality?'+urllib.parse.urlencode({'latitude':39.74025,'longitude':116.32693,'current':'us_aqi','timeformat':'unixtime','timezone':'Asia/Shanghai'}))['current']
        result.update(aqi=a['us_aqi'],aqi_at=a['time'])
    except (OSError,ValueError,KeyError):pass
    return result

class Activity:
    """Only lifecycle events, file offsets and per-session state; no task database."""
    def __init__(self,root=None):
        self.root=Path(root or os.environ.get('CODEX_HOME',Path.home()/'.codex'))/'sessions'
        self.offsets={};self.sessions={};self.last_at=0
    def accept(self,record,session):
        if record.get('type')!='event_msg':return
        p=record.get('payload') or {};kind=p.get('type')
        running=kind in ('task_started','turn_started') or (kind=='item_completed' and (p.get('item') or {}).get('type')=='UserMessage')
        finished=kind in ('task_complete','task_aborted','turn_aborted')
        if not running and not finished:return
        try:at=datetime.fromisoformat(record['timestamp'].replace('Z','+00:00')).timestamp()
        except (ValueError,KeyError):return
        previous=self.sessions.get(session)
        if previous and at<previous[1]:return
        self.sessions[session]=('running' if running else 'complete',at)
        self.last_at=max(self.last_at,at)
    def scan(self):
        files=[];now=datetime.now()
        for day in (now,now-timedelta(days=1)):
            files.extend((self.root/day.strftime('%Y/%m/%d')).glob('rollout-*.jsonl'))
        files=sorted(files,key=lambda p:p.stat().st_mtime)[-32:]
        for path in files:
            try:
                with path.open('rb') as f:
                    end=f.seek(0,2);offset=self.offsets.get(path)
                    if offset is None or offset>end:
                        offset=max(0,end-2_000_000);f.seek(offset)
                        if offset:f.readline()
                    else:f.seek(offset)
                    while True:
                        pos=f.tell();line=f.readline()
                        if not line or not line.endswith(b'\n'):f.seek(pos);break
                        try:self.accept(json.loads(line),str(path))
                        except ValueError:pass
                    self.offsets[path]=f.tell()
            except OSError:continue
        now=time.time()
        active=[v for v in self.sessions.values() if now-v[1]<86400]
        state='running' if any(v[0]=='running' for v in active) else 'complete' if self.last_at and now-self.last_at<86400 else 'idle'
        return {'state':state,'event_at':int(self.last_at),'active_tasks':sum(v[0]=='running' for v in active)}
