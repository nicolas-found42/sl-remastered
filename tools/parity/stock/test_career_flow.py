"""Controller frontier controls; simulated observations are not Stock evidence."""
import importlib.util
from pathlib import Path
import unittest
import tempfile
import struct
import json
import hashlib
from unittest import mock

spec = importlib.util.spec_from_file_location('career_flow', Path(__file__).with_name('career_flow.py'))
flow = importlib.util.module_from_spec(spec)
spec.loader.exec_module(flow)


class PassiveVisionTest(unittest.TestCase):
    def test_cadence_cap_and_original_binding(self):
        with tempfile.TemporaryDirectory() as name:
            out = Path(name)
            clock = [0.0]
            captures = []
            def capture(stage, deadline):
                self.assertEqual(deadline, 1000)
                captures.append(clock[0])
                image = f'passive-{len(captures)}.png'
                (out / image).write_bytes(b'passive only')
                return {'image': image, 'wall_time_ns': 123}
            with mock.patch.object(flow.time, 'monotonic', side_effect=lambda: clock[0]):
                observer = flow.PassiveVisionFrames(out, capture, lambda: 1000)
                request = {'request_id': 'original', 'image': 'original.png', 'expires_monotonic': 1000}
                before = dict(request)
                review = {}
                observer.poll(request, review)
                self.assertEqual(captures, [])
                for tick in range(1, 101):
                    clock[0] = tick
                    observer.poll(request, review)
                self.assertEqual(captures, list(range(3, 73, 3)))
                self.assertEqual(request, before)
                self.assertEqual(len(review['passive_frames']), 24)
                status = json.loads((out / 'career-vision-passive.json').read_text())
                self.assertEqual(status['bound_image'], 'original.png')
                self.assertFalse(status['latest_passive']['authorizes_input'])
                self.assertEqual(status['latest_passive']['image_sha256'],
                                 hashlib.sha256(b'passive only').hexdigest())

    def test_deadline_reserve_and_failed_capture_disable(self):
        with tempfile.TemporaryDirectory() as name:
            clock = [3.0]
            capture = mock.Mock(side_effect=TimeoutError('bounded capture failed'))
            with mock.patch.object(flow.time, 'monotonic', side_effect=lambda: clock[0]):
                observer = flow.PassiveVisionFrames(Path(name), capture, lambda: 100)
                request = {'request_id': 'one', 'image': 'original.png', 'expires_monotonic': 10}
                review = {}
                clock[0] = 6
                observer.poll(request, review)
                capture.assert_not_called()  # Only four seconds remain.
                request['expires_monotonic'] = 100
                observer.remaining = lambda: 4
                observer.poll(request, review)
                capture.assert_not_called()  # Global deadline has the same reserve.
                observer.remaining = lambda: 100
                observer.poll(request, review)
                clock[0] = 30
                observer.poll(request, review)
                capture.assert_called_once()
                self.assertIn('TimeoutError', review['passive_frames'][0]['error'])
                self.assertFalse(review['passive_frames'][0]['authorizes_input'])


