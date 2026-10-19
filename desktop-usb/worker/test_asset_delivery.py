import unittest
from unittest.mock import Mock,patch
from deskhub_worker import AssetDelivery


class AssetDeliveryTests(unittest.TestCase):
    def test_rejection_retries_after_backoff_and_stops_after_success(self):
        delivery=AssetDelivery();delivery.select('a')
        send=Mock(side_effect=[False,True])
        with patch('deskhub_worker.time.monotonic',return_value=10):
            delivery.forward('lyrics',{'track_id':'a'},send,10)
        self.assertNotIn('lyrics',delivery.sent)
        delivery.forward('lyrics',{'track_id':'a'},send,11)
        self.assertEqual(send.call_count,1)
        delivery.forward('lyrics',{'track_id':'a'},send,12)
        delivery.forward('lyrics',{'track_id':'a'},send,20)
        self.assertEqual(send.call_count,2)
        self.assertIn('lyrics',delivery.sent)

    def test_permanent_rejection_has_a_finite_attempt_limit(self):
        delivery=AssetDelivery();delivery.select('a');send=Mock(return_value=False)
        for now in (10,12,16,24,40):
            with patch('deskhub_worker.time.monotonic',return_value=now):
                delivery.forward('cover',b'image',send,now)
        self.assertEqual(send.call_count,3)
        self.assertNotIn('cover',delivery.sent)

    def test_track_change_discards_old_retry_deadline_and_success(self):
        delivery=AssetDelivery();delivery.select('a');send=Mock(side_effect=[False,True])
        with patch('deskhub_worker.time.monotonic',return_value=10):
            delivery.forward('cover',b'old',send,10)
        delivery.select('b')
        delivery.forward('cover',b'new',send,10)
        self.assertEqual(send.call_args.args,('cover',b'new'))
        self.assertIn('cover',delivery.sent)

    def test_assets_retry_independently_and_new_content_can_be_sent(self):
        delivery=AssetDelivery();delivery.select('a');send=Mock(return_value=True)
        delivery.forward('lyrics',{},send,10)
        delivery.forward('cover',b'image',send,10)
        delivery.invalidate('old','lyrics')
        self.assertIn('lyrics',delivery.sent)
        delivery.invalidate('a','lyrics')
        self.assertIn('cover',delivery.sent)
        delivery.forward('lyrics',{'lrc':'new'},send,11)
        self.assertEqual(send.call_count,3)

    def test_transport_exception_does_not_mark_asset_delivered(self):
        delivery=AssetDelivery();delivery.select('a')
        with self.assertRaises(TimeoutError):
            delivery.forward('cover',b'image',Mock(side_effect=TimeoutError),10)
        self.assertNotIn('cover',delivery.sent)
        delivery.reset();delivery.select('a');send=Mock(return_value=True)
        delivery.forward('cover',b'image',send,11)
        send.assert_called_once()


if __name__=='__main__':unittest.main()
