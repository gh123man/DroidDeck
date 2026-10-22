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
    identities={};leases={};health={};writes=[];summaries=[];copy_done={}
    for e in events:
        if e.kind==7 and e.flags==7 and e.v[0]==0:copy_done[e.id]=e
        elif e.kind==15: identities[e.id]=e
        elif e.kind==14 and e.flags==0: health[e.id]=e
        elif e.kind==14 and e.flags==1:
            key=(e.id,e.related,int(e.v[0]))
            if key not in leases or e.v[4]>leases[key].v[4]: leases[key]=e
        elif e.kind==13 and e.flags<3:writes.append(e)
        elif e.kind==13 and e.flags==4:summaries.append(e)
    rows=[]; prior_by_identity={}
    for p in pixel_report['frames']:
        identity=identities.get(p['frame']);row=dict(frame=p['frame'],seconds=p['seconds'],
            repeats_frame=p['repeats_frame'],classification='inconclusive')
        if identity is not None:
            ticket=int(identity.v[1]);inode=identity.related;device=int(identity.v[0]);pid=int(identity.v[2])
            key=(inode,device,pid);lease=leases.get((ticket,inode,device))
            row.update(inode=inode,device=device,producer_pid=pid,lease=ticket)
            valid=(complete and inode and pid in producers and lease is not None and
                   health.get(ticket)!=None and p['layer_match'])
            if lease is not None:
                serial=int(lease.v[1]);row['completed_write_serial']=serial;row['write_kinds']=int(lease.v[2])
                old=prior_by_identity.get(key)
                if valid and old:
                    previous,old_serial,old_health=old
                    h=health[ticket]
                    coverage=(h.v[0]==0 and old_health.v[0]==0 and lease.v[3]==0)
                    if h.v[5]==2 and old_health.v[5]==2:
                        submitted=[e for e in summaries if old_health.ns<e.ns<=h.ns]
                        coverage=(h.v[1]==0 and old_health.v[1]==0 and h.v[6]<256 and old_health.v[6]<256 and
                                  all(e.v[1]==1 and e.v[2]==0 and int(e.v[4])==sum(
                                      w.flags==1 and w.id==e.id and int(w.v[2])==e.related for w in writes)
                                      for e in submitted))
                        row['covered_successful_command_buffers']=len(submitted)
                    row['interval_covered']=bool(coverage)
                    row['previous_texture_frame']=previous['frame']
                    row['new_completed_write']=serial!=old_serial
                    if not coverage:pass
                    elif p['repeats_frame']==previous['frame']:
                        if serial!=old_serial: row['classification']='repeat_despite_new_completed_commands'
                        else:
                            pending=[e for e in writes if e.flags==0 and e.related==inode and int(e.v[0])==device
                                     and previous['ns']<e.ns<=identity.ns and e.id!=serial]
                            row['write_attempts_since_previous_copy']=[e.id for e in pending]
                            row['classification']='repeat_without_new_completed_commands'
                    else:row['classification']='changed_pixels' if p['repeats_frame'] is None else 'other_repeat'
                prior_by_identity[key]=(p,serial,health[ticket]) if valid else None
        if row['classification']=='repeat_without_new_completed_commands':
            done=copy_done.get(p['frame']);h=health[row['lease']]
            later=[e for e in writes if e.flags==1 and e.related==row['inode'] and int(e.v[0])==row['device'] and e.ns>h.ns]
            following=min(later,key=lambda e:e.ns) if later else None
            if done and following:
                attempts=[e for e in writes if e.flags==0 and e.id==following.id and e.related==following.related and int(e.v[0])==row['device']]
                attempt=min(attempts,key=lambda e:e.ns) if attempts else None
                row.update(lease_ready_ns=h.ns,copy_complete_ns=done.ns,next_write_serial=following.id,
                           next_write_return_ns=following.ns,next_write_attempt_ns=attempt.ns if attempt else None,
                           next_write_after_copy=following.ns>done.ns)
        rows.append(row)
    return dict(run=capture['run'],producer_present=bool(producers),capture_complete=complete,
        coverage_gaps=max((e.v[0] for e in health.values()),default=None),counts=dict(Counter(r['classification'] for r in rows)),
        covered_changed_without_new_write=sum(r['classification']=='changed_pixels' and not r['new_completed_write'] for r in rows),
        repeated_frames=[r for r in rows if r['repeats_frame'] is not None],
        limits=['Queued draw commands do not guarantee changed pixels; same producer input or clipping remains possible.',
                'A new completed write with identical imported pixels needs producer-side pixel verification before blaming the importer.',
                'Missing joins, lost object mappings, uncovered command submissions, absent producer traces, and dropped events are inconclusive.',
                'Version 2 can recover from earlier command-recording gaps only when every successful command buffer between leases has complete coverage; metadata gaps remain disqualifying.'])


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
