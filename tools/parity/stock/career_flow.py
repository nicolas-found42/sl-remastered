"""Bounded ordinary Career UI actions within the guest's private XTEST display."""
import json
import hashlib
import struct
import time
import re
import os
from pathlib import Path
import stat
import math


OFFER_REFERENCE = 'local/verify/parity-goal-20261102-092437/factory-entry-first/issue53-stock-used-dealer-inspect-v1/state-047-dealer-inventory.png'
OFFER_REFERENCE_SHA256 = '475079fe2df6fb9550a0deed5140450d416f5ed2c36163561add1732d91adc55'
EXPECTED_OFFER = {'model': 'Duhen SunStrip 1.8 DVC', 'price': 15581, 'balance': 20000}
EXPECTED_RECEIPT = {'name': 'SaveProof', 'money': 4419, 'prestige': [90, 175], 'owned_car': True}
INTRO_PHASE = ('dealer-intro', 'Continue dealer introduction')
EXPECTED_INTRO = {'dealer': "Joe's Used Car Dealership", 'instruction': 'PRESS ENTER TO CONTINUE',
                  'hours': '7AM - 5PM'}
INTRO_REFERENCE = 'local/verify/parity-goal-20261102-092437/factory-depth-first/issue53-stock-dealer-vision-receipt-v1/state-052-dealer-inventory.png'
INTRO_REFERENCE_SHA256 = '3b7c73295557b7680f0156de61a68bb8f8446753e47f8840e953a2b96c7df4b7'
RECEIPT_REFERENCE = 'local/verify/parity-goal-20261102-092437/factory-edx-mutant-second/issue53-stock-dealer-vision-v3/state-065-career-after-purchase.png'
RECEIPT_REFERENCE_SHA256 = 'eef0d158fcebec58f29e4e40dd50f65152c27158a5172b8e6c8f488245c4d9e6'
VISION_PHASES = [('dealer-open', 'Open purchase'), ('purchase-cancel', 'Cancel purchase'),
                 ('dealer-reopen', 'Reopen purchase'), ('purchase-buy', 'Confirm purchase'),
                 ('garage-receipt', 'Verify Garage receipt')]


def validate_vision_fields(request, response, now):
    receipt = request['phase'] == 'garage-receipt'
    intro = response.get('action') == INTRO_PHASE[1] if isinstance(response, dict) else False
    observation = 'observed_card' if intro else 'observed_receipt' if receipt else 'observed_offer'
    keys = {'request_id', 'phase', 'image_sha256', 'action', 'client', observation, 'reason'}
    if not isinstance(response, dict) or set(response) != keys:
        raise ValueError('vision response fields differ')
    start, end = request['created_monotonic'], request['expires_monotonic']
    if not (all(type(value) in (int, float) and math.isfinite(value) for value in (start, end, now))
            and 0 < end - start <= 45 and start <= now <= end):
        raise ValueError('vision response expired')
    for key in ('request_id', 'phase', 'image_sha256'):
        if response[key] != request[key]:
            raise ValueError('vision response binding differs: ' + key)
    if response['action'] in ('wait', 'refuse'):
        if (response['client'] is not None or response[observation] is not None
                or not isinstance(response['reason'], str) or not 0 < len(response['reason']) <= 500):
            raise ValueError('vision non-action response unsupported')
        if response['action'] == 'refuse':
            raise ValueError('vision reviewer refused: ' + response['reason'])
        return
    if (response['action'] != request['action'] and not intro) or response['reason'] != '':
        raise ValueError('vision action differs from requested phase')
    if intro:
        if (request['phase'] != 'dealer-open' or request['action'] != 'Open purchase' or response['client'] is not None
                or response['observed_card'] != EXPECTED_INTRO):
            raise ValueError('vision response introduction differs from bounded card')
        return
    if receipt:
        value = response['observed_receipt']
        if (request['action'] != 'Verify Garage receipt' or response['client'] is not None
                or not isinstance(value, dict) or value != EXPECTED_RECEIPT
                or type(value.get('money')) is not int or type(value.get('owned_car')) is not bool
                or not isinstance(value.get('prestige'), list)
                or any(type(part) is not int for part in value['prestige'])):
            raise ValueError('vision response receipt differs from bounded fixture')
        return
    point = response['client']
    if (not isinstance(point, list) or len(point) != 2 or any(type(v) is not int for v in point)
            or not 0 <= point[0] < 800 or not 0 <= point[1] < 600):
        raise ValueError('vision response client coordinate unsupported')
    offer = response['observed_offer']
    if (not isinstance(offer, dict) or offer != EXPECTED_OFFER
            or type(offer.get('price')) is not int or type(offer.get('balance')) is not int):
        raise ValueError('vision response offer differs from bounded fixture')


