#!/usr/bin/env python3
"""Offline interval compiler/comparator. Never injects into or launches Stock.

Capture lanes remain unresolved identities: this does NOT establish process
replay, scheduler control or a complete initial snapshot.
"""
import argparse
import collections
import hashlib
import json
import os
from pathlib import Path
import stat

MAX_BYTES = 128 * 1024**2
MAX_RECORDS = 500000
MAX_LINE_BYTES = 16384
REFERENCE = Path(__file__).resolve().parents[2] / 'ref'
STOCK_SHA256 = '7d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde'

INITIAL = {
    "engine_frequency": (0x36a280, 8),
    "engine_qpc_epoch": (0x36a288, 8),
    "engine_last_counter": (0x36a290, 8),
    "stopwatch_frequency": (0x36a298, 8),
    "stopwatch_slots_0_to_14": (0x36a3a0, 120),
    "input_frequency": (0x36f9a8, 8),
    "input_previous_counter": (0x377440, 8),
    "frame_delta_float_bits": (0x23c534, 4),
}
CALLERS = {
    "QueryPerformanceFrequency": {0x15169b, 0x1516cb, 0x15231b, 0x156531, 0x156bcf},
    "QueryPerformanceCounter": {0x1516a6, 0x1516d6, 0x15232d, 0x15233e, 0x15653c, 0x156bda},
    "GetTickCount": {0x154eb5, 0x150c46, 0x155278, 0x1552bc, 0x1d4da1},
    "GetAsyncKeyState": {0x15c592, 0x15c5f4},
    "timeGetTime": {0x1d150d, 0x1d17c1, 0x1d17d9},
}
# Format 5 only: exact direct-CALL return sites of the guarded CRT entries
# (005d73fb srand, 005d7408 rand). Other callers must remain gaps. The draw list is the
# 55-site linear census in local/verify/stock-live-rng-capture/direct-rand-calls.json;
# the two seed sites are the wrappers at 0047c7d0/0047c7f0. Not an indirect-caller proof.
RNG_CALLERS = {
    "CRT_srand_after_original_TLS_getter": {0x7c7e0, 0x7c814},
    "CRT_rand_after_original_TLS_getter": {
        0x36148, 0x51339, 0x534c9, 0x54911, 0x5afe4, 0x5bf21, 0x5f27c, 0x5f28a, 0x5f2c4, 0x5f358,
        0x6610a, 0x6613a, 0x67995, 0x679bf, 0x679fd, 0x7c826, 0x8282b, 0x8424c, 0x842c1, 0x842e7,
        0x84314, 0x8435e, 0x845e4, 0x8460a, 0x84637, 0x8467a, 0x84874, 0x8490e, 0x849d3, 0x84a04,
        0x84a2f, 0x1203da, 0x1204cc, 0x1204e8, 0x1207dc, 0x1207f8, 0x12094d, 0x120969, 0x120985,
        0x15bf6a, 0x177154, 0x17a9f0, 0x17b454, 0x17b5cd, 0x17f746, 0x181e6c, 0x181ed6, 0x181f55,
        0x1823d7, 0x182404, 0x1876bd, 0x189595, 0x1895b1, 0x1895cd, 0x189808},
}
RNG_INSTALLATION = {
    'event': 'installation', 'boundary': 'dll_process_attach_main_iat',
    'identity': 'late_full_file_sha256_verified', 'initial_observations': 'not_restorable_checkpoint',
    'file_sha256': STOCK_SHA256, 'scope': 'stock_main_image_api_rng_interval',
    'rng_boundary': 'after_original_TLS_getter',
    'code_mutations': [{'rva': 0x1d7400, 'bytes': 7}, {'rva': 0x1d740d, 'bytes': 9}],
}
RNG_FIELDS = {"rng_tls_raw", "rng_before", "rng_after", "eflags"}
FIELDS = set("event gap_reason seq return_seq thread lane lane_read role api caller caller_rva arg result last_error_before last_error_after output_valid output_u32 cw mxcsr".split())
STATUS = set("event format_version entered returned active pending dropped gaps accepting mode early_startup".split())
GLOBALS = set("event name image_base rva size valid words".split())
NATIVE_INSTALLATION = {
    **{key: value for key, value in RNG_INSTALLATION.items()
       if key not in ('rng_boundary', 'code_mutations', 'scope')},
    'scope': 'stock_main_image_api_native_clock_returns',
    'native_boundary': 'after_original_currentTime_fmul_before_ret',
    'code_mutations': [{'rva': 0x7bfeb, 'bytes': 5}],
}
NATIVE_FIELDS = set('event native ordinal phase thread lane lane_read role caller last_error fxsave_hex'.split())
STORED_NATIVE_INSTALLATION = {
    **NATIVE_INSTALLATION,
    'native_boundary': 'after_original_native_bridge_fstp32',
    'code_mutations': [{'rva': 0x20094, 'bytes': 7}],
}
STORED_NATIVE_FIELDS = NATIVE_FIELDS | {'callee', 'value_f32_bits'}


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def phase_timeline(events, final, native_returns=()):
    count = len(events)
    require(final.get("format_version") in (3, 4, 5, 8, 9), "phase order requires format 3, 4, 5, 8 or 9")
    phases = final.get("phases")
    require(type(phases) is int and phases == 2 * count + len(native_returns) and 0 < phases <= 0x7fffffff,
            "incomplete/exhausted phase count")
    timeline = []
    for event in native_returns:
        timeline.append({'ordinal': event['phase'], 'phase': 'native_return',
                         'native_return': event['ordinal'], 'lane': event['lane']})
    for event in events:
        entry, returned = event.get("entry_phase"), event.get("return_phase")
        require(type(entry) is int and type(returned) is int and 0 < entry < returned <= phases,
                "missing/invalid/reversed call phase")
        timeline.append({"ordinal": entry, "phase": "entry", "call": event["seq"], "lane": event["lane"]})
        timeline.append({"ordinal": returned, "phase": "return", "call": event["seq"], "lane": event["lane"]})
    timeline.sort(key=lambda phase: phase["ordinal"])
    require([phase["ordinal"] for phase in timeline] == list(range(1, phases + 1)),
            "phase ordinal gap/duplicate")
    stacks = {}
    for phase in timeline:
        stack = stacks.setdefault(phase["lane"], [])
        if phase["phase"] == "entry":
            stack.append(phase["call"])
        elif phase['phase'] == 'return':
            require(stack and stack.pop() == phase["call"], "per-lane phase nesting mismatch")
    require(all(not stack for stack in stacks.values()), "unclosed phase call")
    return timeline


