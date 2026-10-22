import math
from pathlib import Path
import tempfile
import unittest
from tools.motion.analyze import Event, HEADER, RECORD, analyze, angle, matrix_quat, read, synthetic

EPOCH = 1_000_000_000
META = dict(run=1, mode=2, start_ns=EPOCH, end_ns=EPOCH+30_000_000_000, dropped=0, unregistered=0)


def driver(i, t, seq=None, q=None):
    q = q or synthetic(2, EPOCH, t)
    v = (*q, *q, t-1_000_000, 0, t, i)
    return Event(t, i, seq if seq is not None else i*2, 3, 3, v)


class MotionTraceTests(unittest.TestCase):
    def test_quaternion_sign_is_not_a_snap(self):
        q = synthetic(2, EPOCH, EPOCH+1_000_000_000)
        self.assertAlmostEqual(angle(q, tuple(-v for v in q)), 0)

    def test_valid_synthetic_and_injected_reset(self):
        es = [driver(i, EPOCH+i*2_000_000) for i in range(1, 1000)]
        report = analyze([META], es)
        self.assertNotIn('synthetic_function_mismatch', report['issue_counts'])
        es[500] = driver(501, es[500].ns, q=(1, 0, 0, 0))
        report = analyze([META], es)
        self.assertEqual(report['issue_counts']['synthetic_function_mismatch'], 1)
        self.assertGreater(report['issue_counts']['exceeds_synthetic_speed_envelope'], 0)

    def test_delay_and_reordering_detected(self):
        es = [driver(1, EPOCH+1_000_000, 4), driver(2, EPOCH+41_000_000, 2)]
        report = analyze([META], es)
        self.assertIn('arrival_gap', report['issue_counts'])
        self.assertIn('shared_sequence_regression', report['issue_counts'])

    def test_sequence_wrap_allowed(self):
        es = [driver(1, EPOCH+1_000_000, 0xfffffffe), driver(2, EPOCH+3_000_000, 0)]
        self.assertNotIn('shared_sequence_regression', analyze([META], es)['issue_counts'])

    def test_malformed_render_pose_is_not_evidence(self):
        e = Event(EPOCH, 1, 0, 5, 0, (0,)*12)
        report = analyze([META], [e])
        self.assertEqual(report['render_pose']['invalid'], 1)
        self.assertFalse(report['render_pose']['varying'])

    def test_rotation_and_reflection(self):
        q = matrix_quat((0, 0, 1, 0, 1, 0, -1, 0, 0))
        self.assertAlmostEqual(angle(q, (math.sqrt(.5), 0, math.sqrt(.5), 0)), 0)
        with self.assertRaises(ValueError): matrix_quat((-1,0,0,0,1,0,0,0,1))

    def test_callback_correlated_by_actual_driver_call(self):
        d = driver(7, EPOCH+100_000_000)
        s = Event(d.ns+10, 7, 0, 4, 1, (*d.v[4:8], 0, 0, 0, 0, 0, 0, 0, 0))
        self.assertNotIn('driver_to_callback_mismatch', analyze([META], [d,s])['issue_counts'])
        s.v = (1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
        self.assertIn('driver_to_callback_mismatch', analyze([META], [d,s])['issue_counts'])

    def test_overflow_and_absent_stages_are_inconclusive(self):
        report = analyze([dict(META, dropped=1)], [])
        self.assertFalse(report['complete'])
        self.assertEqual(report['lost'], 1)
        self.assertIn('layer', report['missing_boundaries'])

    def test_reject_mixed_runs_and_clocks(self):
        with self.assertRaises(ValueError): analyze([META, dict(META, run=2)], [])
        with self.assertRaises(ValueError): analyze([META, dict(META, start_ns=EPOCH+1)], [])

    def test_interrupted_capture_is_inconclusive(self):
        report = analyze([dict(META, finished=False)], [])
        self.assertTrue(report['interrupted'])
        self.assertFalse(report['complete'])

    def test_binary_roundtrip_and_truncation(self):
        e = driver(1, EPOCH+1_000_000)
        header = HEADER.pack(b'DDMOTION', 1, 128, 1, EPOCH, EPOCH+30_000_000_000,
                             1, 0, 0, 2, 42, 65536, 0, b'driver', EPOCH+30_000_000_000, 1, 0, 0)
        record = RECORD.pack(e.ns, e.id, e.related, e.kind, e.flags, *e.v)
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)/'trace.bin'; p.write_bytes(header+record)
            m, es = read(p)
            self.assertEqual(m['process'], 'driver')
            self.assertEqual(es[0].v, e.v)
            p.write_bytes((header+record)[:-1])
            with self.assertRaises(ValueError): read(p)


if __name__ == '__main__': unittest.main()