def read_vision_response(request, response_path, image, now):
    """Read one exact-frame action; this cannot execute arbitrary guest commands."""
    before = response_path.lstat()
    if not stat.S_ISREG(before.st_mode) or before.st_nlink != 1:
        raise ValueError('vision response must be an ordinary single-link file')
    if not 0 < before.st_size <= 4096:
        raise ValueError('vision response size unsupported')
    descriptor = os.open(response_path, os.O_RDONLY | os.O_NOFOLLOW)
    try:
        opened = os.fstat(descriptor)
        if (opened.st_dev, opened.st_ino) != (before.st_dev, before.st_ino):
            raise ValueError('vision response replaced while opening')
        raw = os.read(descriptor, 4097)
        after = os.fstat(descriptor)
        if ((after.st_size, after.st_mtime_ns, after.st_ctime_ns) !=
                (before.st_size, before.st_mtime_ns, before.st_ctime_ns) or len(raw) != before.st_size):
            raise ValueError('vision response changed while reading')
    finally:
        os.close(descriptor)
    if image.is_symlink() or not image.is_file() or hashlib.sha256(image.read_bytes()).hexdigest() != request['image_sha256']:
        raise ValueError('vision frame changed')
    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('duplicate vision response field')
            result[key] = value
        return result
    response = json.loads(raw, object_pairs_hook=unique_object)
    validate_vision_fields(request, response, now)
    return response, hashlib.sha256(raw).hexdigest()




def evaluate_dealer(image, out, detector, deadline):
    """Observe the proposed Used Cars frontier; absence never authorizes input."""
    texts = detector.regions(image, out, [
        ('dealer-title', 'crop=180:35:0:0,scale=720:140:flags=lanczos', '7'),
        ('dealer-model', 'crop=480:35:190:0,scale=1920:140:flags=lanczos', '7'),
        ('dealer-balance', 'crop=130:35:670:0,scale=520:140:flags=lanczos', '7')], deadline)
    currency = re.fullmatch(r'\s*\$([0-9]{1,7})\s*', texts['dealer-balance'])
    labels = {'car_dealer': re.search(r'\bCAR\s+DEALER\b', texts['dealer-title'].upper()) is not None,
              'balance': int(currency[1]) if currency else None,
              'model': texts['dealer-model'].split('$')[0].strip()}
    (out / 'dealer-ocr.json').write_text(json.dumps({'source': str(image), 'texts': texts, 'labels': labels}, indent=2))
    return labels


def evaluate_balance(image, out, detector, deadline):
    texts = detector.regions(image, out, [
        ('observed-money', 'crop=180:25:620:0,scale=720:100:flags=lanczos', '7')], deadline)
    match = re.search(r'\$([0-9]{1,7})\s*$', texts['observed-money'])
    value = int(match[1]) if match else None
    (out / 'money-ocr.json').write_text(json.dumps({'source': str(image), 'texts': texts, 'money': value}, indent=2))
    return value






def scenario_extension(mode, career):
    if mode not in ('dealer-inspect', 'dealer-vision'):
        return [], True
    dealer = career.get('dealer', {}).get('dealer', {})
    if mode == 'dealer-vision':
        expected = VISION_PHASES
        reviews = [row for row in career.get('vision_reviews', [])
                   if row.get('response', {}).get('action') == row.get('request', {}).get('action')]
        valid = len(reviews) == len(expected)
        try:
            for row, (phase, action) in zip(reviews, expected):
                validate_vision_fields(row['request'], row['response'], row['consumed_monotonic'])
                valid &= row['request']['phase'] == phase and row['request']['action'] == action
        except (ValueError, KeyError, TypeError):
            valid = False
        cancelled = career.get('cancelled', {}).get('dealer', {})
        final = career.get('garage_after_purchase', {})
        valid &= (career.get('dealer_reviewed_offer') == EXPECTED_OFFER
                  and career.get('cancelled_reviewed_offer') == EXPECTED_OFFER
                  and dealer.get('balance') in (None, 20000) and cancelled.get('balance') in (None, 20000)
                  and final.get('career', {}).get('identity') is True
                  and final.get('receipt') == EXPECTED_RECEIPT
                  and final.get('balance') in (None, 4419)
                  and final.get('receipt', {}).get('money') == career.get('expected_money') == 4419)
        return (['Used Car Dealer'] + ([INTRO_PHASE[1]] if career.get('dealer_intro') is not None else [])
                + [action for phase, action in expected if phase != 'garage-receipt']), valid
    returned = career.get('garage_return', {}).get('career', {})
    return ['Used Car Dealer', 'Dealer Return'], (
        dealer.get('car_dealer') is True and dealer.get('balance') == 20000 and returned.get('identity') is True
        and returned.get('money') is True and returned.get('welcome') is False)


