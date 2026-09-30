import unittest

from measure_frames import summarize


class PresentationMetricsTest(unittest.TestCase):
    def test_regular_slow_panel_does_not_pass_absolute_budget(self):
        metrics = summarize([1_000_000_000 + i * 8_387_000 for i in range(100)], 8_333_333)
        self.assertEqual(0, metrics["intervals_within_8_333_ms_percent"])
        self.assertEqual(0, metrics["intervals_over_1_5_nominal_refresh"])
        self.assertEqual(0, metrics["estimated_missed_measured_refresh_slots"])
        self.assertGreater(metrics["median_offset_from_nominal_percent"], .6)

    def test_dropped_frame_is_reported(self):
        metrics = summarize([i * 8_333_333 for i in (1, 2, 3, 5, 6, 7)], 8_333_333)
        self.assertEqual(1, metrics["intervals_over_1_5_nominal_refresh"])
        self.assertEqual(1, metrics["estimated_missed_measured_refresh_slots"])
        self.assertEqual(80, metrics["intervals_within_requested_budget_percent"])

    def test_60hz_is_separate_from_120hz_acceptance(self):
        metrics = summarize([i * 16_666_666 for i in range(100)], 16_666_666, 60)
        self.assertEqual(100, metrics["intervals_within_requested_budget_percent"])
        self.assertEqual(0, metrics["intervals_within_8_333_ms_percent"])

    def test_missing_samples_are_not_a_pass(self):
        with self.assertRaises(RuntimeError):
            summarize([1], 8_333_333)
        with self.assertRaises(RuntimeError):
            summarize([1, 2], 0)


if __name__ == "__main__":
    unittest.main()