def uint(value, bits=32):
    require(type(value) is int and 0 <= value < (1 << bits), f"invalid uint{bits}: {value!r}")
    return value


def unique_object(pairs):
    obj = {}
    for key, value in pairs:
        require(key not in obj, f"duplicate JSON key: {key}")
        obj[key] = value
    return obj


def pointer(api, caller, arg, base):
    if api == "GetAsyncKeyState":
        require(arg <= 255, "virtual key outside supported range")
        return {"virtual_key": arg}
    if api not in ("QueryPerformanceCounter", "QueryPerformanceFrequency"):
        require(arg == 0, "argument on zero-argument API")
        return None
    globals_by_site = {
        0x15169b: 0x36a280, 0x1516cb: 0x36a280, 0x15231b: 0x36a298,
        0x156531: 0x36f9a8, 0x156bcf: 0x36f9a8,
        0x1516a6: 0x36a288, 0x1516d6: 0x36a290, 0x15653c: 0x377440,
    }
    if caller in globals_by_site:
        rva = globals_by_site[caller]
        require(arg == base + rva, "wrong global output pointer")
        return {"global_rva": rva, "bytes": 8}
    if caller == 0x15232d:
        offset = arg - base - 0x36a3a0
        require(offset >= 0 and offset % 8 == 0 and offset // 8 in (0, 1, 2, 3, 4, 7, 8, 11, 14), "wrong stopwatch slot")
        return {"stopwatch_slot": offset // 8, "bytes": 8}
    require(caller in (0x15233e, 0x156bda) and 0x10000 <= arg <= 0xfffffff8 and arg % 4 == 0,
            "unsupported local output pointer")
    # Recorder already checks its live stack address. This offline file cannot
    # independently reconstruct the old TEB/stack, and never claims to.
    return {"stack_local": "stopwatch" if caller == 0x15233e else "input", "bytes": 8}


def parity8(value):
    return int(bin(value & 0xff).count("1") % 2 == 0)


def check_rng_chain(rng_events):
    """State continuity per CRT block, in true call (seq) order, not file order."""
    state = {}
    for row in sorted(rng_events, key=lambda r: r["seq"]):
        tls = row["rng_tls_raw"]
        if tls in state:
            require(state[tls] == row["rng_before"], "unobserved CRT state mutation between RNG calls")
        state[tls] = row["rng_after"]


def check_rng(row, blocks):
    """Arithmetic/flag relations of the exact guarded Stock CRT tails.

    seed: MOV ECX,[ESP+4]; MOV [EAX+14],ECX; RET  -> state=arg, EAX/flags unchanged.
    rand: state=state*0x343fd+0x269ec3; EAX=(state>>16)&0x7fff via AND (CF/OF/SF=0).
    Raw EAX/pointers/flags are retained; these checks only reject impossible records.
    """
    api, tls = row["api"], row["rng_tls_raw"]
    before, after, flags = row["rng_before"], row["rng_after"], row["eflags"]
    require(tls >= 0x10000 and tls % 4 == 0, "implausible CRT block pointer")
    require(row["output_valid"] == 0 and row["output_u32"] == [0, 0], "RNG record claims API output")
    require(blocks.setdefault(row["lane"], tls) == tls, "CRT block changed within a lane")
    require(blocks.setdefault(("block", tls), row["lane"]) == row["lane"], "CRT block shared across lanes")
    # The patched tails touch no LastError, x87 control word or MXCSR, and only arithmetic EFLAGS.
    require(row["last_error_before"] == row["last_error_after"] and row["cw"][0] == row["cw"][1]
            and row["mxcsr"][0] == row["mxcsr"][1], "RNG record perturbed LastError/x87/MXCSR state")
    require((flags[0] ^ flags[1]) & ~0x8d5 == 0, "RNG record changed non-arithmetic EFLAGS bits")
    if api == "CRT_srand_after_original_TLS_getter":
        require(after == row["arg"] and row["result"] == tls and flags[1] == flags[0],
                "srand record contradicts the guarded tail")
    else:
        require(row["arg"] == 0 and after == (before * 0x343fd + 0x269ec3) & 0xffffffff
                and row["result"] == (after >> 16) & 0x7fff, "rand record contradicts the guarded LCG tail")
        expected = (parity8(row["result"]) << 2) | ((row["result"] == 0) << 6)
        require(flags[1] & 0x8c5 == expected, "rand EFLAGS contradict AND EAX,7FFF")


def read_rows(path, max_bytes, max_records):
    require(type(max_bytes) is int and max_bytes > 0 and type(max_records) is int and max_records > 0,
            'capture limits must be positive integers')
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    with os.fdopen(fd, 'rb') as stream:
        before = os.fstat(stream.fileno())
        require(stat.S_ISREG(before.st_mode) and before.st_size <= max_bytes, 'capture type/size limit')
        digest, rows, used = hashlib.sha256(), [], 0
        for line in iter(lambda: stream.readline(MAX_LINE_BYTES + 1), b''):
            used += len(line)
            require(len(line) <= MAX_LINE_BYTES and line.endswith(b'\n'), 'overlong/truncated capture line')
            require(used <= max_bytes and len(rows) < max_records, 'capture size/record limit')
            digest.update(line)
            rows.append(json.loads(line, object_pairs_hook=unique_object,
                                   parse_constant=lambda value: require(False, 'nonfinite JSON number')))
        after = os.fstat(stream.fileno())
    signature = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns)
    require(signature(before) == signature(after) == signature(Path(path).stat()) and used == before.st_size,
            'capture changed during inspection')
    return rows, digest.hexdigest()


def compile_tape(path, max_bytes=MAX_BYTES, max_records=MAX_RECORDS):
    try:
        rows, source_digest = read_rows(path, max_bytes, max_records)
        return compile_rows(rows, source_digest)
    except (KeyError, TypeError, RecursionError) as error:
        raise ValueError(f'malformed capture: {error}') from error


def compile_rows(rows, source_digest):
    require(all(type(row) is dict for row in rows), "record must be a JSON object")
    versions = {row.get("format_version") for row in rows if row.get("event") == "status"}
    require(len(versions) == 1 and next(iter(versions)) in (2, 3, 4, 5, 8, 9), "unsupported/mixed capture versions")
    version = next(iter(versions))
    call_fields = FIELDS | ({"entry_phase", "return_phase"} if version >= 3 else set())
    rng_fields = call_fields | RNG_FIELDS
    rng_blocks = {}
    status_fields = STATUS | ({"phases"} if version >= 3 else set())
    initial = {}
    events = []
    statuses = []
    base = None
    last_was_status = False
    installation = None
    native_returns, native_statuses = [], []
    for row in rows:
        kind = row.get("event")
        last_was_status = kind == "status"
        if kind == 'installation':
            require(version in (4, 5, 8, 9) and row is rows[0] and installation is None, 'unexpected/duplicate installation')
            require(row == ({'event': 'installation', 'boundary': 'dll_process_attach_main_iat',
                             'identity': 'late_full_file_sha256_verified',
                             'initial_observations': 'not_restorable_checkpoint',
                             'file_sha256': STOCK_SHA256, 'scope': 'stock_main_image_api_interval'}
                            if version == 4 else (RNG_INSTALLATION if version == 5 else
                                                 (NATIVE_INSTALLATION if version == 8 else STORED_NATIVE_INSTALLATION))),
                    'unsupported/unverified early installation')
            installation = row
        elif kind == "initial_global":
            require(set(row) == GLOBALS, "unknown/missing global fields")
            name = row["name"]
            require(name in INITIAL and name not in initial and not events and not statuses, "duplicate/late/unknown initial global")
            for key in ("rva", "size", "valid"):
                uint(row[key])
            require((row["rva"], row["size"]) == INITIAL[name] and row["valid"] == 1, "invalid initial global")
            current = uint(row["image_base"])
            require(version < 4 or current == 0x400000, 'early descriptor does not support relocated main image')
            require(base in (None, current), "mixed image bases")
            base = current
            require(type(row["words"]) is list and len(row["words"]) * 4 == row["size"], "wrong global size")
            initial[name] = [uint(word) for word in row["words"]]
        elif kind == "api_return":
            require(set(row) == call_fields and base is not None, "unknown/missing call fields")
            require(row["api"] in CALLERS and row["caller_rva"] in CALLERS[row["api"]], "unsupported API/callsite")
            require(row["gap_reason"] == 0 and row["role"] == "unresolved", "gap or unexpected lane-role declaration")
            for key in call_fields - {"event", "role", "api", "output_u32", "cw", "mxcsr"}:
                uint(row[key])
            require(row["caller"] == base + row["caller_rva"], "inconsistent caller address")
            require(type(row["output_u32"]) is list and len(row["output_u32"]) == 2, "wrong output shape")
            require(type(row["cw"]) is list and len(row["cw"]) == 2, "wrong x87 shape")
            require(type(row["mxcsr"]) is list and len(row["mxcsr"]) == 2, "wrong SSE shape")
            for word in row["output_u32"] + row["mxcsr"]:
                uint(word)
            for word in row["cw"]:
                uint(word, 16)
            output_api = row["api"] in ("QueryPerformanceCounter", "QueryPerformanceFrequency")
            valid = int(output_api and row["result"] != 0)
            require(row["output_valid"] == valid, "inconsistent output validity")
            require(valid or row["output_u32"] == [0, 0], "output bytes on invalid/non-output call")
            events.append(row)
        elif kind == "rng_return":
            require(version == 5 and set(row) == rng_fields and base is not None, "unknown/missing RNG fields")
            require(row["api"] in RNG_CALLERS and row["caller_rva"] in RNG_CALLERS[row["api"]], "unsupported RNG callsite")
            require(row["gap_reason"] == 0 and row["role"] == "unresolved", "gap or unexpected lane-role declaration")
            for key in rng_fields - {"event", "role", "api", "output_u32", "cw", "mxcsr", "eflags"}:
                uint(row[key])
            require(row["caller"] == base + row["caller_rva"], "inconsistent caller address")
            for key, count, bits in (("output_u32", 2, 32), ("cw", 2, 16), ("mxcsr", 2, 32), ("eflags", 2, 32)):
                require(type(row[key]) is list and len(row[key]) == count, f"wrong {key} shape")
                for word in row[key]:
                    uint(word, bits)
            # The inspected getter initializes +0 with the numeric thread id.
            check_rng(row, rng_blocks)
            events.append(row)
        elif kind == 'native_clock_return':
            native_fields = STORED_NATIVE_FIELDS if version == 9 else NATIVE_FIELDS
            require(version in (8, 9) and set(row) == native_fields and base is not None,
                    'unknown/missing Native return fields')
            require(row['native'] == 'java.lang.System.currentTime()F' and row['role'] == 'unresolved',
                    'unsupported Native/role')
            for key in native_fields - {'event', 'native', 'role', 'fxsave_hex'}:
                uint(row[key])
            raw = row['fxsave_hex']
            require(type(raw) is str and len(raw) == 1024 and all(c in '0123456789abcdef' for c in raw),
                    'invalid FXSAVE return bytes')
            if version == 8:
                fx = bytes.fromhex(raw)
                # Historical format8 requires architectural FXSAVE. Actual
                # backend snapshots that fail this remain refused, not decoded
                # as invented80-bit values. Format9 observes the original store.
                top = (int.from_bytes(fx[2:4], 'little') >> 11) & 7
                require(fx[4] & (1 << top), 'Native return has empty ST0')
            else:
                require(row['caller'] == base + 0x20090 and row['callee'] == base + 0x7bfe0,
                        'wrong original Native Float32 store/callee binding')
                require(row['value_f32_bits'] & 0x7f800000 != 0x7f800000,
                        'nonfinite actual clock result is an unsupported observation')
            require(base <= row['caller'] < base + 0x3c0000, 'Native caller outside pinned image')
            native_returns.append(row)
        elif kind == 'native_clock_status':
            require(version in (8, 9) and set(row) == {'event', 'returns', 'phase_source', 'scope'} and
                    row['phase_source'] == 'shared_next_phase' and row['scope'] == 'return_only_not_replay',
                    'unsupported Native status')
            uint(row['returns']); native_statuses.append(row)
        elif kind == "status":
            require(set(row) == status_fields and row["format_version"] == version and row["mode"] == "record_only" and row["early_startup"] == ('main_iat_from_proxy_attach' if version >= 4 else 'unobserved'), "unsupported status/schema")
            for key in status_fields - {"event", "mode", "early_startup"}:
                uint(row[key])
            require(row["dropped"] == row["gaps"] == 0, "recording reported loss")
            statuses.append(row)
        else:
            raise ValueError(f"unknown/gap record: {kind!r}")
    require(last_was_status and statuses and set(initial) == set(INITIAL), "incomplete envelope")
    require(version < 4 or installation is not None, 'missing early installation provenance')
    final = statuses[-1]
    if version in (8, 9):
        native_returns.sort(key=lambda event: event['ordinal'])
        require(native_returns and native_statuses and len(native_statuses) == len(statuses),
                'missing Native returns/status')
        require([event['ordinal'] for event in native_returns] == list(range(1, len(native_returns) + 1)) and
                native_statuses[-1]['returns'] == len(native_returns), 'incomplete Native return interval')
    require(final["active"] == final["pending"] == final["accepting"] == 0, "capture not stopped")
    events.sort(key=lambda e: e["seq"])
    n = len(events)
    require(n > 0 and final["entered"] == final["returned"] == n, "incomplete call interval")
    require([e["seq"] for e in events] == list(range(1, n + 1)), "entry ordinal gap/duplicate")
    require(sorted(e["return_seq"] for e in events) == list(range(1, n + 1)), "return ordinal gap/duplicate")
    rng_events = [e for e in events if e["event"] == "rng_return"]
    if version == 5:
        # The installation row declares two code mutations; a tape with no observation through them
        # (e.g. a stale translation cache) is a missing observation, never a pass.
        require(rng_events, "format 5 declares RNG code mutations but recorded no RNG call")
        check_rng_chain(rng_events)
    lanes, threads, reads = {}, {}, collections.Counter()
    if version in (8, 9):
        observations = [(e['entry_phase'], e) for e in events] + [(e['phase'], e) for e in native_returns]
        for _, e in sorted(observations, key=lambda item: item[0]):
            lane, thread = e['lane'], e['thread']
            require(lane < 128 and thread != 0, 'invalid lane/thread')
            require(lanes.setdefault(lane, thread) == thread and threads.setdefault(thread, lane) == lane,
                    'lane/thread identity changed')
            reads[lane] += 1
            require(e['lane_read'] == reads[lane], 'mixed per-lane observation order mismatch')
    previous_return, previous_entry = {}, {}
    compiled = []
    for e in events:
        lane, thread = e["lane"], e["thread"]
        require(lane < 128 and thread != 0, "invalid lane/thread")
        require(lanes.setdefault(lane, thread) == thread and threads.setdefault(thread, lane) == lane, "lane/thread identity changed")
        if version not in (8, 9):
            reads[lane] += 1
            require(e["lane_read"] == reads[lane], "per-lane entry order mismatch")
        # Hooks are not themselves reentrant in this experiment. A same-thread
        # return order inversion is refused rather than assumed replayable.
        require(e["return_seq"] > previous_return.get(lane, 0), "same-lane return inversion requires nesting evidence")
        previous_return[lane] = e["return_seq"]
        if version >= 3:
            require(e["entry_phase"] > previous_entry.get(lane, 0), "lane seq order contradicts phase order")
            previous_entry[lane] = e["entry_phase"]
        if e["event"] == "rng_return":
            # Raw CRT diagnostics are retained; no consumed-value projection is made here.
            compiled.append({
                "entry": e["seq"], "return": e["return_seq"],
                "request": {"capture_lane": lane, "lane_read": e["lane_read"], "api": e["api"], "caller_rva": e["caller_rva"],
                            "argument": {"seed": e["arg"]} if e["api"].startswith("CRT_srand") else None,
                            "last_error": e["last_error_before"], "cw": e["cw"][0], "mxcsr": e["mxcsr"][0],
                            "crt_block_raw": e["rng_tls_raw"], "state_before": e["rng_before"], "eflags": e["eflags"][0]},
                "reply": {"eax": e["result"], "last_error": e["last_error_after"], "state_after": e["rng_after"],
                          "eflags": e["eflags"][1], "output_u32": None, "cw": e["cw"][1], "mxcsr": e["mxcsr"][1]},
            })
            continue
        compiled.append({
            "entry": e["seq"], "return": e["return_seq"],
            "request": {"capture_lane": lane, "lane_read": e["lane_read"], "api": e["api"], "caller_rva": e["caller_rva"], "argument": pointer(e["api"], e["caller_rva"], e["arg"], base), "last_error": e["last_error_before"], "cw": e["cw"][0], "mxcsr": e["mxcsr"][0]},
            "reply": {"eax": e["result"], "last_error": e["last_error_after"], "output_u32": e["output_u32"] if e["output_valid"] else None, "cw": e["cw"][1], "mxcsr": e["mxcsr"][1]},
        })
    result = {"format": "stock-api-interval-offline/1", "source_sha256": source_digest, "scope": "post-install observed main-image APIs only", "process_replay_qualified": False, "lane_roles": "unresolved; numeric lanes require a future explicit binding", "initial_observations": initial, "calls": compiled}
    if version >= 3:
        result["format"] = "stock-api-interval-offline/2"
        if version == 5:
            result["format"] = "stock-api-interval-offline/3"
            result["rng_calls"] = sum(1 for e in events if e["event"] == "rng_return")
            result["rng_scope"] = "guarded CRT srand/rand tails after the original TLS getter; raw diagnostics, not a replay"
        result["phase_order"] = phase_timeline(events, final, native_returns)
        if version in (8, 9):
            result['format'] = 'stock-api-native-return-interval-offline/' + ('2' if version == 9 else '1')
            result['native_returns'] = native_returns
            result['scope'] = ('observed APIs plus original stored currentTime Float32; opaque backend FX; no process replay'
                               if version == 9 else 'observed APIs plus return-only currentTime FXSAVE; no process replay')
    if installation is not None:
        result['installation'] = installation
    return result


def first_difference(expected, observed, path="$"):
    if type(expected) is not type(observed):
        return {"path": path, "expected": expected, "observed": observed}
    if isinstance(expected, dict):
        if expected.keys() != observed.keys():
            return {"path": path + ".keys", "expected": sorted(expected), "observed": sorted(observed)}
        for key in expected:
            found = first_difference(expected[key], observed[key], path + "." + key)
            if found:
                return found
    elif isinstance(expected, list):
        for i, (a, b) in enumerate(zip(expected, observed)):
            found = first_difference(a, b, f"{path}[{i}]")
            if found:
                return found
        if len(expected) != len(observed):
            return {"path": path + ".length", "expected": len(expected), "observed": len(observed)}
    elif expected != observed:
        return {"path": path, "expected": expected, "observed": observed}
    return None


def compare(expected, observed):
    # Provenance hashes intentionally differ across captures, but are retained.
    a, b = dict(expected), dict(observed)
    a.pop("source_sha256")
    b.pop("source_sha256")
    mismatch = first_difference(a, b)
    return {"offline_match": mismatch is None, "process_replay_qualified": False, "first_divergence": mismatch,
            'expected_sha256': expected['source_sha256'], 'observed_sha256': observed['source_sha256']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tape", type=Path)
    parser.add_argument("--compare", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        if args.output:
            output, reference = args.output.resolve(), REFERENCE.resolve()
            require(output != reference and reference not in output.parents, 'output inside Reference Folder')
            require(output not in [p.resolve() for p in (args.tape, args.compare) if p], 'output overlaps input')
            require(not output.exists(), 'output must be new')
        result = compile_tape(args.tape)
        summary = {'status': 'validated_interval', 'format': result['format'],
                   'calls': len(result['calls']), 'phase_order_observed': 'phase_order' in result,
                   'source_sha256': result['source_sha256'], 'process_replay_qualified': False}
        if args.compare:
            result = compare(result, compile_tape(args.compare))
            summary = result
        if args.output:
            with args.output.open('x') as stream:
                stream.write(json.dumps(result, indent=2, allow_nan=False) + '\n')
        print(json.dumps(summary, allow_nan=False))
        return 0 if result.get('offline_match', True) else 1
    except (OSError, ValueError) as error:
        print(json.dumps({'status': 'inconclusive', 'reason': str(error), 'process_replay_qualified': False}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
