import unittest
from clock_sync import normalize_limits


class LimitsTests(unittest.TestCase):
    def test_window_duration_and_remaining(self):
        result = normalize_limits({"rateLimitsByLimitId": {"codex": {
            "secondary": {"usedPercent": 25.5, "windowDurationMins": 300, "resetsAt": 1792311125},
            "primary": {"usedPercent": 80, "windowDurationMins": 10080, "resetsAt": 1792876727}}}})
        self.assertEqual(result["five"], {"remaining": 74.5, "reset": 1792311125})
        self.assertEqual(result["week"]["remaining"], 20)

    def test_missing_window_is_unknown(self):
        result = normalize_limits({"rateLimits": {"primary": {
            "usedPercent": 0, "windowDurationMins": 300, "resetsAt": None}}})
        self.assertIsNone(result["week"])
        self.assertEqual(result["five"]["remaining"], 100)
        self.assertIsNone(result["five"]["reset"])

    def test_never_select_other_bucket(self):
        with self.assertRaises(ValueError):
            normalize_limits({"rateLimits": {"limitId": "other", "primary": {
                "usedPercent": 5, "windowDurationMins": 300}}})

    def test_invalid_usage_does_not_become_zero(self):
        for invalid in (None, True, "50", float("nan")):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                normalize_limits({"rateLimits": {"primary": {
                    "usedPercent": invalid, "windowDurationMins": 300}}})


if __name__ == "__main__": unittest.main()