def audit_vision(out, flow):
    """Re-read immutable records after execution, rather than trust flow flags."""
    root = out.resolve(strict=True)
    career = flow['career']
    if not scenario_extension('dealer-vision', career)[1]:
        raise ValueError('vision phase or financial evidence incomplete')
    expected = VISION_PHASES
    history = career['vision_reviews']
    if not len(expected) <= len(history) <= 3 * len(expected):
        raise ValueError('vision request count unsupported')
    def owned_file(relative, maximum):
        if not isinstance(relative, str) or Path(relative).is_absolute() or '..' in Path(relative).parts:
            raise ValueError('vision record path unsupported')
        path = root / relative
        if path.resolve(strict=True) != path:
            raise ValueError('vision record path is aliased')
        info = path.lstat()
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or not 0 < info.st_size <= maximum:
            raise ValueError('vision record is not an ordinary bounded file')
        return path
    verified, observations, phase_index, attempts = [], [], 0, {}
    for number, review in enumerate(history, 1):
        request_path = owned_file(review['request_file'], 4096)
        raw_request = request_path.read_bytes()
        request = json.loads(raw_request)
        if request != review['request'] or hashlib.sha256(raw_request).hexdigest() != review['request_sha256']:
            raise ValueError('vision retained request changed')
        attempts[request['phase']] = attempts.get(request['phase'], 0) + 1
        if attempts[request['phase']] > 3:
            raise ValueError('too many phase review records')
        if phase_index >= len(expected) or (request['phase'], request['action']) != expected[phase_index]:
            raise ValueError('vision retained phase order differs')
        stem = f"career-vision/{number:03d}-{request['phase']}"
        if review['request_file'] != stem + '-request.json' or request['response'] != stem + '-response.json':
            raise ValueError('vision retained record names differ')
        image = owned_file(request['image'], 16 * 1024 * 1024)
        if (hashlib.sha256(image.read_bytes()).hexdigest() != request['image_sha256']
                or review['frame']['image'] != request['image']):
            raise ValueError('vision retained frame changed')
        if 'response' not in review:
            if review.get('expired') is not True:
                raise ValueError('vision request lacks a response or expiration')
            continue
        response_path = owned_file(request['response'], 4096)
        response, identity = read_vision_response(request, response_path, image, review['consumed_monotonic'])
        if response != review['response'] or identity != review['response_sha256']:
            raise ValueError('vision retained response changed')
        if response['action'] == 'wait':
            if review.get('waited') is not True:
                raise ValueError('vision wait was not recorded')
            continue
        actions = [row for row in flow['actions'] if row.get('label') == response['action']]
        if response['action'] == INTRO_PHASE[1]:
            if (len(actions) != 1 or actions[0].get('key') != 'Return'
                    or actions[0].get('client') is not None or actions[0].get('virtual_display') != ':99'
                    or not review['frame']['wall_time_ns'] <= actions[0]['action_start_wall_time_ns']
                    <= actions[0]['keydown_wall_time_ns'] <= actions[0]['released_wall_time_ns']
                    or career['dealer_intro']['image'] != request['image']):
                raise ValueError('vision retained introduction key differs')
            verified.append({'phase': request['phase'], 'image': request['image'],
                             'image_sha256': request['image_sha256'], 'action': actions[0],
                             'request_sha256': review['request_sha256'], 'response_sha256': identity})
            continue
        if request['phase'] == 'garage-receipt':
            final = career['garage_after_purchase']
            if (actions or final['image'] != request['image']
                    or final['receipt'] != response['observed_receipt']):
                raise ValueError('vision retained receipt differs or caused input')
            observations.append({'phase': request['phase'], 'image': request['image'],
                                 'image_sha256': request['image_sha256'],
                                 'request_sha256': review['request_sha256'],
                                 'response_sha256': identity, 'receipt': response['observed_receipt']})
            phase_index += 1
            continue
        if (len(actions) != 1 or actions[0].get('client') != response['client']
                or actions[0].get('virtual_display') != ':99'
                or not review['frame']['wall_time_ns'] <= actions[0]['action_start_wall_time_ns'] <= actions[0]['released_wall_time_ns']):
            raise ValueError('vision retained action differs from reviewed target')
        if request['phase'] in ('dealer-open', 'dealer-reopen'):
            field = 'dealer' if request['phase'] == 'dealer-open' else 'cancelled'
            if (career[field]['image'] != request['image']
                    or career[field + '_reviewed_offer'] != response['observed_offer']):
                raise ValueError('vision inventory proof differs from action frame')
        verified.append({'phase': request['phase'], 'image': request['image'], 'image_sha256': request['image_sha256'],
                         'request_sha256': review['request_sha256'], 'response_sha256': identity,
                         'action': actions[0]})
        phase_index += 1
    if phase_index != len(expected):
        raise ValueError('vision accepted phases incomplete')
    return {'passed': True, 'actions': verified, 'observations': observations,
            'scope': 'functional exact-frame reviewed private XTEST actions; no response replay or paired parity'}


