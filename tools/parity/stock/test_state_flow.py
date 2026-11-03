"""Private input acknowledgement controls, not actual Stock observations."""
import importlib.util
from pathlib import Path
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('state_flow', Path(__file__).with_name('state_flow.py'))
state = importlib.util.module_from_spec(spec)
spec.loader.exec_module(state)


class ObservedButtonTest(unittest.TestCase):
    def fixture(self, delay=.6, no_down=False, no_up=False, truncate=False, budget=10):
        self.now = 0.
        self.calls = []
        self.down_at = self.up_at = None
        self.down_seen = self.up_seen = False
        self.raw = b'[0 ms] input_poll: kind=1 offset=12 data=0 source=native state_call=1\n'
        def guest(args):
            self.calls.append(args)
            if args[1] == 'mousedown':
                self.down_at = self.now
            else:
                self.up_at = self.now
        def read():
            if truncate and self.down_at is not None:
                return b'changed prefix\n'
            if self.down_at is not None and self.up_at is None and not no_down and not self.down_seen and self.now >= self.down_at + delay:
                self.raw += b'[1 ms] input_poll: kind=1 offset=12 data=128 source=native state_call=2\n'
                self.down_seen = True
            if self.up_at is not None and self.down_seen and not no_up and not self.up_seen and self.now >= self.up_at + delay:
                self.raw += b'[2 ms] input_poll: kind=1 offset=12 data=0 source=native state_call=3\n'
                self.up_seen = True
            return self.raw
        def sleep(seconds):
            self.now += seconds
        return guest, lambda: max(0, budget-self.now), read, sleep

    def run_fixture(self, **options):
        guest, remaining, read, sleep = self.fixture(**options)
        with mock.patch.object(state.time, 'monotonic', side_effect=lambda: self.now), \
             mock.patch.object(state.time, 'sleep', side_effect=sleep):
            return state.press_observed_button(guest, remaining, read)

    def test_slow_poll_holds_one_press_until_native_down_then_observes_release(self):
        result = self.run_fixture()
        self.assertGreaterEqual(self.up_at-self.down_at, .6)
        self.assertEqual(self.calls, [['xdotool', 'mousedown', '1'], ['xdotool', 'mouseup', '1']])
        self.assertEqual(result['native_baseline_transitions'], 1)
        self.assertEqual(result['native_ack_transitions'], [128, 0])

    def test_immutable_fixed_250ms_protocol_misses_the_same_slow_poll(self):
        guest, _, read, sleep = self.fixture(delay=.6)
        guest(['xdotool', 'mousedown', '1'])
        sleep(.25)
        guest(['xdotool', 'mouseup', '1'])
        sleep(1)
        self.assertNotIn(b'data=128', read())
        self.assertFalse(self.down_seen)

    def test_missing_down_refuses_and_releases_without_retry(self):
        with self.assertRaises(TimeoutError):
            self.run_fixture(no_down=True)
        self.assertEqual(self.calls, [['xdotool', 'mousedown', '1'], ['xdotool', 'mouseup', '1']])
        self.assertLessEqual(self.now, 2.05)

    def test_failure_preserves_partial_native_identity_and_release_timing(self):
        guest, remaining, read, sleep = self.fixture(no_down=True)
        record = {}
        with mock.patch.object(state.time, 'monotonic', side_effect=lambda: self.now), \
             mock.patch.object(state.time, 'sleep', side_effect=sleep):
            with self.assertRaises(TimeoutError):
                state.press_observed_button(guest, remaining, read, record)
        self.assertEqual(record['native_baseline_transitions'], 1)
        self.assertIn('button_down_request_wall_time_ns', record)
        self.assertIn('button_up_request_wall_time_ns', record)
        self.assertIn('released_wall_time_ns', record)
        self.assertNotIn('native_ack_transitions', record)

    def test_missing_up_refuses_after_one_release(self):
        with self.assertRaises(TimeoutError):
            self.run_fixture(no_up=True)
        self.assertEqual(len(self.calls), 2)

    def test_replaced_log_releases_and_refuses(self):
        with self.assertRaises(ValueError):
            self.run_fixture(truncate=True)
        self.assertEqual(self.calls[-1], ['xdotool', 'mouseup', '1'])

    def test_mousedown_failure_still_attempts_one_release(self):
        guest, remaining, read, sleep = self.fixture()
        def failing(args):
            guest(args)
            if args[1] == 'mousedown':
                raise RuntimeError('controlled command failure')
        with self.assertRaisesRegex(RuntimeError, 'controlled command failure'):
            state.press_observed_button(failing, remaining, read)
        self.assertEqual(self.calls, [['xdotool', 'mousedown', '1'], ['xdotool', 'mouseup', '1']])

    def test_insufficient_global_budget_refuses_before_press(self):
        with self.assertRaises(TimeoutError):
            self.run_fixture(budget=.2)
        self.assertEqual(self.calls, [])

    def test_old_or_injected_down_is_not_a_new_native_acknowledgement(self):
        guest, remaining, read, sleep = self.fixture(no_down=True)
        self.raw += b'[1 ms] input_poll: kind=1 offset=12 data=128 source=injected state_call=2\n'
        with mock.patch.object(state.time, 'monotonic', side_effect=lambda: self.now), \
             mock.patch.object(state.time, 'sleep', side_effect=sleep):
            with self.assertRaises(TimeoutError):
                state.press_observed_button(guest, remaining, read)
        self.assertEqual(len(self.calls), 2)


if __name__ == '__main__':
    unittest.main()