class CareerFrontierTest(unittest.TestCase):
    def test_intro_review_requires_exact_card_and_no_arbitrary_key_or_coordinate(self):
        request = {'phase': 'dealer-open', 'action': 'Open purchase', 'request_id': 'fixture',
                   'image_sha256': 'fresh-frame', 'created_monotonic': 1, 'expires_monotonic': 40}
        response = {key: request[key] for key in ('phase', 'action', 'request_id', 'image_sha256')}
        response['action'] = flow.INTRO_PHASE[1]
        response.update(client=None, observed_card=dict(flow.EXPECTED_INTRO), reason='')
        flow.validate_vision_fields(request, response, 2)
        for field, value in [('dealer', 'Other dealer'), ('instruction', 'CLICK CONTINUE'),
                             ('hours', '7AM - 6PM')]:
            wrong = dict(response, observed_card=dict(response['observed_card'], **{field: value}))
            with self.assertRaises(ValueError):
                flow.validate_vision_fields(request, wrong, 2)
        for update in ({'client': [400, 570]}, {'key': 'Escape'}, {'image_sha256': 'stale'},
                       {'phase': 'purchase-cancel'}):
            with self.assertRaises(ValueError):
                flow.validate_vision_fields(request, dict(response, **update), 2)

    def run_vision_controller(self, wrong_phase=False, wait_first=False, corrupt_retained=False,
                              wrong_receipt=False, ocr_balance=4419, introduction=False, intro_auto=False,
                              blank_dealer_title=False, wait_all=False, response_delay=0, passive_failure=False):
        self.vision_clicks = []
        self.guest_keys = []
        intro_pending = introduction
        with tempfile.TemporaryDirectory() as name:
            out = Path(name)
            result = {'actions': []}
            responded = set()
            calls = 0
            clock = [0.]
            def remaining():
                nonlocal calls, intro_pending
                calls += 1
                pending = out / 'career-vision-pending.json'
                if pending.exists():
                    request = json.loads(pending.read_text())
                    self.assertLessEqual(request['expires_monotonic'] - request['created_monotonic'], 10.000000001)
                    if (request['request_id'] not in responded
                            and clock[0] >= request['created_monotonic'] + response_delay):
                        responded.add(request['request_id'])
                        phase = request['phase']
                        point = [518, 470] if phase == 'purchase-cancel' else [278, 470]
                        if phase.startswith('dealer-'):
                            point = [707, 54]
                        response = {key: request[key] for key in ('request_id', 'phase', 'image_sha256', 'action')}
                        if phase == 'dealer-open' and intro_pending:
                            response.update(action=flow.INTRO_PHASE[1], client=None,
                                            observed_card=flow.EXPECTED_INTRO, reason='')
                        elif phase == 'garage-receipt':
                            receipt = {'name': 'SaveProof', 'money': 4419,
                                       'prestige': [90, 175], 'owned_car': True}
                            if wrong_receipt:
                                receipt['money'] = 4420
                            response.update(client=None, observed_receipt=receipt, reason='')
                        else:
                            response.update(client=point, observed_offer=flow.EXPECTED_OFFER, reason='')
                        if wrong_phase:
                            response['phase'] = 'wrong-phase'
                        if wait_all or (wait_first and len(responded) == 1):
                            observation = 'observed_offer'
                            response.pop('observed_card', None)
                            response.update(action='wait', client=None, **{observation: None},
                                            reason='Wait for a complete fresh phase')
                            if intro_auto:
                                intro_pending = False
                        (out / request['response']).write_text(json.dumps(response))
                return max(0, 200 - clock[0])
            def capture(stage):
                (out / (stage + '.png')).write_bytes(('unit fixture only: ' + stage).encode())
                (out / (stage + '-ocr')).mkdir(exist_ok=True)
                return {'image': stage + '.png', 'menu': {'MAIN MENU': True},
                        'dialog': {'name_title': False, 'cancel_button': False},
                        'wall_time_ns': flow.time.time_ns()}
            if passive_failure:
                capture.raw = mock.Mock(side_effect=TimeoutError('passive control'))
            def click(label, x, y):
                self.vision_clicks.append(label)
                stamp = flow.time.time_ns()
                result['actions'].append({'label': label, 'client': [x, y], 'virtual_display': ':99',
                                          'action_start_wall_time_ns': stamp, 'released_wall_time_ns': stamp})
            detector = type('Detector', (), {'evaluate_career': staticmethod(lambda *_:
                {'identity': True, 'money': True, 'welcome': False, 'load_list': True}),
                'regions': staticmethod(lambda *_: {'vision-title': 'BUY CAR',
                    'dealer-intro-title': "WELCOME TO JOE'S USED CAR DEALERSHIP!"})})()
            def guest(args):
                nonlocal intro_pending
                self.guest_keys.append(args)
                if args == ['xdotool', 'keyup', 'Return']:
                    intro_pending = False
            def dealer(*_):
                return {'car_dealer': not intro_pending and not blank_dealer_title,
                        'balance': 20000, 'model': 'Duhen SunStrip 18 DVC'}
            with mock.patch.object(flow.time, 'monotonic', side_effect=lambda: clock[0]), \
                    mock.patch.object(flow.time, 'sleep', side_effect=lambda seconds: clock.__setitem__(0, clock[0] + max(.001, seconds))), \
                    mock.patch.object(flow, 'evaluate_dealer', side_effect=dealer), \
                    mock.patch.object(flow, 'evaluate_balance', return_value=ocr_balance):
                flow.run('dealer-vision', out, result, guest, capture,
                         click, remaining, detector)
            expected_count = 5 + int(wait_first) + int(introduction and not intro_auto)
            self.assertEqual(len(list((out / 'career-vision').glob('*-request.json'))), expected_count)
            self.assertEqual(len(list((out / 'career-vision').glob('*-response.json'))), expected_count)
            self.assertTrue(flow.audit_vision(out, result)['passed'])
            if corrupt_retained:
                for review in (next(row for row in result['career']['vision_reviews'] if not row.get('waited')),
                               result['career']['vision_reviews'][-1]):
                    for relative in (review['request_file'], review['request']['response'], review['request']['image']):
                        path = out / relative
                        original = path.read_bytes()
                        path.write_bytes(original + b' ')
                        with self.assertRaises(ValueError):
                            flow.audit_vision(out, result)
                        path.write_bytes(original)
                result['actions'].append({'label': 'Verify Garage receipt', 'client': [0, 0]})
                with self.assertRaisesRegex(ValueError, 'caused input'):
                    flow.audit_vision(out, result)
                result['actions'].pop()
                action = next(row for row in result['actions'] if row['label'] == 'Open purchase')
                action['client'] = [100, 100]
                with self.assertRaisesRegex(ValueError, 'action differs'):
                    flow.audit_vision(out, result)
            return result

    def test_first_visit_review_sends_only_fixed_return_and_retains_card_binding(self):
        result = self.run_vision_controller(introduction=True)
        self.assertEqual(self.guest_keys, [['xdotool', 'keydown', 'Return'], ['xdotool', 'keyup', 'Return']])
        self.assertTrue(flow.scenario_extension('dealer-vision', result['career'])[1])
        self.assertEqual(result['career']['vision_reviews'][0]['request']['phase'], 'dealer-open')
        self.run_vision_controller()
        self.assertEqual(self.guest_keys, [])

    def test_loading_intro_transitions_to_inventory_after_no_input_wait(self):
        result = self.run_vision_controller(introduction=True, intro_auto=True, wait_first=True,
                                            blank_dealer_title=True)
        self.assertEqual(self.guest_keys, [])
        self.assertNotIn('dealer_intro', result['career'])
        self.assertEqual(result['career']['dealer_reviewed_offer'], flow.EXPECTED_OFFER)
        self.assertTrue(result['career']['vision_reviews'][0]['waited'])
        self.assertTrue(flow.scenario_extension('dealer-vision', result['career'])[1])

    def test_blank_dealer_title_does_not_hide_fresh_inventory_from_review(self):
        result = self.run_vision_controller(blank_dealer_title=True)
        self.assertEqual(self.guest_keys, [])
        self.assertFalse(result['career']['dealer']['dealer']['car_dealer'])
        self.assertTrue(flow.scenario_extension('dealer-vision', result['career'])[1])

    def test_vision_controller_consumes_four_actions_and_bound_receipt_without_input(self):
        result = self.run_vision_controller()
        self.assertTrue(result['passed'])
        self.assertEqual(self.vision_clicks[-5:], ['Open purchase', 'Cancel purchase', 'Reopen purchase',
                                                 'Confirm purchase', 'Garage Main Menu'])
        self.assertTrue(flow.scenario_extension('dealer-vision', result['career'])[1])
        self.assertNotIn('Verify Garage receipt', self.vision_clicks)
        receipt = result['career']['vision_reviews'][-1]
        self.assertEqual(receipt['request']['phase'], 'garage-receipt')
        self.assertIsNone(receipt['response']['client'])
        self.assertNotIn('receipt', receipt['frame'])
        result['career']['vision_reviews'][2]['response']['phase'] = 'purchase-buy'
        self.assertFalse(flow.scenario_extension('dealer-vision', result['career'])[1])

    def test_wrong_receipt_refuses_ordinary_exit_without_an_extra_click(self):
        with self.assertRaisesRegex(ValueError, 'receipt differs'):
            self.run_vision_controller(wrong_receipt=True)
        self.assertNotIn('Garage Main Menu', self.vision_clicks)

    def test_receipt_handles_unreadable_ocr_without_rewriting_it(self):
        result = self.run_vision_controller(ocr_balance=None)
        final = result['career']['garage_after_purchase']
        self.assertIsNone(final['balance'])
        self.assertEqual(final['receipt']['money'], 4419)

    def test_receipt_refuses_contradicting_numeric_ocr(self):
        with self.assertRaisesRegex(ValueError, 'contradicts numeric money OCR'):
            self.run_vision_controller(ocr_balance=4420)
        self.assertNotIn('Garage Main Menu', self.vision_clicks)

    def test_receipt_requires_exact_profile_finances_prestige_car_and_no_input(self):
        request = {'request_id': 'receipt', 'phase': 'garage-receipt',
                   'action': 'Verify Garage receipt', 'image_sha256': 'bound',
                   'created_monotonic': 1, 'expires_monotonic': 40}
        response = {'request_id': 'receipt', 'phase': 'garage-receipt',
                    'action': 'Verify Garage receipt', 'image_sha256': 'bound',
                    'client': None, 'observed_receipt': {'name': 'SaveProof', 'money': 4419,
                        'prestige': [90, 175], 'owned_car': True}, 'reason': ''}
        flow.validate_vision_fields(request, response, 2)
        for key, bad in [('name', 'Other'), ('money', 4420), ('money', 4419.0),
                         ('prestige', [90, 139]), ('prestige', [90, 175.0]),
                         ('owned_car', False), ('owned_car', 1)]:
            changed = json.loads(json.dumps(response))
            changed['observed_receipt'][key] = bad
            with self.assertRaises(ValueError):
                flow.validate_vision_fields(request, changed, 2)
        for key, bad in [('client', [764, 48]), ('image_sha256', 'wrong'),
                         ('phase', 'purchase-buy')]:
            changed = dict(response, **{key: bad})
            with self.assertRaises(ValueError):
                flow.validate_vision_fields(request, changed, 2)
        with self.assertRaises(ValueError):
            flow.validate_vision_fields(request, response, 41)

    def test_vision_controller_never_clicks_on_wrong_phase_response(self):
        with self.assertRaisesRegex(ValueError, 'binding differs'):
            self.run_vision_controller(wrong_phase=True)
        self.assertNotIn('Open purchase', self.vision_clicks)

    def test_passive_failure_preserves_delayed_action_response(self):
        result = self.run_vision_controller(response_delay=4, passive_failure=True)
        review = result['career']['vision_reviews'][0]
        self.assertIn('TimeoutError', review['passive_frames'][0]['error'])
        self.assertEqual(review['response']['action'], 'Open purchase')
        self.assertEqual(self.vision_clicks.count('Open purchase'), 1)

    def test_response_deadline_exhaustion_cannot_cause_controller_input(self):
        with self.assertRaisesRegex(TimeoutError, 'expired=3'):
            self.run_vision_controller(response_delay=11, passive_failure=True)
        self.assertNotIn('Open purchase', self.vision_clicks)
        self.assertEqual(self.guest_keys, [])

    def test_three_valid_waits_report_exhaustion_without_input(self):
        with self.assertRaisesRegex(TimeoutError, 'valid waits=3, introductions=0, expired=0'):
            self.run_vision_controller(wait_all=True)
        self.assertNotIn('Open purchase', self.vision_clicks)
        self.assertEqual(self.guest_keys, [])

    def test_vision_wait_recaptures_without_repeating_input(self):
        result = self.run_vision_controller(wait_first=True)
        self.assertTrue(result['career']['vision_reviews'][0]['waited'])
        self.assertEqual(self.vision_clicks.count('Open purchase'), 1)
        self.assertTrue(flow.scenario_extension('dealer-vision', result['career'])[1])

    def test_vision_final_audit_detects_retained_frame_request_response_and_action_changes(self):
        self.run_vision_controller(corrupt_retained=True)

    def test_vision_response_binds_exact_frame_phase_offer_and_client(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            image = root / 'frame.png'
            image.write_bytes(b'retained test frame, not a Stock observation')
            request = {'request_id': 'one', 'phase': 'purchase-cancel',
                       'image_sha256': hashlib.sha256(image.read_bytes()).hexdigest(),
                       'action': 'Cancel purchase', 'created_monotonic': 1.0, 'expires_monotonic': 46.0}
            good = {'request_id': 'one', 'phase': 'purchase-cancel', 'image_sha256': request['image_sha256'],
                    'action': 'Cancel purchase', 'client': [518, 470],
                    'observed_offer': {'model': 'Duhen SunStrip 1.8 DVC', 'price': 15581, 'balance': 20000},
                    'reason': ''}
            response = root / 'response.json'
            response.write_text(json.dumps(good))
            accepted, identity = flow.read_vision_response(request, response, image, 2.0)
            self.assertEqual(accepted, good)
            self.assertEqual(identity, hashlib.sha256(response.read_bytes()).hexdigest())
            for changes in ({'request_id': 'old'}, {'phase': 'purchase-buy'}, {'image_sha256': 'changed'},
                            {'action': 'Confirm purchase'}, {'client': [800, 470]}, {'client': [True, 470]},
                            {'observed_offer': dict(good['observed_offer'], price=15582)},
                            {'observed_offer': dict(good['observed_offer'], price=15581.0)}):
                response.write_text(json.dumps(dict(good, **changes)))
                with self.assertRaises(ValueError):
                    flow.read_vision_response(request, response, image, 2.0)
            response.write_text(json.dumps(good))
            response.write_text(response.read_text()[:-1] + ',"phase":"purchase-cancel"}')
            with self.assertRaisesRegex(ValueError, 'duplicate'):
                flow.read_vision_response(request, response, image, 2.0)
            response.write_text(json.dumps(good))
            with self.assertRaisesRegex(ValueError, 'expired'):
                flow.read_vision_response(request, response, image, 47.0)
            image.write_bytes(b'changed frame')
            with self.assertRaisesRegex(ValueError, 'frame changed'):
                flow.read_vision_response(request, response, image, 2.0)

    def test_vision_response_refuses_links_and_oversized_input(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            target = root / 'target'
            target.write_text('{}')
            response = root / 'response'
            response.symlink_to(target)
            with self.assertRaisesRegex(ValueError, 'ordinary'):
                flow.read_vision_response({}, response, target, 0)
            response.unlink()
            response.write_bytes(b'x' * 4097)
            with self.assertRaisesRegex(ValueError, 'size'):
                flow.read_vision_response({}, response, target, 0)
    def dealer_scenario(self, dealer_labels, mode='dealer-inspect', cancelled_balance=20000):
        clicks, observations = [], []
        self.dealer_clicks = clicks
        labels = iter(dealer_labels)
        count = 0

        def remaining():
            nonlocal count
            count += 1
            return max(0, 80 - count)

        def capture(stage):
            observations.append(stage)
            return {'image': stage + '.png', 'menu': {'MAIN MENU': True},
                    'dialog': {'name_title': False, 'cancel_button': False}}

        def career(*_):
            return {'identity': True, 'money': True, 'welcome': False, 'load_list': True}

        def dealer(image, *_):
            return {'car_dealer': next(labels, False), 'balance':
                    cancelled_balance if image.stem == 'dealer-after-cancel' else 20000,
                    'model': 'Duhen SunStrip 1.8 DVC'}


        with mock.patch.object(flow.time, 'sleep'), \
                mock.patch.object(flow, 'evaluate_dealer', side_effect=dealer, create=True), \
                mock.patch.object(flow, 'evaluate_balance', return_value=4419, create=True):
            result = {'actions': []}
            flow.run(mode, Path('/owned-fixture'), result, lambda _: None,
                     capture, lambda label, *_: clicks.append(label), remaining,
                     type('Detector', (), {'evaluate_career': staticmethod(career)})())
        return result, clicks, observations

    def test_dealer_inventory_is_observed_before_return_and_never_buys(self):
        result, clicks, observations = self.dealer_scenario([False, True])
        self.assertTrue(result['passed'])
        self.assertEqual(clicks, ['Load Career', 'Select SaveProof', 'Load selected Career',
                                  'Used Car Dealer', 'Dealer Return', 'Garage Main Menu'])
        self.assertEqual(observations.count('dealer-inventory'), 2)
        self.assertTrue(result['career']['dealer']['dealer']['car_dealer'])

    def test_unknown_dealer_frontier_refuses_without_return_or_buy(self):
        with self.assertRaisesRegex(TimeoutError, 'dealer-inventory'):
            self.dealer_scenario([])




    def test_default_career_prefix_still_requires_original_money(self):
        raw = bytearray(51)
        raw[:8] = b'SDAT\0\1\2\0'
        struct.pack_into('<IIIifiiI', raw, 8, 0x87654321, 21, 1, 4419, 0, 0, 0, 10)
        raw[40:50] = b'SaveProof\0'
        with tempfile.TemporaryDirectory() as name:
            path = Path(name) / 'main'
            path.write_bytes(raw)
            with self.assertRaisesRegex(ValueError, 'profile differs'):
                flow.saved_prefix(path)
            self.assertEqual(flow.saved_prefix(path, expected_money=4419)['money'], 4419)


    def scenario(self, portrait_labels):
        self.clicks = []
        self.observed = []
        self.calls = 0
        labels = iter(portrait_labels)

        def remaining():
            self.calls += 1
            return max(0, 40 - self.calls)

        def capture(stage):
            self.observed.append(stage)
            name = stage == 'career-name'
            return {'image': stage + '.png', 'menu': {'MAIN MENU': True},
                    'dialog': {'name_title': name, 'cancel_button': name}}

        def detect(image, *_):
            stage = image.stem
            return {'character': next(labels, False) if stage == 'career-portrait-selected' else True,
                    'identity': True, 'money': True, 'welcome': False}

        with mock.patch.object(flow.time, 'sleep'), mock.patch.object(flow.time, 'time_ns', return_value=1):
            result = {'actions': []}
            flow.run('create', Path('/owned-fixture'), result, lambda _: None,
                     capture, lambda label, *_: self.clicks.append(label), remaining,
                     type('Detector', (), {'evaluate_career': staticmethod(detect)})())
            return result

    def test_transient_missing_title_waits_without_repeating_portrait_input(self):
        result = self.scenario([False, True])
        self.assertTrue(result['passed'])
        self.assertEqual(self.clicks.count('Next portrait'), 1)
        self.assertEqual(self.observed.count('career-portrait-selected'), 2)
        self.assertIn('Accept character', self.clicks)

    def test_persistently_missing_title_never_accepts_character(self):
        with self.assertRaisesRegex(TimeoutError, 'career-portrait-selected'):
            self.scenario([])
        self.assertNotIn('Accept character', self.clicks)
        self.assertEqual(self.clicks.count('Next portrait'), 1)


if __name__ == '__main__':
    unittest.main()