def saved_prefix(path, expected_portrait=None, expected_money=20000):
    if path.is_symlink() or not path.is_file() or not 40 < path.stat().st_size <= 16 * 1024 * 1024:
        raise ValueError('missing or unsupported saved Career')
    raw = path.read_bytes()
    if raw[:8] != b'SDAT\0\1\2\0':
        raise ValueError('unexpected saved Career envelope')
    identifier, version, portrait, money, _, racer, flags, size = struct.unpack_from('<IIIifiiI', raw, 8)
    if identifier != 0x87654321 or version != 21 or size != 10 or raw[40:50] != b'SaveProof\0':
        raise ValueError('saved Career identity was not observed')
    if type(expected_money) is not int or not 0 <= expected_money <= 20000:
        raise ValueError('unsupported expected Career money')
    if not portrait or money != expected_money or racer != 0 or flags != 0:
        raise ValueError('saved Career profile differs from selected ordinary fixture')
    if expected_portrait is not None and portrait != expected_portrait:
        raise ValueError('loaded Career portrait differs from the actual source fixture')
    return {'path': str(path), 'name': 'SaveProof', 'portrait_word': portrait, 'money': money,
            'racer_index': racer, 'flags': flags, 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest(),
            'limits': ['prefix inspection only; independent whole SDAT audit is still required']}


