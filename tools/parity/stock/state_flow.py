"""Observe actual guest UI before and after coherent virtual-XTEST actions."""
import importlib.util
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time


def press_observed_button(guest, remaining, read_log, record=None):
    """One private press, bounded by fresh native down/up observations.

    A slow guest must actually poll the held button. No timeout repeats input.
    The common deadline leaves release time within the original global budget.
    """
    if remaining() < .75:
        raise TimeoutError('insufficient budget for observed button release')
    baseline = read_log()
    if not baseline.endswith(b'\n'):
        raise ValueError('native input baseline is incomplete')
    def transitions(raw):
        complete = b''.join(line for line in raw.splitlines(keepends=True) if line.endswith(b'\n'))
        rows = [(int(value), int(call)) for value, call in re.findall(
            rb'input_poll: kind=1 offset=12 data=(\d+) source=native state_call=(\d+)', complete)]
        if (not rows or len(rows) > 4096 or any(value != (128 if index % 2 else 0)
                for index, (value, _) in enumerate(rows))
                or any(rows[index][1] <= rows[index-1][1] for index in range(1, len(rows)))):
            raise ValueError('native input transitions malformed or discontinuous')
        return rows
    before = transitions(baseline)
    if before[-1][0] != 0:
        raise ValueError('native button is not released before press')
    deadline = min(time.monotonic()+2, time.monotonic()+remaining()-.5)
    result = {} if record is None else record
    result.update(native_baseline_bytes=len(baseline),
                  native_baseline_sha256=hashlib.sha256(baseline).hexdigest(),
                  native_baseline_transitions=len(before))
    def await_transition(values):
        while time.monotonic() < deadline:
            raw = read_log()
            if not raw.startswith(baseline):
                raise ValueError('native input baseline replaced')
            rows = transitions(raw)
            if rows[:len(before)] != before or len(rows) > len(before)+len(values):
                raise ValueError('unexpected native input transition during press')
            if [value for value, _ in rows[len(before):]] == values:
                return
            time.sleep(.02)
        raise TimeoutError('private button native acknowledgement not observed')
    try:
        down_started = time.monotonic()
        result['button_down_request_wall_time_ns'] = time.time_ns()
        guest(['xdotool', 'mousedown', '1'])
        await_transition([128])
        result['native_down_ack_wall_time_ns'] = time.time_ns()
        result['native_down_latency_seconds'] = time.monotonic()-down_started
    finally:
        up_started = time.monotonic()
        result['button_up_request_wall_time_ns'] = time.time_ns()
        guest(['xdotool', 'mouseup', '1'])
        result['released_wall_time_ns'] = time.time_ns()
    await_transition([128, 0])
    result['native_up_ack_wall_time_ns'] = time.time_ns()
    result['native_up_latency_seconds'] = time.monotonic()-up_started
    result['native_ack_transitions'] = [128, 0]
    return result


