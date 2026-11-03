"""Consume recorded Stock API/Native replies in an explicit owned caller harness.

This does not launch/inject Stock, assign native thread roles, set CPU/FPU state,
restore a checkpoint or schedule instructions. Caller tokens are supplied by the
owned single-threaded harness, not inferred from native TIDs. Simultaneous calls
are unsupported; this object supplies no synchronization. Shared phases are consumed exactly;
a mismatch latches failure, with no fallback to a live clock/input API.
"""
import copy
import importlib.util
import json
from pathlib import Path
import struct
import threading

_spec = importlib.util.spec_from_file_location('response_control_interval', Path(__file__).with_name('control_interval.py'))
control = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(control)


class ReplayMismatch(ValueError):
    pass


def bounded_request(value, depth=0):
    """Only bounded plain protocol data may reach the exact comparator."""
    if depth > 8:
        return False
    if type(value) is dict:
        return len(value) <= 32 and all(type(key) is str and len(key) <= 256 and
                bounded_request(item, depth+1) for key,item in value.items())
    if type(value) is list:
        return len(value) <= 32 and all(bounded_request(item, depth+1) for item in value)
    return value is None or type(value) is int or (type(value) is str and len(value) <= 256)


class RecordedProgram:
    """Validated immutable serialized program; use from_tape, never a JSON plan."""
    def __init__(self, path):
        compiled = control.compile_tape(path)
        if compiled['format'] not in ('stock-api-interval-offline/2', 'stock-api-native-return-interval-offline/2'):
            raise ReplayMismatch('only phase-qualified API/stored-Native streams are implemented; RNG/input/callback streams refuse')
        if any(call['request']['api'] not in control.CALLERS for call in compiled['calls']):
            raise ReplayMismatch('unsupported response stream')
        self._encoded = json.dumps(compiled, sort_keys=True, separators=(',', ':')).encode()

    @classmethod
    def from_tape(cls, path):
        return cls(path)

    def describe(self):
        value = json.loads(self._encoded)
        result = {'source_sha256': value['source_sha256'], 'calls': len(value['calls']),
                'phases': len(value['phase_order']),
                'capture_lanes': sorted({phase['lane'] for phase in value['phase_order']}),
                'scope': 'owned_harness_API_response_consumption', 'process_replay_qualified': False}
        if 'native_returns' in value:
            result.update(native_returns=len(value['native_returns']),
                          scope='owned_harness_API_Native_response_consumption')
        return result

    def new_session(self, bindings):
        return ResponseSession(self, bindings)


class ResponseSession:
    def __init__(self, program, bindings):
        if type(program) is not RecordedProgram or type(bindings) is not dict:
            raise ReplayMismatch('validated program and explicit harness bindings required')
        value = json.loads(program._encoded)
        lanes = {phase['lane'] for phase in value['phase_order']}
        if (not bindings or any(type(token) is not str or not token for token in bindings) or
                any(type(lane) is not int for lane in bindings.values()) or
                len(set(bindings.values())) != len(bindings) or set(bindings.values()) != lanes):
            raise ReplayMismatch('bindings must cover each captured lane exactly once; native role inference is unsupported')
        self._bindings = dict(bindings)
        self._calls = {call['entry']: call for call in value['calls']}
        self._timeline = value['phase_order']
        self._source = value['source_sha256']
        self._native = {}
        for row in value.get('native_returns', []):
            request = {key: row[key] for key in ('native', 'caller', 'callee', 'ordinal', 'phase', 'lane_read')}
            request.update(source_sha256=self._source, capture_lane=row['lane'])
            self._native[row['ordinal']] = {
                'request': request,
                'reply': {key: row[key] for key in ('value_f32_bits', 'last_error', 'fxsave_hex')}}
        self._scope = ('owned_harness_API_Native_response_consumption' if self._native else
                       'owned_harness_API_response_consumption')
        self._index = 0
        self._active = {}
        self._returned = 0
        self._native_returned = 0
        self._failure = None
        self._owner_thread = threading.current_thread()

    def _refuse(self, reason):
        if self._failure is None:
            self._failure = reason
        raise ReplayMismatch(self._failure)

    def _next(self, token, phase):
        if threading.current_thread() is not self._owner_thread:
            self._refuse('unsupported simultaneous or foreign harness thread')
        if self._failure is not None:
            raise ReplayMismatch('replay already failed: ' + self._failure)
        if type(token) is not str or token not in self._bindings:
            self._refuse('unknown harness thread token')
        if self._index == len(self._timeline):
            self._refuse('recorded stream exhausted')
        expected = self._timeline[self._index]
        if expected['phase'] != phase or expected['lane'] != self._bindings[token]:
            self._refuse('shared phase/lane mismatch at ordinal ' + str(expected['ordinal']))
        return expected

    def enter(self, token, request):
        """Admit one exact normalized caller request; returns its opaque call id."""
        expected = self._next(token, 'entry')
        call = self._calls[expected['call']]
        if type(request) is not dict or not bounded_request(request):
            self._refuse('malformed or unbounded caller request')
        if control.first_difference(call['request'], request):
            self._refuse('recorded caller request mismatch')
        # A token can nest only where the actual phase tape demonstrates nesting.
        self._active.setdefault(token, []).append(call['entry'])
        self._index += 1
        return call['entry']

    def leave(self, token, call_id, output=None):
        """Apply recorded output bytes and return recorded machine-state words.

        The owned caller must apply returned EAX/LastError/CW/MXCSR at its own
        boundary; returning them in Python is not native ABI qualification.
        """
        expected = self._next(token, 'return')
        stack = self._active.get(token, [])
        if type(call_id) is not int or not stack or stack[-1] != call_id or expected['call'] != call_id:
            self._refuse('recorded return/nesting mismatch')
        reply = self._calls[call_id]['reply']
        words = reply['output_u32']
        if words is None:
            if output is not None:
                self._refuse('scalar response supplied an output buffer')
        elif type(output) is not bytearray or len(output) < 8:
            self._refuse('recorded eight-byte output requires owned writable buffer')
        # Validate the whole return before publishing any bytes or advancing.
        if words is not None:
            output[:8] = struct.pack('<II', *words)
        stack.pop()
        self._index += 1
        self._returned += 1
        return copy.deepcopy(reply)

    def native_return(self, token, request):
        """Consume one original stored Float32 observation, not a Native entry.

        Shared API phases must be consumed explicitly before this operation.
        The reply's opaque backend FX bytes are retained, never applied or
        interpreted as architectural state by this owned Python harness.
        """
        expected = self._next(token, 'native_return')
        observed = self._native[expected['native_return']]
        if type(request) is not dict or not bounded_request(request):
            self._refuse('malformed or unbounded Native request')
        if control.first_difference(observed['request'], request):
            self._refuse('recorded Native request mismatch at ordinal ' + str(expected['ordinal']))
        self._index += 1
        self._native_returned += 1
        return copy.deepcopy(observed['reply'])

    def finish(self):
        if threading.current_thread() is not self._owner_thread:
            self._refuse('unsupported foreign harness completion thread')
        if self._failure is not None:
            raise ReplayMismatch('replay already failed: ' + self._failure)
        if self._index != len(self._timeline) or any(self._active.values()):
            self._refuse('recorded responses remain unconsumed')
        result = {'scope': self._scope, 'source_sha256': self._source,
                'consumed_calls': self._returned, 'consumed_phases': self._index,
                'pending': 0, 'process_replay_qualified': False,
                'native_lane_binding_qualified': False, 'scheduler_replay_qualified': False}
        if self._native:
            result['consumed_native_returns'] = self._native_returned
        return result
