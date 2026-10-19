"""Forward each validated device control once to the selected Windows media session."""
import json
from collections import deque

COMMANDS={'previous','next','play','pause'}
class ControlBridge:
    def __init__(self,process):
        self.process=process;self.seen=set();self.order=deque()
    def reset(self):self.seen.clear();self.order.clear()
    def dispatch(self,event):
        identifier=event.get('id');command=event.get('command')
        if event.get('kind')!='music_control' or type(identifier) is not int or not 0<identifier<=0xffffffff or command not in COMMANDS:
            return None
        if identifier in self.seen:return None
        self.seen.add(identifier);self.order.append(identifier)
        if len(self.order)>128:self.seen.remove(self.order.popleft())
        proc=self.process()
        try:
            if proc is None or proc.poll() is not None or proc.stdin is None:raise OSError('collector unavailable')
            line=json.dumps({'id':identifier,'command':command},separators=(',',':'))+'\n'
            # stdout uses utf-8-sig for .NET's preamble; do not emit that BOM on stdin.
            binary=getattr(proc.stdin,'buffer',None)
            if binary is not None:binary.write(line.encode('utf-8'));binary.flush()
            else:proc.stdin.write(line);proc.stdin.flush()
            return None
        except (OSError,ValueError):return {'kind':'control_result','id':identifier,'command':command,'success':False}

def playback_payload(latest,now_ms):
    payload={key:latest[key] for key in ('track_id','position_ms','duration_ms','playing')}
    at=latest.get('observed_at_ms')
    if payload['playing'] and type(at) is int:
        payload['position_ms']+=max(0,min(30000,now_ms-at))
    if payload['duration_ms']:payload['position_ms']=min(payload['position_ms'],payload['duration_ms'])
    return payload
