#!/usr/bin/env python3
"""Join source image repeats to producer submission and completed lease generations."""
import argparse
from collections import Counter
import json
from pathlib import Path
try:
    from .analyze import read
except ImportError:
    from analyze import read


def correlate_producer(pixel_report, metas, events):
    capture=pixel_report['capture']
    for m in metas:
        if any(m[k]!=capture[k] for k in ('run','mode','start_ns','end_ns')):
            raise ValueError('producer/pixel run or window mismatch')
    producers={m['pid'] for m in metas if m['process']=='producer'}
    complete=bool(producers) and all(m.get('finished') and not m['dropped'] and not m['unregistered'] for m in metas)
    identities={};leases={};health={};writes=[]
    for e in events:
        if e.kind==15: identities[e.id]=e
        elif e.kind==14 and e.flags==0: health[e.id]=int(e.v[0])
        elif e.kind==14 and e.flags==1:
            key=(e.id,e.related,int(e.v[0]))
            if key not in leases or e.v[4]>leases[key].v[4]: leases[key]=e
        elif e.kind==13:writes.append(e)
    rows=[]; prior_by_identity={}
    for p in pixel_report['frames']:
        identity=identities.get(p['frame']);row=dict(frame=p['frame'],seconds=p['seconds'],
            repeats_frame=p['repeats_frame'],classification='inconclusive')
        if identity is not None:
            ticket=int(identity.v[1]);inode=identity.related;device=int(identity.v[0]);pid=int(identity.v[2])
            key=(inode,device,pid);lease=leases.get((ticket,inode,device))
            row.update(inode=inode,device=device,producer_pid=pid,lease=ticket)
            valid=(complete and inode and pid in producers and lease is not None and
                   health.get(ticket)!=None and health[ticket]==0 and lease.v[3]==0 and p['layer_match'])
            if lease is not None:
                serial=int(lease.v[1]);row['completed_write_serial']=serial;row['write_kinds']=int(lease.v[2])
                old=prior_by_identity.get(key)
                if valid and old:
                    previous,old_serial=old
                    row['previous_texture_frame']=previous['frame']
                    row['new_completed_write']=serial!=old_serial
                    if p['repeats_frame']==previous['frame']:
                        if serial!=old_serial: row['classification']='repeat_despite_new_completed_commands'
                        else:
                            pending=[e for e in writes if e.flags==0 and e.related==inode and int(e.v[0])==device
                                     and previous['ns']<e.ns<=identity.ns and e.id!=serial]
                            row['write_attempts_since_previous_copy']=[e.id for e in pending]
                            row['classification']='repeat_without_new_completed_commands'
                    else:row['classification']='changed_pixels' if p['repeats_frame'] is None else 'other_repeat'
                prior_by_identity[key]=(p,serial) if valid else None
        rows.append(row)
    return dict(run=capture['run'],producer_present=bool(producers),capture_complete=complete,
        coverage_gaps=max(health.values(),default=None),counts=dict(Counter(r['classification'] for r in rows)),
        repeated_frames=[r for r in rows if r['repeats_frame'] is not None],
        limits=['Queued draw commands do not guarantee changed pixels; same producer input or clipping remains possible.',
                'A new completed write with identical imported pixels needs producer-side pixel verification before blaming the importer.',
                'Missing joins, startup coverage gaps, absent producer traces, and dropped events are inconclusive.'])


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory',type=Path);args=p.parse_args()
    root=args.directory;metas=[];events=[]
    for path in root.glob('*.bin'):
        m,e=read(path);metas.append(m);events+=e
    pixels=json.loads((root/'pixels.json').read_text())
    result=correlate_producer(pixels,metas,sorted(events,key=lambda e:e.ns))
    (root/'producer.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='repeated_frames'},indent=2))

if __name__=='__main__':main()
