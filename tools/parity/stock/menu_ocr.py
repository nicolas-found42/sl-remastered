"""Read observed labels in fixed 800x600 private Stock captures.

This detector observes a functional UI frontier. It does not establish script
state, control consumption, intro completion or pixel equivalence with the Port.
"""
import json
import os
from pathlib import Path
import subprocess
import time


def regions(image, output, definitions, deadline):
    texts = {}
    def timeout():
        remaining = 10 if deadline is None else deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError('OCR observation deadline')
        return min(10, remaining)
    for name, crop, psm in definitions:
        destination = output / (name + '-ocr.png')
        subprocess.run(['ffmpeg', '-loglevel', 'error', '-threads', '1', '-filter_threads', '1',
                        '-y', '-i', str(image), '-vf', crop, '-frames:v', '1', str(destination)],
                       check=True, timeout=timeout())
        process = subprocess.run(['tesseract', str(destination), 'stdout', '--psm', psm],
                                 stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=True,
                                 timeout=timeout(), env=dict(os.environ, OMP_THREAD_LIMIT='1'))
        texts[name] = process.stdout.decode(errors='replace')
    return texts


def evaluate(image, out_dir, deadline=None):
    image, out_dir = Path(image), Path(out_dir)
    texts = regions(image, out_dir, [
        ('header', 'scale=800:600,crop=380:35:0:0,scale=1520:140:flags=lanczos', '7'),
        ('choices', 'scale=800:600,crop=220:190:415:90,scale=880:760:flags=lanczos', '6')], deadline)
    joined = '\n'.join(texts.values()).upper()
    labels = {name: name in joined for name in ('MAIN MENU', 'FREE RIDE', 'OPTIONS')}
    (out_dir / 'region-ocr.json').write_text(json.dumps({'source': str(image), 'texts': texts, 'labels': labels}, indent=2))
    return labels


def evaluate_dialog(image, out_dir, deadline=None):
    image, out_dir = Path(image), Path(out_dir)
    texts = regions(image, out_dir, [
        ('dialog', 'crop=500:380:150:110,scale=1000:760:flags=lanczos', '11'),
        ('cancel', 'crop=120:30:420:452,scale=480:120:flags=lanczos', '7')], deadline)
    labels = {'name_title': 'ENTER PLAYER NAME' in texts['dialog'].upper(),
              'cancel_button': 'CANCEL' in texts['cancel'].upper()}
    (out_dir / 'dialog-ocr.json').write_text(json.dumps({'source': str(image), 'texts': texts, 'labels': labels}, indent=2))
    return labels


def evaluate_exit(image, out_dir, deadline=None):
    image, out_dir = Path(image), Path(out_dir)
    texts = regions(image, out_dir, [
        ('exit-title', 'crop=120:30:340:120,scale=480:120:flags=lanczos', '7'),
        ('exit-question', 'crop=300:35:250:165,scale=1200:140:flags=lanczos', '7'),
        ('exit-yes', 'crop=100:30:270:452,extractplanes=b,lut=y=if(gt(val\\,120)\\,255\\,0),negate,scale=400:120:flags=neighbor', '7'),
        ('exit-no', 'crop=100:30:430:452,extractplanes=b,lut=y=if(gt(val\\,120)\\,255\\,0),negate,scale=400:120:flags=neighbor', '7')], deadline)
    import re
    has = lambda key, word: re.search(r'\b' + word + r'\b', texts[key].upper()) is not None
    labels = {'title': has('exit-title', 'EXIT'), 'question': has('exit-question', 'REALLY'),
              'yes': has('exit-yes', 'YES'), 'no': has('exit-no', 'NO')}
    (out_dir / 'exit-ocr.json').write_text(json.dumps({'source': str(image), 'texts': texts, 'labels': labels}, indent=2))
    return labels


def evaluate_career(image, out_dir, deadline=None):
    """Fixed-geometry external frontiers for the private SaveProof scenario."""
    import re
    image, out_dir = Path(image), Path(out_dir)
    texts = regions(image, out_dir, [
        ('career-header', 'crop=320:25:0:0,scale=1280:100:flags=lanczos', '7'),
        ('career-money', 'crop=180:25:620:0,scale=720:100:flags=lanczos', '7'),
        ('career-character-title', 'crop=300:30:250:120,scale=1200:120:flags=lanczos', '7'),
        ('career-list-row', 'crop=160:30:330:185,extractplanes=b,lut=y=if(gt(val\\,180)\\,255\\,0),negate,scale=640:120:flags=neighbor', '7'),
        ('career-dialog', 'crop=480:360:160:120,scale=960:720:flags=lanczos', '11'),
        ('career-load', 'crop=480:200:160:110,scale=960:400:flags=lanczos', '11')], deadline)
    header, money = texts['career-header'].upper(), texts['career-money'].upper()
    dialog, load = texts['career-dialog'].upper(), texts['career-load'].upper()
    labels = {'identity': re.search(r'\bSAVEPROOF\b', header) is not None,
              'money': re.search(r'(?<!\d)20000(?!\d)', money) is not None,
              'character': 'CHOOSE CHARACTER' in texts['career-character-title'].upper(),
              'welcome': re.search(r'\bWELCOME\b', dialog) is not None,
              'load_list': ('LOAD CAREER' in texts['career-character-title'].upper()
                            and re.search(r'\bSAVEPROOF\s*[-–—]', texts['career-list-row'].upper()) is not None)}
    (out_dir / 'career-ocr.json').write_text(json.dumps({'source': str(image), 'texts': texts, 'labels': labels}, indent=2))
    return labels