def run(container, out, process, deadline, ordinary_exit=False, career_flow=None, input_observer=False):
    here = Path(__file__).resolve().parent
    spec = importlib.util.spec_from_file_location('stock_menu_ocr', here / 'menu_ocr.py')
    detector = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(detector)
    input_tape = None
    if input_observer:
        spec = importlib.util.spec_from_file_location('stock_input_tape', here / 'stock_input_tape.py')
        input_tape = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(input_tape)
    result = {'method': 'private Xvfb XTEST', 'actions': [], 'observations': [], 'passed': False}
    sequence = 0
    start = time.monotonic()

    def remaining():
        return max(0.0, deadline - time.monotonic())

    def guest(args):
        if remaining() <= 0 or process.poll() is not None:
            raise TimeoutError('scenario deadline')
        command = ['docker', 'exec', '-e', 'DISPLAY=:99', container, *args]
        done = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              timeout=min(8, remaining()))
        if done.returncode:
            (out / 'guest-command-error.json').write_text(json.dumps({
                'command': command, 'returncode': done.returncode,
                'output': done.stdout[:65536].decode(errors='replace'),
                'output_truncated': len(done.stdout) > 65536}, indent=2) + '\n')
            done.check_returncode()
        return done

    def capture(stage):
        nonlocal sequence
        sequence += 1
        name = f'state-{sequence:03d}-{stage}'
        observed_at = time.time_ns()
        guest(['xwd', '-root', '-silent', '-out', '/work/' + name + '.xwd'])
        image = out / (name + '.png')
        subprocess.run(['ffmpeg', '-loglevel', 'error', '-threads', '1', '-y', '-i', str(out / (name + '.xwd')),
                        '-frames:v', '1', str(image)], check=True, timeout=8)
        roi = out / (name + '-ocr')
        roi.mkdir()
        labels = detector.evaluate(image, roi, deadline)
        dialog = detector.evaluate_dialog(image, roi, deadline)
        row = {'stage': stage, 'image': image.name, 'wall_time_ns': observed_at,
               'menu': labels, 'dialog': dialog, 'elapsed': time.monotonic() - start}
        result['observations'].append(row)
        return row

    def click(label, x, y, actions=None):
        # Coordinates refer exclusively to the private virtual display.
        sent_at = time.time_ns()
        origin = result['geometry']['origin']
        screen = [x + origin[0], y + origin[1]]
        location = guest(['xdotool', 'getmouselocation', '--shell']).stdout.decode()
        observed = dict(re.findall(r'^(X|Y)=(-?\d+)$', location, re.M))
        if set(observed) != {'X', 'Y'}:
            raise ValueError('private pointer location could not be observed')
        prior = [int(observed['X']), int(observed['Y'])]
        # xdotool --sync can wait forever for a motion event when already at the
        # destination. Preserve that state and still issue the real press/release.
        if prior != screen:
            guest(['xdotool', 'mousemove', '--sync', str(screen[0]), str(screen[1])])
        time.sleep(0.25)
        action = {'label': label, 'client': [x, y], 'virtual_display': ':99',
                  'pointer_before': prior, 'motion_sent': prior != screen,
                  'screen': screen, 'action_start_wall_time_ns': sent_at}
        try:
            if input_observer:
                # The API tape keeps its existing, separate input format.
                action['button_down_request_wall_time_ns'] = time.time_ns()
                guest(['xdotool', 'mousedown', '1'])
                time.sleep(0.25)
                action['button_up_request_wall_time_ns'] = time.time_ns()
                guest(['xdotool', 'mouseup', '1'])
                action['released_wall_time_ns'] = time.time_ns()
            else:
                press_observed_button(guest, remaining,
                    lambda: (out / 'game/dumps/dinput8proxy.log').read_bytes(), action)
        except Exception as error:
            action['input_error'] = str(error)
            raise
        finally:
            (result['actions'] if actions is None else actions).append(action)

    try:
        while not (out / 'game.ready').exists():
            if process.poll() is not None or remaining() <= 0:
                raise RuntimeError('game launcher ended before readiness')
            time.sleep(0.2)
        # No action is issued until the actual Main Menu and input polling are observed.
        stable = 0
        while remaining() > 0:
            row = capture('await-menu')
            log = out / 'game/dumps/dinput8proxy.log'
            text = log.read_text(errors='replace') if log.exists() else ''
            polls = [int(n) for n in re.findall(r'GetDevice(?:State|Data) calls=(\d+)', text)]
            ready = all(row['menu'].values()) and not any(row['dialog'].values()) and max(polls, default=0) > 0
            native = [int(v) for v in re.findall(r'input_poll: kind=1 offset=12 data=(\d+) source=native', text)]
            if input_observer:
                tape = out / 'game/dumps/stock-control-tape.jsonl'
                consumed = input_tape.consumption(input_tape.read_rows(tape, partial=True)) if tape.exists() else {}
                polls = [consumed.get('successful_mouse_reads', 0)]
                native = consumed.get('mouse_transitions', [])
                ready = all(row['menu'].values()) and not any(row['dialog'].values()) and max(polls) > 0
            ready = ready and native == [0]
            stable = stable + 1 if ready else 0
            if stable >= 2:
                result['before'] = row
                break
            time.sleep(0.75)
        else:
            raise TimeoutError('menu was not observed')
        guest(['/bin/bash', '-c', "printf 'where\\n' >> /work/game/dumps/input.cmd"])
        geometry_deadline = time.monotonic() + 2
        while time.monotonic() < geometry_deadline:
            text = (out / 'game/dumps/d3d9proxy.log').read_text(errors='replace')
            matches = re.findall(r'input_geometry: valid=1 hwnd=(\w+) client=\(([-\d]+),([-\d]+),([-\d]+),([-\d]+)\) origin=\(([-\d]+),([-\d]+)\)', text)
            if matches:
                hwnd, left, top, right, bottom, x, y = matches[-1]
                result['geometry'] = {'hwnd': hwnd, 'client': [int(left), int(top), int(right), int(bottom)], 'origin': [int(x), int(y)]}
                if result['geometry']['client'] != [0, 0, 800, 600]:
                    raise RuntimeError('the qualified coordinate fixture requires800x600 client')
                break
            time.sleep(.05)
        else:
            raise RuntimeError('no actual Stock client geometry observed')
        if career_flow:
            spec = importlib.util.spec_from_file_location('stock_career_flow', here / 'career_flow.py')
            career = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(career)
            career.run(career_flow, out, result, guest, capture, click, remaining, detector)
        else:
            click('New Career', 300, 120)
            while remaining() > 0:
                time.sleep(0.6)
                row = capture('await-name-dialog')
                if all(row['dialog'].values()):
                    result['name_dialog'] = row
                    break
            else:
                raise TimeoutError('name dialog did not appear after guest input')
            click('Cancel button', 480, 470)
            while remaining() > 0:
                time.sleep(0.6)
                row = capture('await-menu-return')
                if all(row['menu'].values()) and not any(row['dialog'].values()):
                    result['after'] = row
                    result['passed'] = True
                    break
            else:
                raise TimeoutError('Cancel did not return to menu')
        if ordinary_exit:
            # Freeze the completed two-action scenario before issuing new actions.
            (out / 'scenario-input.log').write_bytes((out / 'game/dumps/dinput8proxy.log').read_bytes())
            shutdown = result['shutdown'] = {'actions': [], 'passed': False}
            click('Exit Game', 480, 336, shutdown['actions'])
            while remaining() > 0:
                time.sleep(.6)
                row = capture('exit-confirmation')
                image = out / row['image']
                labels = detector.evaluate_exit(image, out / (image.stem + '-ocr'), deadline)
                if all(labels.values()):
                    break
            else:
                raise TimeoutError('unambiguous Exit/No/Yes confirmation not observed')
            x, y = 320, 470
            shutdown['confirmation'] = {'image': image.name, 'yes_center': [x, y], 'labels': labels}
            # Observe windows before they are destroyed; the process handle is
            # the independent post-confirmation exit observation.
            (out / 'windows-before-exit.txt').write_bytes(guest(['xwininfo', '-root', '-tree']).stdout)
            click('Confirm Yes', x, y, shutdown['actions'])
            while not (out / 'game-process-exit.json').exists():
                if remaining() <= 0 or process.poll() is not None:
                    raise TimeoutError('game did not exit after confirmation')
                time.sleep(.1)
            started = json.loads((out / 'game-process-start.json').read_text())
            exited = json.loads((out / 'game-process-exit.json').read_text())
            shutdown['process_start'], shutdown['process_exit'] = started, exited
            shutdown['passed'] = (started['pid'] == exited['pid'] and exited['wait_result'] == 0
                                  and exited['exit_code'] == 0)
            if not shutdown['passed']:
                raise ValueError('game process wait or exit status failed')
    except Exception as error:
        result['error'] = str(error)
        result['passed'] = False
    finally:
        result['seconds'] = time.monotonic() - start
        (out / 'flow.json').write_text(json.dumps(result, indent=2) + '\n')
        (out / 'controller.done').touch()
    return result