def run(mode, out, result, guest, capture, click, remaining, detector):
    evidence = result['career'] = {'mode': mode, 'name': 'SaveProof', 'welcome_dismissed': False}

    def observe(stage):
        row = capture(stage)
        row['career'] = detector.evaluate_career(out / row['image'], out / (row['image'][:-4] + '-ocr'),
                                                time.monotonic() + remaining())
        if stage.startswith('dealer-') or stage.startswith('vision-dealer-'):
            row['dealer'] = evaluate_dealer(out / row['image'], out / (row['image'][:-4] + '-ocr'),
                                             detector, time.monotonic() + remaining())
        if stage.startswith('vision-dealer-'):
            row['ocr_phase_hint'] = row['dealer']['car_dealer'] and row['dealer']['balance'] == 20000
        if stage.startswith('vision-purchase-'):
            directory = out / (row['image'][:-4] + '-ocr')
            texts = detector.regions(out / row['image'], directory, [
                ('vision-title', 'crop=300:30:250:120,scale=1200:120:flags=lanczos', '7')],
                time.monotonic() + remaining())
            row['ocr_phase_hint'] = re.search(r'\bBUY\s+CAR\b', texts['vision-title'].upper()) is not None
            (directory / 'vision-phase-ocr.json').write_text(json.dumps({'texts': texts,
                'ready': row['ocr_phase_hint']}, indent=2))
        if stage in ('career-after-purchase', 'vision-garage-receipt'):
            row['balance'] = evaluate_balance(out / row['image'], out / (row['image'][:-4] + '-ocr'),
                                               detector, time.monotonic() + remaining())
        if stage == 'vision-garage-receipt':
            row['ocr_phase_hint'] = row['career']['identity'] and not row['career']['welcome']
        if stage.startswith('vision-'):
            # OCR remains retained evidence, never a prerequisite for presenting
            # the fresh frame to the bounded phase reviewer.
            row['vision_phase_ready'] = True
        return row

    def await_state(stage, predicate):
        while remaining() > 0:
            row = observe(stage)
            if predicate(row):
                return row
            time.sleep(.6)
        raise TimeoutError('Career UI frontier not observed: ' + stage)

    def vision_action(phase, action):
        directory = out / 'career-vision'
        directory.mkdir(exist_ok=True)
        reviews = evidence.setdefault('vision_reviews', [])
        for _ in range(3):
            if remaining() <= 0:
                break
            row = await_state('vision-' + phase, lambda observed: observed.get('vision_phase_ready') is True)
            image = out / row['image']
            created = time.monotonic()
            number = len(reviews) + 1
            identity = hashlib.sha256(image.read_bytes()).hexdigest()
            request = {'request_id': hashlib.sha256((phase + identity + str(time.time_ns())).encode()).hexdigest(),
                       'phase': phase, 'action': action, 'image': row['image'], 'image_sha256': identity,
                       'created_monotonic': created, 'expires_monotonic': created + min(45, remaining()),
                       'expected_offer': EXPECTED_OFFER,
                       'fixture_reference': {'image': OFFER_REFERENCE, 'sha256': OFFER_REFERENCE_SHA256},
                       'review_required': 'View this exact fresh frame; respond only if the shown phase, target and offer are unambiguous.'}
            if phase == 'garage-receipt':
                request['expected_receipt'] = EXPECTED_RECEIPT
                request['fixture_reference'] = {'image': RECEIPT_REFERENCE, 'sha256': RECEIPT_REFERENCE_SHA256}
                request['review_required'] = 'View this exact fresh Garage frame; verify profile, money, prestige and owned car. This response must cause no input.'
            if phase == 'dealer-open':
                request['expected_card'] = EXPECTED_INTRO
                request['intro_reference'] = {'image': INTRO_REFERENCE, 'sha256': INTRO_REFERENCE_SHA256}
                request['allowed_actions'] = ['Open purchase', INTRO_PHASE[1], 'wait', 'refuse']
                request['review_required'] = 'View this fresh dealer frame. Loading: wait without input. Exact Joe card with PRESS ENTER and7AM-5PM: fixed Return only. Inventory: verify exact offer and cart target before Open purchase.'
            stem = f'{number:03d}-{phase}'
            request_path = directory / (stem + '-request.json')
            response_path = directory / (stem + '-response.json')
            request['response'] = str(response_path.relative_to(out))
            with request_path.open('x') as stream:
                json.dump(request, stream, indent=2)
            temporary = directory / (stem + '-pending.tmp')
            with temporary.open('x') as stream:
                json.dump(request, stream, indent=2)
            temporary.replace(out / 'career-vision-pending.json')
            review = {'request': request, 'frame': row, 'request_file': str(request_path.relative_to(out)),
                      'request_sha256': hashlib.sha256(request_path.read_bytes()).hexdigest()}
            reviews.append(review)
            while remaining() > 0 and time.monotonic() <= request['expires_monotonic']:
                if response_path.exists() or response_path.is_symlink():
                    consumed = time.monotonic()
                    response, response_hash = read_vision_response(request, response_path, image, consumed)
                    review.update(response=response, consumed_monotonic=consumed,
                                  response_sha256=response_hash)
                    if response['action'] == 'wait':
                        review['waited'] = True
                        time.sleep(.3)
                        break
                    if response['action'] == INTRO_PHASE[1]:
                        if evidence.get('dealer_intro') is not None:
                            raise ValueError('dealer introduction already acknowledged')
                        if remaining() < 2:
                            raise TimeoutError('insufficient time for introduction key release')
                        evidence['dealer_intro'] = row
                        key_action = {'label': INTRO_PHASE[1], 'key': 'Return', 'client': None,
                                      'virtual_display': ':99', 'action_start_wall_time_ns': time.time_ns()}
                        try:
                            guest(['xdotool', 'keydown', 'Return'])
                            key_action['keydown_wall_time_ns'] = time.time_ns()
                            time.sleep(.15)
                        finally:
                            guest(['xdotool', 'keyup', 'Return'])
                        key_action['released_wall_time_ns'] = time.time_ns()
                        result['actions'].append(key_action)
                        review['introduction_acknowledged'] = True
                        break
                    return row, response
                time.sleep(.15)
            if not review.get('waited') and not review.get('introduction_acknowledged'):
                review['expired'] = True
        raise TimeoutError('vision response not observed: ' + phase)

    if mode == 'create':
        click('New Career', 300, 120)
        evidence['name_dialog'] = await_state('career-name', lambda row: all(row['dialog'].values()))
        sent = time.time_ns()
        guest(['xdotool', 'type', '--clearmodifiers', '--delay', '200', 'SaveProof'])
        result['actions'].append({'label': 'Type Career name', 'text': 'SaveProof',
                                  'virtual_display': ':99', 'request_wall_time_ns': sent,
                                  'returned_wall_time_ns': time.time_ns()})
        time.sleep(.6)
        evidence['name_typed'] = observe('career-name-typed')
        click('Accept name', 320, 470)
        evidence['character_before'] = await_state('career-character', lambda row: row['career']['character'])
        click('Next portrait', 544, 372)
        time.sleep(.6)
        # A retained Stock portrait redraw frame had a blank title. Require the same frontier again before acceptance;
        # an absent label is an observation to wait on, not proof of dismissal.
        evidence['character_after'] = await_state(
            'career-portrait-selected', lambda row: row['career']['character'])
        click('Accept character', 400, 470)
    elif mode in ('load', 'dealer-inspect', 'dealer-vision'):
        click('Load Career', 280, 156)
        evidence['load_list'] = await_state('career-load-list', lambda row: row['career']['load_list'])
        click('Select SaveProof', 400, 201)
        time.sleep(.6)
        evidence['load_selected'] = observe('career-load-selected')
        click('Load selected Career', 320, 470)
    else:
        raise ValueError('unsupported Career flow')

    evidence['garage'] = await_state('career-garage', lambda row: row['career']['identity'] and row['career']['money'])
    if evidence['garage']['career']['welcome']:
        click('Dismiss Welcome', 400, 470)
        evidence['welcome_dismissed'] = True
        time.sleep(.6)
        evidence['garage_after_welcome'] = observe('career-garage-ready')
        if not all(evidence['garage_after_welcome']['career'][key] for key in ('identity', 'money')):
            raise ValueError('Garage identity disappeared after Welcome')
    if mode in ('dealer-inspect', 'dealer-vision'):
        # The third car icon is present at this location in the retained
        # actual Stock 800x600 Garage frame; its destination remains an
        # experiment until the independently retained dealer label appears.
        click('Used Car Dealer', 260, 48)
        if mode == 'dealer-vision':
            evidence['dealer'], response = vision_action('dealer-open', 'Open purchase')
            if evidence['dealer']['dealer']['balance'] not in (None, 20000):
                raise ValueError('dealer inventory contradicts numeric balance OCR')
            evidence['dealer_reviewed_offer'] = response['observed_offer']
            click('Open purchase', *response['client'])
            evidence['purchase_confirmation'], response = vision_action('purchase-cancel', 'Cancel purchase')
            click('Cancel purchase', *response['client'])
            evidence['cancelled'], response = vision_action('dealer-reopen', 'Reopen purchase')
            if evidence['cancelled']['dealer']['balance'] not in (None, 20000):
                raise ValueError('cancelled inventory contradicts numeric balance OCR')
            evidence['cancelled_reviewed_offer'] = response['observed_offer']
            click('Reopen purchase', *response['client'])
            evidence['purchase_reopened'], response = vision_action('purchase-buy', 'Confirm purchase')
            evidence['expected_money'] = 20000 - response['observed_offer']['price']
            click('Confirm purchase', *response['client'])
            final, receipt = vision_action('garage-receipt', 'Verify Garage receipt')
            if final['balance'] not in (None, EXPECTED_RECEIPT['money']):
                raise ValueError('Garage receipt contradicts numeric money OCR')
            evidence['garage_after_purchase'] = dict(final, receipt=receipt['observed_receipt'])
        elif mode == 'dealer-inspect':
            evidence['dealer'] = await_state('dealer-inventory', lambda row: row['dealer']['car_dealer']
                                              and row['dealer']['balance'] == 20000 and bool(row['dealer']['model']))
            click('Dealer Return', 774, 48)
            evidence['garage_return'] = await_state(
                'career-garage-return', lambda row: row['career']['identity'] and row['career']['money']
                and not row['career']['welcome'])
    click('Garage Main Menu', 764, 48)
    result['after'] = await_state('career-menu-return',
                                 lambda row: all(row['menu'].values()) and not any(row['dialog'].values()))
    evidence['passed'] = True
    result['passed'] = True
