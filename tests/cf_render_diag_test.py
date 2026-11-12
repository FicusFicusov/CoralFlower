#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
import pathlib
import sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
from cf_render_stats import NAMES, decode, interval, BackupError
raw=pathlib.Path(sys.argv[1]).read_bytes();actual=decode(raw)
assert actual['hardware']==0 and actual['updates']>10
assert not interval(actual,actual)['valid']
for bad in (raw[:-1], b'\x02'+raw[1:], raw+b'\0'):
    try: decode(bad)
    except BackupError: pass
    else: raise AssertionError('malformed schema accepted')
a={k:0 for k in NAMES};a.update(ms=1000,ticks=24000000,ticks_per_us=24,hardware=1)
b=dict(a);b.update(ms=2000,ticks=48000000,frames=20,updates=18,gaps=18,gap_ticks=24000000,
    frame_wall_ticks=19200000,frame_cpu_ticks=9600000,compose_cpu_ticks=4800000,
    lcd_wait_cpu_ticks=2400000,lcd_wait_wall_ticks=7200000,
    frame_audio_ticks=7200000,frame_timer_ticks=2400000,
    irq_audio_ticks=8400000,irq_timer_ticks=2400000,stamp_pair_min_ticks=12)
r=interval(a,b)
assert r['valid'] and r['flora_submissions_per_s']==18
assert r['mean_frame_ms']==40 and r['mean_compose_cpu_ms']==10
assert r['mean_present_cpu_ms']==5 and r['mean_lcd_poll_cpu_ms']==5
assert r['mean_frame_audio_ms']==15 and r['mean_frame_timer5_ms']==5
assert r['audio_irq_fraction']==.35 and r['timer5_irq_fraction']==.1
for key,value in [('hardware',0),('screen',2),('popup',1),('sound_chord',1),('scene_epoch',2),('ms',999),('frames',0),('frame_cpu_ticks',19200000)]:
    wrong=dict(b);wrong[key]=value;assert not interval(a,wrong)['valid'],key
# Cumulative wrap is valid within a bounded capture (including ticks and uptime).
wrapped=('ticks','ms','frame_wall_ticks','updates')
wa={k:0xfffffff0 if k in wrapped else v for k,v in a.items()}
wb={k:(v+0xfffffff0-a[k])&0xffffffff if k in wrapped else v for k,v in b.items()}
assert interval(wa,wb)['flora_submissions_per_s']==18
print('RENDER_DECODER: PASS physical/source gating, known decomposition, malformed input, mixed scene, wrap')
