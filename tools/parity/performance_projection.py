"""Observed engine-clock subset -> owned Port currentTime fixture operands.

The initial words are separate observed globals, not a restorable checkpoint.
Stock arithmetic is modeled at the observed x87 precision/rounding mode. No
Stock native return float is captured here, so this is not a paired proof.
"""
import argparse
from fractions import Fraction
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

HERE=Path(__file__).resolve().parent

def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path);value=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value);return value

replay=module('projection_replay',HERE/'response_replay.py')
input_math=module('projection_input_math',HERE/'input_interval.py')


def require(value,reason):
    if not value:raise ValueError(reason)


def signed_words(words):
    require(type(words) is list and len(words)==2 and all(type(v) is int and 0<=v<=0xffffffff for v in words),'invalid counter words')
    value=words[0] | words[1]<<32
    return value-(1<<64) if value>>63 else value


def constants(stock):
    raw=Path(stock).read_bytes()
    require(hashlib.sha256(raw).hexdigest()==replay.control.STOCK_SHA256,'wrong Stock arithmetic identity')
    pe=struct.unpack_from('<I',raw,60)[0];count=struct.unpack_from('<H',raw,pe+6)[0];optional=struct.unpack_from('<H',raw,pe+20)[0]
    found={}
    for rva,expected in [(0x1f0910,0x447a0000),(0x1f10a4,0x3a83126f)]:
        for index in range(count):
            offset=pe+24+optional+index*40
            _,base,size,pointer=struct.unpack_from('<IIII',raw,offset+8)
            if base<=rva<base+size:
                value=struct.unpack_from('<I',raw,pointer+rva-base)[0]
                require(value==expected,'Stock clock scale bytes differ');found[hex(rva)]=hex(value);break
    require(len(found)==2,'missing Stock scale section')
    return found


class EngineClockProjection:
    def __init__(self,initial):
        self.frequency=signed_words(initial['engine_frequency'])
        self.epoch=signed_words(initial['engine_qpc_epoch'])
        self.pending=set()

    def reply(self,request,reply):
        api,site=request['api'],request['caller_rva']
        pairs={('QueryPerformanceFrequency',0x15169b),('QueryPerformanceCounter',0x1516a6),
               ('QueryPerformanceFrequency',0x1516cb),('QueryPerformanceCounter',0x1516d6)}
        if site not in {site for _,site in pairs}:return None
        require((api,site) in pairs,'impossible engine API/callsite pair')
        require(reply['eax']!=0 and reply['output_u32'] is not None,'failed engine counter response')
        value=signed_words(reply['output_u32'])
        lane=request['capture_lane']
        if api=='QueryPerformanceFrequency':
            require(value>0,'nonpositive engine frequency');self.frequency=value
            if site==0x1516cb:
                require(lane not in self.pending,'overlapping engine frequency read');self.pending.add(lane)
            return None
        if site==0x1516a6:self.epoch=value;return None
        require(lane in self.pending and self.frequency>0,'counter without observed same-lane frequency response')
        self.pending.remove(lane)
        cw=request['cw']
        require(cw==reply['cw'] and cw&63==63 and (cw>>10)&3==0,'unsupported changing/unmasked/directed x87 clock state')
        bits={0:24,2:53,3:64}.get((cw>>8)&3)
        require(bits is not None,'reserved x87 precision')
        rounded=lambda number:input_math.round_to_precision(number,bits)
        delta=rounded(Fraction(value)-Fraction(self.epoch))
        milliseconds=rounded(rounded(delta/self.frequency)*1000)
        scale=Fraction.from_float(struct.unpack('<f',struct.pack('<I',0x3a83126f))[0])
        seconds=input_math.round_to_precision(rounded(milliseconds*scale),24)
        expected=struct.unpack('<I',struct.pack('<f',float(seconds)))[0]
        return float(milliseconds),expected


def project(capture,stock,fixture,report):
    scales=constants(stock)
    rows,digest=replay.control.read_rows(capture,128*1024**2,500000)
    initial={row['name']:row['words'] for row in rows if row['event']=='initial_global'}
    base=next(row['image_base'] for row in rows if row['event']=='initial_global')
    calls={row['seq']:row for row in rows if row['event']=='api_return'}
    timeline=sorted((phase,tag,row['seq']) for row in calls.values() for tag,phase in [('entry',row['entry_phase']),('return',row['return_phase'])])
    bindings={f'owned_projection_token_{i}':lane for i,lane in enumerate(sorted({r['lane'] for r in calls.values()}))}
    tokens={lane:token for token,lane in bindings.items()}
    session=replay.RecordedProgram.from_tape(capture).new_session(bindings)
    projection=EngineClockProjection(initial);handles={};requests={};samples=[]
    for _,phase,identifier in timeline:
        row=calls[identifier];token=tokens[row['lane']]
        if phase=='entry':
            request=dict(capture_lane=row['lane'],lane_read=row['lane_read'],api=row['api'],caller_rva=row['caller_rva'],
                         argument=replay.control.pointer(row['api'],row['caller_rva'],row['arg'],base),
                         last_error=row['last_error_before'],cw=row['cw'][0],mxcsr=row['mxcsr'][0])
            requests[identifier]=request;handles[identifier]=session.enter(token,request)
        else:
            reply=session.leave(token,handles.pop(identifier),bytearray(8) if row['output_valid'] else None)
            sample=projection.reply(requests.pop(identifier),reply)
            if sample is not None:samples.append((*sample,identifier))
    summary=session.finish()
    require(samples and not projection.pending,'missing or unfinished engine-clock samples')
    data=b'SLRCLK1\n'+struct.pack('<I',len(samples))+b''.join(struct.pack('<dII',*sample) for sample in samples)
    with Path(fixture).open('xb') as stream:stream.write(data)
    result=dict(scope='observed engine-clock subset -> owned Port currentTime operand fixture',source_sha256=digest,
                stock_sha256=replay.control.STOCK_SHA256,scale_constants=scales,initial_observations=initial,
                consumed_api_replies=summary['consumed_calls'],consumed_phases=summary['consumed_phases'],
                projected_samples=len(samples),fixture_sha256=hashlib.sha256(data).hexdigest(),
                bindings=bindings,process_replay_qualified=False,paired_Stock_return_qualified=False,
                native_Port_consumption_qualified=False,
                limits=['single-threaded explicitly bound harness; no native lane provenance or scheduler',
                        'x87 arithmetic model, not actual Stock returned float observation',
                        'separate observed initial words are not a checkpoint',
                        'global writes modeled at return phases; intervening/cross-lane memory read order unobserved',
                        'outer consumer of engine clock is unobserved; these are not observed System.currentTime invocations'])
    with Path(report).open('x') as stream:json.dump(result,stream,indent=2);stream.write('\n')
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ('capture','stock','fixture','report'):parser.add_argument('--'+key,type=Path,required=True)
    args=parser.parse_args()
    print(json.dumps(project(args.capture,args.stock,args.fixture,args.report)))
