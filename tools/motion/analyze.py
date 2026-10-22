#!/usr/bin/env python3
"""Decode bounded motion captures. Candidates are measurements, not diagnoses."""
import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass
import glob
import json
import math
from pathlib import Path
import struct

HEADER = struct.Struct('<8sIIQqqQQIIIIQ16s4Q')
RECORD = struct.Struct('<qQQII12d')
NAMES = {1: 'sensor', 2: 'publish', 3: 'driver', 4: 'sent', 5: 'layer', 6: 'present',
         7: 'copy', 8: 'vsync', 9: 'throttle', 10: 'recenter', 11: 'android', 12: 'commit'}

@dataclass
class Event:
    ns: int
    id: int
    related: int
    kind: int
    flags: int
    v: tuple
    process: str = ''
    slot: int = 0


def read(path):
    raw = Path(path).read_bytes()
    if len(raw) < HEADER.size:
        raise ValueError(f'{path}: truncated header')
    h = HEADER.unpack_from(raw)
    if h[0] != b'DDMOTION' or h[1:3] != (1, RECORD.size):
        raise ValueError(f'{path}: unsupported format')
    if len(raw) != HEADER.size + h[6] * RECORD.size:
        raise ValueError(f'{path}: truncated or extra records')
    process = h[13].split(b'\0')[0].decode('ascii')
    meta = dict(path=str(path), run=h[3], start_ns=h[4], end_ns=h[5], count=h[6],
                dropped=h[7], slot=h[8], mode=h[9], pid=h[10], capacity=h[11],
                unregistered=h[12], process=process, exported_ns=h[14], finished=bool(h[15]))
    events = []
    for offset in range(HEADER.size, len(raw), RECORD.size):
        r = RECORD.unpack_from(raw, offset)
        events.append(Event(*r[:5], r[5:], process, h[8]))
    return meta, events


def angle(a, b):
    na = math.sqrt(sum(x*x for x in a)); nb = math.sqrt(sum(x*x for x in b))
    if not math.isfinite(na*nb) or min(na, nb) < 1e-9:
        raise ValueError('invalid quaternion')
    dot = abs(sum(x*y for x, y in zip(a, b)) / (na*nb))
    return math.degrees(2 * math.acos(min(1, dot)))


def matrix_quat(m):
    if len(m) == 12:
        m = [m[i] for i in (0, 1, 2, 4, 5, 6, 8, 9, 10)]
    if not all(math.isfinite(x) for x in m):
        raise ValueError('nonfinite matrix')
    rows = [m[0:3], m[3:6], m[6:9]]
    for i in range(3):
        for j in range(3):
            if abs(sum(a*b for a, b in zip(rows[i], rows[j])) - (i == j)) > .02:
                raise ValueError('non-rotation matrix')
    det = (m[0]*(m[4]*m[8]-m[5]*m[7]) - m[1]*(m[3]*m[8]-m[5]*m[6]) + m[2]*(m[3]*m[7]-m[4]*m[6]))
    if abs(det-1) > .02:
        raise ValueError('reflected matrix')
    tr = m[0]+m[4]+m[8]
    if tr > 0:
        s = math.sqrt(tr+1)*2
        return (s/4, (m[7]-m[5])/s, (m[2]-m[6])/s, (m[3]-m[1])/s)
    i = max(range(3), key=lambda k: m[4*k]); j = (i+1)%3; k = (i+2)%3
    s = math.sqrt(1+m[4*i]-m[4*j]-m[4*k])*2
    q = [0., 0., 0., 0.]
    q[0] = (m[k*3+j]-m[j*3+k])/s; q[i+1] = s/4
    q[j+1] = (m[i*3+j]+m[j*3+i])/s; q[k+1] = (m[i*3+k]+m[k*3+i])/s
    return tuple(q)


def synthetic(mode, start, now):
    t = max(0, (now-start)*1e-9)
    yaw = math.pi/6 * math.sin(t*math.pi/4) if mode == 2 else 0
    return (math.cos(yaw/2), 0, math.sin(yaw/2), 0)


