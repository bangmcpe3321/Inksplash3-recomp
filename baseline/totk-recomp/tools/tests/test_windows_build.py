"""Build-graph/preflight tests; fixtures contain no game code or game data."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
REPO = ROOT.parents[1]
WINDOWS = ROOT / 'tools' / 'recomp' / 'mkninja_windows.py'
LINUX = ROOT / 'tools' / 'recomp' / 'mkninja_linux.py'
TRANSLATOR_TEST = ROOT / 'tools' / 'tests' / 'maxwell_translate.c'
GYRO_TEST = ROOT / 'tools' / 'tests' / 'windows_gyro.cpp'


class WindowsBuildTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='inksplash3-build-test-')
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name) / 'paths with spaces $tag'
        self.work.mkdir()
        self.opus = self.work / 'opus.lib'
        self.opus.touch()
        self.sdk = self.work / 'vulkan-sdk'
        for name in ('Include/vulkan/vulkan.h', 'Lib/vulkan-1.lib', 'Lib/shaderc_combined.lib'):
            path = self.sdk / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        for module in ('main', 'sdk'):
            directory = self.work / 'gen' / module
            directory.mkdir(parents=True)
            (directory / 'report.json').write_text(json.dumps({'functions': 2, 'tus': 2}))
            for name in ('tu_000', 'tu_001', 'ftab', 'imports'):
                (directory / f'{name}.cpp').write_text(f'int test_{module}_{name}() {{ return 0; }}\n')

    def run_tool(self, script, *args):
        return subprocess.run([sys.executable, '-B', str(script), *map(str, args)],
                              capture_output=True, text=True, timeout=60)

    def windows(self, headless=True, runtime_only=False, out=None, extra=()):
        out = out or self.work / ('build-null' if headless else 'build-vk')
        args = ['--root', ROOT, '--out', out, '--obj', self.work / 'obj',
                '--gen', self.work / 'gen' / 'main', '--opus-lib', self.opus,
                '--cxx', os.environ.get('CXX') or sys.executable, '--cc', sys.executable,
                '--vulkan-sdk', self.sdk]
        if headless:
            args += ['--null']
        if runtime_only:
            args += ['--runtime-only']
        return self.run_tool(WINDOWS, *args, *extra), out

    def graph(self, headless=True, runtime_only=False):
        result, out = self.windows(headless, runtime_only)
        self.assertEqual(result.returncode, 0, result.stderr)
        parsed = subprocess.run(['ninja', '-C', str(out), '-t', 'targets', 'all'],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(parsed.returncode, 0, parsed.stderr)
        return (out / 'build.ninja').read_text()

    def test_native_source_sets(self):
        null = self.graph()
        self.assertIn('nvn/backend_null.c', null)
        self.assertIn('nvn/null_stubs.c', null)
        self.assertNotIn('vulkan/shim_win.c', null)
        vk = self.graph(headless=False)
        self.assertIn('vulkan/shim_win.c', vk)
        self.assertIn('vulkan/shim_vma.c', vk)
        self.assertNotIn('nvn/backend_null.c', vk)
        self.assertNotIn('nvn/null_stubs.c', vk)
        for graph in (null, vk):
            self.assertNotIn('plat_posix.cpp', graph)
            self.assertNotIn('shim_sdl3.c', graph)
            self.assertNotIn('SDL3.lib', graph)
            self.assertIn('rspfile_content = $objects $libraries', graph)
            self.assertIn('-rtlib=compiler-rt', graph)
            self.assertIn('-lkernel32', graph)
            self.assertIn('-luser32', graph)
            self.assertNotIn('kernel32.lib', graph)

    def test_runtime_only_cannot_claim_a_guest_executable(self):
        graph = self.graph(runtime_only=True)
        self.assertNotIn('inksplash3.exe', graph)
        self.assertNotIn('tu_000.cpp', graph)
        self.assertIn('default objs-rt', graph)

    def test_only_reported_tus_are_compiled(self):
        (self.work / 'gen' / 'main' / 'tu_999.cpp').write_text('not C++')
        graph = self.graph()
        self.assertNotIn('tu_999', graph)
        self.assertIn('tu_001.cpp', graph)

    def test_incomplete_or_invalid_lifts_are_rejected(self):
        (self.work / 'gen' / 'sdk' / 'tu_001.cpp').unlink()
        result, out = self.windows()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('incomplete lift', result.stderr)
        self.assertFalse((out / 'build.ninja').exists())
        report = self.work / 'gen' / 'main' / 'report.json'
        for bad in ('{', '{"functions": 2, "tus": -1}', '{"functions": 2, "tus": "2"}'):
            report.write_text(bad)
            result, _ = self.windows()
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('lift', result.stderr)

    def test_invalid_runtime_root_is_rejected(self):
        result, _ = self.windows(runtime_only=True, extra=('--root', self.work))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('missing runtime source tree', result.stderr)

    def test_linux_keeps_the_shared_null_stubs_only_in_headless(self):
        deps = self.work / 'deps'
        (deps / 'vulkan-headers' / 'include').mkdir(parents=True)
        for headless in (True, False):
            out = self.work / ('linux-null' if headless else 'linux-vk')
            args = ['--root', ROOT, '--gen', self.work / 'gen' / 'main', '--out', out,
                    '--obj', self.work / 'linux-obj', '--deps', deps, '--syslib', self.work]
            if headless:
                args += ['--null']
            result = self.run_tool(LINUX, *args)
            self.assertEqual(result.returncode, 0, result.stderr)
            graph = (out / 'build.ninja').read_text()
            self.assertEqual('null_stubs.c' in graph, headless)
            self.assertNotIn('linux/null_stubs.c', graph.replace('\\', '/'))

    @unittest.skipUnless(os.name == 'nt' and os.environ.get('CXX') and shutil.which('ninja'),
                         'set CXX to native Windows Clang')
    def test_generated_objects_reused_between_backends(self):
        result, out = self.windows()
        self.assertEqual(result.returncode, 0, result.stderr)
        compiled = subprocess.run(['ninja', '-C', str(out), '-j1', 'objs-gen'],
                                  capture_output=True, text=True, timeout=60)
        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
        result, vk = self.windows(headless=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        reused = subprocess.run(['ninja', '-C', str(vk), '-n', 'objs-gen'],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(reused.returncode, 0, reused.stderr)
        self.assertIn('no work to do', reused.stdout)
        (self.work / 'gen' / 'main' / 'tu_000.cpp').write_text('int test_main_tu_000() { return 1; }\n')
        changed = subprocess.run(['ninja', '-C', str(vk), '-n', 'objs-gen'],
                                 capture_output=True, text=True, timeout=30)
        self.assertEqual(changed.returncode, 0, changed.stderr)
        self.assertIn('GEN ', changed.stdout)
        self.assertIn('tu_000.obj', changed.stdout)
        self.assertNotIn('tu_001.obj', changed.stdout)

    @unittest.skipUnless(os.name == 'nt' and os.environ.get('CXX') and shutil.which('powershell'),
                         'requires Visual Studio and PowerShell on Windows')
    def test_build_wrapper_rejects_concurrent_builds(self):
        with (self.work / '.build.lock').open('w'):
            result = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                                     '-File', str(REPO / 'scripts' / 'build-windows.ps1'),
                                     '-WorkDir', str(self.work), '-Headless', '-RuntimeOnly'],
                                    capture_output=True, text=True, timeout=60)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Cannot lock', result.stderr)
        self.assertFalse((self.work / 'build-null').exists())

    def test_private_work_stays_outside_repo(self):
        for script in ('gen-windows.py', 'run-windows.py'):
            argument = '--exefs' if script.startswith('gen') else '--romfs'
            result = self.run_tool(REPO / 'scripts' / script, '--work', REPO, argument, self.work)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('outside the repository', result.stderr)

    def test_generation_never_overwrites_existing_output(self):
        exefs = self.work / 'exefs'
        exefs.mkdir()
        for module in ('main', 'sdk'):
            (exefs / module).write_bytes(b'NSO0' + bytes(0x5c))
        marker = self.work / 'gen' / 'keep.txt'
        marker.write_text('existing output')
        result = self.run_tool(REPO / 'scripts' / 'gen-windows.py', '--work', self.work, '--exefs', exefs)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('already exists', result.stderr)
        self.assertEqual(marker.read_text(), 'existing output')

    def test_runner_rejects_splatoon_2_or_unmerged_romfs(self):
        result = self.run_tool(REPO / 'scripts' / 'run-windows.py', '--work', self.work,
                               '--romfs', self.work / 'romfs', '--headless')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Params.pack.zs', result.stderr)
        self.assertFalse((self.work / 'logs').exists())
        self.assertFalse((self.work / 'userdata').exists())

    @unittest.skipUnless(os.name == 'nt' and os.environ.get('CXX') and
                         Path(os.environ.get('VULKAN_SDK', ''), 'Bin', 'glslc.exe').is_file(),
                         'requires native Clang and Vulkan glslc')
    def test_maxwell_lea_hi_translation_and_glsl(self):
        cxx = Path(os.environ['CXX'])
        cc = cxx.with_name('clang.exe')
        glslc = Path(os.environ['VULKAN_SDK']) / 'Bin' / 'glslc.exe'
        with tempfile.TemporaryDirectory(prefix='inksplash3-lea-test-') as temp:
            work = Path(temp)
            exe = work / 'maxwell-translate-test.exe'
            compiled = subprocess.run([
                str(cc), '-std=c11', '-O1', '-Wno-everything',
                '-fms-runtime-lib=dll', '-fuse-ld=lld', '-rtlib=compiler-rt',
                '-D_CRT_SECURE_NO_WARNINGS', f'-I{ROOT / "runtime" / "nvn" / "maxwell"}',
                str(TRANSLATOR_TEST), str(ROOT / 'runtime' / 'nvn' / 'maxwell' / 'translate.c'),
                '-o', str(exe),
            ], capture_output=True, text=True, timeout=120)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            translated = subprocess.run([str(exe), str(work)], capture_output=True, text=True, timeout=30)
            self.assertEqual(translated.returncode, 0, translated.stdout + translated.stderr)
            self.assertIn('LEA.HI tests passed', translated.stdout)
            for i in range(4):
                shader = work / f'lea_hi_{i}.comp'
                spirv = work / f'lea_hi_{i}.spv'
                validated = subprocess.run([str(glslc), '-fshader-stage=compute', str(shader), '-o', str(spirv)],
                                           capture_output=True, text=True, timeout=60)
                self.assertEqual(validated.returncode, 0, validated.stdout + validated.stderr)
                self.assertTrue(spirv.is_file() and spirv.stat().st_size > 0)

    @unittest.skipUnless(os.name == 'nt' and os.environ.get('CXX'),
                         'requires native Windows Clang')
    def test_nintendo_gyro_calibration(self):
        exe = Path(tempfile.mkdtemp(prefix='inksplash3-gyro-test-')) / 'gyro-test.exe'
        self.addCleanup(shutil.rmtree, exe.parent, ignore_errors=True)
        compiled = subprocess.run([
            os.environ['CXX'], '-std=c++20', '-O1', '-Wno-everything',
            '-fms-runtime-lib=dll', '-fuse-ld=lld', '-rtlib=compiler-rt',
            '-D_CRT_SECURE_NO_WARNINGS', str(GYRO_TEST), '-o', str(exe),
        ], capture_output=True, text=True, timeout=120)
        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
        tested = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
        self.assertEqual(tested.returncode, 0, tested.stdout + tested.stderr)
        self.assertIn('Nintendo gyro calibration tests passed', tested.stdout)


if __name__ == '__main__':
    unittest.main()
