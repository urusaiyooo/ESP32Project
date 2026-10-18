import unittest
from dashboard_data import Activity,workbuddy
from unittest.mock import patch
from pathlib import Path
import tempfile,json

class Tests(unittest.TestCase):
    def event(self,kind,stamp='2026-10-18T10:00:00Z'):
        return {'type':'event_msg','timestamp':stamp,'payload':{'type':kind,'last_agent_message':'must not be retained'}}
    def test_parallel_tasks(self):
        a=Activity();a.accept(self.event('task_started'),'one');a.accept(self.event('task_started'),'two')
        a.accept(self.event('task_complete','2026-10-18T10:00:02Z'),'one')
        self.assertEqual(a.sessions['two'][0],'running')
        a.accept(self.event('task_complete','2026-10-18T10:00:03Z'),'two')
        self.assertTrue(all(x[0]=='complete' for x in a.sessions.values()))
        self.assertNotIn('must not be retained',str(a.__dict__))
    def test_older_completion_ignored(self):
        a=Activity();a.accept(self.event('task_started','2026-10-18T10:00:05Z'),'one');a.accept(self.event('task_complete'),'one')
        self.assertEqual(a.sessions['one'][0],'running')
    def test_pool_includes_paused_accounts_like_panel(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/'config.json';p.write_text(json.dumps({'api_key':'test-only'}))
            with patch('dashboard_data.get_json',return_value={'accounts':[{'credits':12,'credits_total':30},{'credits':5,'credits_total':20,'paused':True}]}):
                data=workbuddy(p)
            self.assertEqual((data['remaining'],data['total']),(17,50))
            self.assertNotIn('test-only',str(data))

if __name__=='__main__':unittest.main()
