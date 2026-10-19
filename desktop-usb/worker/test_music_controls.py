import io,unittest
from types import SimpleNamespace
from music_controls import ControlBridge,playback_payload

class Tests(unittest.TestCase):
    def process(self):return SimpleNamespace(stdin=io.StringIO(),poll=lambda:None)
    def test_valid_commands_forward_once(self):
        proc=self.process();bridge=ControlBridge(lambda:proc)
        event={'kind':'music_control','command':'next','id':1}
        bridge.dispatch(event);bridge.dispatch(event)
        self.assertEqual(len(proc.stdin.getvalue().splitlines()),1)
    def test_unknown_or_injected_commands_ignored(self):
        proc=self.process();bridge=ControlBridge(lambda:proc)
        for cmd in ['shutdown','next\nanything',None]:bridge.dispatch({'kind':'music_control','command':cmd,'id':1})
        self.assertEqual(proc.stdin.getvalue(),'')
    def test_no_collector_returns_failure(self):
        bridge=ControlBridge(lambda:None)
        self.assertFalse(bridge.dispatch({'kind':'music_control','command':'pause','id':2})['success'])
    def test_control_pipe_has_no_utf8_bom(self):
        raw=io.BytesIO();stream=io.TextIOWrapper(raw,encoding='utf-8-sig');proc=SimpleNamespace(stdin=stream,poll=lambda:None)
        ControlBridge(lambda:proc).dispatch({'kind':'music_control','command':'play','id':3})
        self.assertTrue(raw.getvalue().startswith(b'{'));self.assertNotIn(b'\xef\xbb\xbf',raw.getvalue())
    def test_reboot_clears_deduplication(self):
        proc=self.process();bridge=ControlBridge(lambda:proc);event={'kind':'music_control','command':'play','id':1}
        bridge.dispatch(event);bridge.reset();bridge.dispatch(event)
        self.assertEqual(len(proc.stdin.getvalue().splitlines()),2)
    def test_transport_delay_advances_only_playing_position(self):
        item={'track_id':'a','position_ms':1000,'duration_ms':3000,'playing':True,'observed_at_ms':10000}
        self.assertEqual(playback_payload(item,11500)['position_ms'],2500)
        self.assertEqual(playback_payload(item,20000)['position_ms'],3000)
        item['playing']=False;self.assertEqual(playback_payload(item,11500)['position_ms'],1000)

if __name__=='__main__':unittest.main()
