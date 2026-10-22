#!/usr/bin/env python3
"""Read actual source-eye pixels. Frame labels do not prove source freshness."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
try:
    from .analyze import HEADER, read
except ImportError:
    from analyze import HEADER, read

PREFIX = struct.Struct('<qQQ')


def read_pixels(path):
    raw = Path(path).read_bytes()
    if len(raw) < HEADER.size:
        raise ValueError('truncated pixel header')
    h = HEADER.unpack_from(raw)
    if h[0] != b'DDPIXELS' or h[1] != 1 or h[16:18] != (128, 72):
        raise ValueError('unsupported pixel format')
    size = PREFIX.size + h[16]*h[17]*4
    if h[2] != size or h[6] > h[11] or len(raw) != HEADER.size + h[6]*size:
        raise ValueError('invalid pixel record size/count')
    meta = dict(run=h[3], start_ns=h[4], end_ns=h[5], count=h[6], dropped=h[7],
                mode=h[9], pid=h[10], finished=bool(h[15]), width=h[16], height=h[17])
    frames = []
    for offset in range(HEADER.size, len(raw), size):
        ns, frame, texture = PREFIX.unpack_from(raw, offset)
        if not h[4] <= ns < h[5]:
            raise ValueError('pixel timestamp outside capture')
        frames.append(dict(ns=ns, frame=frame, texture=texture,
                           rgba=raw[offset+PREFIX.size:offset+size]))
    return meta, frames


def correlate(meta, frames, traces):
    layers = {}
    for path in traces:
        m, events = read(path)
        if any(m[k] != meta[k] for k in ('run', 'mode', 'start_ns', 'end_ns')):
            raise ValueError('pixel/pose capture mismatch')
        for e in events:
            if e.kind == 5 and e.flags == 0:
                layers[e.id] = e
    rows = []
    previous = None
    seen = {}
    for index, f in enumerate(frames):
        layer = layers.get(f['frame'])
        digest = hashlib.sha256(f['rgba']).hexdigest()
        old = seen.get(digest)
        rows.append(dict(ns=f['ns'], seconds=(f['ns']-meta['start_ns'])*1e-9,
                         frame=f['frame'], texture=f['texture'], sha256=digest,
                         repeats_previous=previous == digest,
                         repeats_frame=old[1]['frame'] if old else None,
                         repeat_distance=index-old[0] if old else None,
                         repeat_age_ms=(f['ns']-old[1]['ns'])*1e-6 if old else None,
                         repeat_same_texture=f['texture'] == old[1]['texture'] if old else None,
                         layer_match=layer is not None and layer.related == f['texture'],
                         layer_pose=list(layer.v) if layer else None))
        previous = digest
        seen[digest] = index, f
    return dict(capture=meta, frames=rows,
                limits='Pixel hashes identify equality only. Inspect actual image motion before diagnosing discontinuity.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pixels')
    parser.add_argument('--traces', nargs='+', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--images', help='Optional directory for lossless RGB PPM frames')
    args = parser.parse_args()
    meta, frames = read_pixels(args.pixels)
    report = correlate(meta, frames, args.traces)
    Path(args.output).write_text(json.dumps(report, indent=2)+'\n')
    if args.images:
        output = Path(args.images); output.mkdir(parents=True, exist_ok=True)
        for i, f in enumerate(frames):
            rgb = bytes(v for j, v in enumerate(f['rgba']) if j % 4 != 3)
            (output/f'{i:04d}-{f["frame"]}.ppm').write_bytes(b'P6\n128 72\n255\n'+rgb)
    print(json.dumps(dict(meta, matched=sum(r['layer_match'] for r in report['frames']),
                          adjacent_repeats=sum(r['repeats_previous'] for r in report['frames']),
                          all_repeats=sum(r['repeats_frame'] is not None for r in report['frames'])), indent=2))


if __name__ == '__main__':
    main()