def stats(values):
    if not values:
        return None
    v = sorted(values)
    return dict(n=len(v), p50=v[len(v)//2], p99=v[min(len(v)-1, int(len(v)*.99))], max=v[-1])


def fit_layer_yaw(events, epoch):
    samples = []
    for e in events:
        if e.kind != 5 or e.flags != 0: continue
        try: matrix_quat(e.v)
        except ValueError: continue
        samples.append(((e.ns-epoch)*1e-9, math.degrees(math.atan2(e.v[2], e.v[10]))))
    if len(samples) < 2: return None
    def residuals(shift):
        return [yaw-30*math.sin((t+shift)*math.pi/4) for t,yaw in samples]
    def score(shift):
        errors = residuals(shift); bias = sum(errors)/len(errors)
        return sum((v-bias)**2 for v in errors)
    shift = min((ms/1000 for ms in range(-100, 101)), key=score)
    errors = residuals(shift); bias = sum(errors)/len(errors)
    expected = [30*math.sin((t+shift)*math.pi/4) for t,_ in samples]
    backward = sum(abs(b-a) > .1 and (b-a)*(samples[i+1][1]-samples[i][1]) < 0
                   for i,(a,b) in enumerate(zip(expected, expected[1:])))
    return dict(time_shift_ms=shift*1000, bias_degrees=bias,
                absolute_residual_degrees=stats([abs(v-bias) for v in errors]),
                backward_steps=backward,
                fit_at_search_boundary=abs(shift) == .1,
                limits='Yaw metadata fit, not image motion. Fits constant time shift and bias; excludes near-stationary direction checks.')


def analyze(metas, events):
    if len({m['run'] for m in metas}) != 1 or len({m['mode'] for m in metas}) != 1:
        raise ValueError('analyze one run and one mode at a time')
    if len({(m['start_ns'], m['end_ns']) for m in metas}) != 1:
        raise ValueError('capture clocks/windows do not agree')
    mode = metas[0]['mode']; epoch = metas[0]['start_ns']
    events = sorted(events, key=lambda e: (e.ns, e.process, e.slot))
    groups = defaultdict(list)
    for e in events:
        groups[e.kind].append(e)
    issues = []
    def issue(e, reason, **detail):
        issues.append(dict(ns=e.ns, seconds=(e.ns-epoch)*1e-9, boundary=NAMES[e.kind],
                           reason=reason, id=e.id, related=e.related, **detail))
    intervals = {}; angular = {}; previous = {}; valid_matrices = invalid_matrices = 0
    for e in events:
        key = None; q = None
        try:
            if e.kind == 1 and e.flags & 1: key, q = 'filtered', e.v[:4]
            elif e.kind == 2: key, q = 'published', matrix_quat(e.v[:9])
            elif e.kind == 3: key, q = 'driver_effective', e.v[4:8]
            elif e.kind == 4: key, q = 'sent', e.v[:4]
            elif e.kind == 5 and e.flags == 0:
                q = matrix_quat(e.v); key = 'layer_first_left'; valid_matrices += 1
        except ValueError as exc:
            issue(e, str(exc))
            if e.kind == 5: invalid_matrices += 1
        if key is None: continue
        try:
            angle(q, q)
            if key in previous:
                before, prev_q = previous[key]
                dt = (e.ns-before.ns)*1e-9
                step = angle(prev_q, q)
                intervals.setdefault(key, []).append(dt*1000)
                angular.setdefault(key, []).append(step)
                if dt > .025: issue(e, 'arrival_gap', ms=dt*1000)
                if step > 2: issue(e, 'angular_step_candidate', degrees=step, ms=dt*1000)
                if mode and key in ('driver_effective', 'sent', 'layer_first_left') and step > (30*math.pi/4*dt if mode == 2 else 0)+.25:
                    issue(e, 'exceeds_synthetic_speed_envelope', degrees=step, ms=dt*1000)
            previous[key] = e, q
        except ValueError as exc:
            issue(e, str(exc))
    sensors = {e.id: e for e in groups[1]}
    publications = {e.id: e for e in groups[2]}
    drivers = {e.id: e for e in groups[3]}
    misses = Counter(); ages = []; seq_prev = None
    synthetic_errors = []
    for e in groups[3]:
        if seq_prev is not None and ((e.related-seq_prev) & 0xffffffff) >= 0x80000000:
            issue(e, 'shared_sequence_regression', previous=seq_prev)
        seq_prev = e.related
        age = (e.ns-e.v[8])*1e-6; ages.append(age)
        if age < -.1: issue(e, 'clock_or_publication_in_future', age_ms=age)
        p = publications.get(e.related)
        if not p: misses['driver_without_publication'] += 1
        elif not e.flags & 4:
            try:
                error = angle(e.v[:4], matrix_quat(p.v[:9]))
                if error > .05: issue(e, 'publication_to_driver_mismatch', degrees=error)
            except ValueError: pass
        if mode:
            error = angle(e.v[4:8], synthetic(mode, epoch, e.v[10]))
            synthetic_errors.append(error)
            if error > .01: issue(e, 'synthetic_function_mismatch', degrees=error)
    for e in groups[4]:
        d = drivers.get(e.id)
        if not d: misses['sent_without_driver'] += 1
        elif angle(e.v[:4], d.v[4:8]) > .01: issue(e, 'driver_to_callback_mismatch')
    sensor_clock = []
    before = None
    for e in groups[1]:
        if before and not e.flags & 2:
            if e.related <= before.related: issue(e, 'sensor_timestamp_regression_or_duplicate')
            if e.related > before.related:
                sensor_clock.append(((e.v[10]-before.v[10])-(e.related-before.related))*1e-6)
        before = e
    last_pair = None
    recentered = [e.ns for e in groups[10]]
    for e in groups[2]:
        source = sensors.get(e.related)
        if e.related and source is None: misses['publication_without_sensor'] += 1
        if source is None or not source.flags & 1: continue
        if last_pair:
            old, old_source = last_pair
            if not any(old.ns < ns <= e.ns for ns in recentered):
                try:
                    sensor_step = angle(old_source.v[:4], source.v[:4])
                    published_step = angle(matrix_quat(old.v[:9]), matrix_quat(e.v[:9]))
                    if abs(sensor_step-published_step) > .1:
                        issue(e, 'sensor_to_publication_step_mismatch', sensor_degrees=sensor_step,
                              publication_degrees=published_step)
                except ValueError: pass
        last_pair = e, source
    durations = {}
    for kind, first, last, label in ((6,0,1,'present'), (7,1,2,'lease'), (7,3,4,'acquire'),
            (7,5,6,'copy_submit'), (7,6,7,'copy_fence'), (7,9,10,'linux_present'),
            (9,0,1,'post_present'), (11,2,3,'android_submit'), (11,4,5,'android_present')):
        pending = {}; samples = []
        for e in groups[kind]:
            if e.flags == first: pending[e.id] = e
            elif e.flags == last and e.id in pending:
                samples.append((e.ns-pending.pop(e.id).ns)*1e-6)
        durations[label] = stats(samples)
    lost = sum(m['dropped'] for m in metas) + max(m['unregistered'] for m in metas)
    required = (1,2,3,4,5,6,7,8,9,11)
    missing = [NAMES[k] for k in required if not groups[k]]
    interrupted = any(not m.get('finished', True) for m in metas)
    return dict(run=metas[0]['run'], mode=mode, complete=not lost and not missing and not interrupted,
        interrupted=interrupted,
        synthetic_layer_yaw=fit_layer_yaw(groups[5], epoch) if mode == 2 else None,
        lost=lost, missing_boundaries=missing, counts={NAMES[k]:len(v) for k,v in groups.items()},
        joins_missing=dict(misses), intervals_ms={k:stats(v) for k,v in intervals.items()},
        angular_step_degrees={k:stats(v) for k,v in angular.items()}, publication_age_ms=stats(ages),
        sensor_host_increment_difference_ms=stats(sensor_clock), synthetic_error_degrees=stats(synthetic_errors),
        render_pose=dict(valid=valid_matrices, invalid=invalid_matrices,
                         varying=bool(angular.get('layer_first_left') and max(angular['layer_first_left']) > .001)),
        durations_ms=durations, issue_counts=dict(Counter(i['reason'] for i in issues)),
        first_candidates=sorted(issues, key=lambda i:i['ns'])[:100],
        limits=['CPU submit/copy IDs do not identify source pixel generations or optical output.',
                'Render-pose interpretation needs the synthetic run; a rotation matrix alone is insufficient.',
                'Candidates and arrival gaps do not by themselves establish the cause of a visible snap.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('files', nargs='+')
    parser.add_argument('--output')
    args = parser.parse_args()
    paths = sorted({p for pattern in args.files for p in glob.glob(pattern)})
    if not paths: parser.error('no captures matched')
    metas, events = [], []
    for path in paths:
        m, e = read(path); metas.append(m); events.extend(e)
    result = json.dumps(analyze(metas, events), indent=2)
    if args.output: Path(args.output).write_text(result+'\n')
    else: print(result)

if __name__ == '__main__': main()
