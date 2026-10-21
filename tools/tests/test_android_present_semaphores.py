from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class AndroidPresentSemaphoresTest(unittest.TestCase):
    def test_pending_presents_and_allocation_failure(self):
        source = (ROOT / 'app/src/main/cpp/waylandcomp/src/vk_present.c').read_text()
        start = source.index('static void destroy_present_semaphores(')
        lifecycle = source[start:source.index('static void destroy_swapchain(', start)]
        start = source.index('        const int last = (k + 1 == presents);')
        submission = source[start:source.index('        perf_add(&g_perf.present_ns', start)]
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            (work / 'lifecycle.inc').write_text(lifecycle)
            (work / 'submission.inc').write_text(submission)
            binary = work / 'test'
            command = ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Wno-unused-parameter',
                       '-Wno-unused-variable', '-Wno-unused-but-set-variable', f'-I{work}']
            if os.environ.get('VK_INCLUDE'):
                command.append('-I' + os.environ['VK_INCLUDE'])
            command += [str(ROOT / 'tools/vr/tests/test_android_present_semaphores.c'), '-o', str(binary)]
            subprocess.run(command, check=True, capture_output=True, text=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS:', result.stdout)


if __name__ == '__main__':
    unittest.main()
