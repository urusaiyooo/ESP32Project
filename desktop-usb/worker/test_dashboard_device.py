"""Explicit hardware test; uses real weather/account inputs and leaves clock open."""
import sys,time,json
from pathlib import Path
from usb_link import UsbLink
from dashboard_data import weather,workbuddy,Activity
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from clock_sync import AccountClient

def main():
    link=UsbLink.connect();result={}
    try:
        link.write('HUB_APP clock');time.sleep(.3)
        client=AccountClient()
        try:limits=client.limits()
        finally:client.close()
        link.clock(dict(epoch=int(time.time()),quota_at=int(time.time()),available=True,**limits))
        frame={'weather':weather(),'workbuddy':workbuddy(),'activity':{'state':'running'},'music':{'title':'USB 滚动歌曲名测试 キミの記憶','playing':True}}
        assert link.packet('dashboard',frame)
        def status():return json.loads(link.command('HUB_CLOCK_STATUS','CLOCK_STATUS ')[13:])
        running=status();assert running['activity']=='running' and running['weather_fresh'] and running['workbuddy_fresh'] and running['freshness']==2,running
        assert running['wb_remaining']==frame['workbuddy']['remaining']
        result['running']=running
        frame['activity']['state']='complete';assert link.packet('dashboard',frame);assert status()['activity']=='complete'
        time.sleep(10.3);assert status()['activity']=='offline';result['offline_after_timeout']=True
        try:link.packet('codex',{'v':1,'type':'history_request'});raise AssertionError('Legacy accepted')
        except RuntimeError:result['legacy_protocol_rejected']=True
        link.write('HUB_APP codex');time.sleep(.3);assert link.status()['app']=='clock';result['terminal_removed']=True
        frame['activity']=Activity().scan();assert link.packet('dashboard',frame)
        result['final']=status()
        Path('dashboard-device-tests.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
        print(json.dumps(result,ensure_ascii=True))
    finally:link.close()

if __name__=='__main__':main()
