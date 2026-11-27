"""Write a native x64 Windows Ninja build using Clang's MSVC ABI.

Linux objects/libraries are not reusable. --runtime-only creates compilation
targets but deliberately no executable, for testing platform code before a lift
finishes. Null and Vulkan builds share generated objects in --obj.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess


def esc(value):
    return str(value).replace('\\', '/').replace('$', '$$').replace(' ', '$ ').replace(':', '$:')


def literal(value):
    return str(value).replace('$', '$$')


def quote(value):
    return literal(subprocess.list2cmdline([str(value).replace('\\', '/')]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--null', action='store_true')
    parser.add_argument('--runtime-only', action='store_true')
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--gen', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--obj', type=Path)
    parser.add_argument('--opus-lib', type=Path, required=True)
    parser.add_argument('--vulkan-sdk', type=Path, default=os.environ.get('VULKAN_SDK'))
    parser.add_argument('--cxx', default=os.environ.get('CXX') or shutil.which('clang++'))
    parser.add_argument('--cc', default=os.environ.get('CC'))
    parser.add_argument('--gen-opt', choices=('-O0', '-O1', '-O2'), default='-O1')
    parser.add_argument('--gen-pool', type=int, default=1)
    args = parser.parse_args()
    if args.gen_pool < 1:
        parser.error('--gen-pool must be positive')
    if not args.cxx:
        parser.error('Clang is missing; run scripts/build-windows.ps1 or pass --cxx')
    cxx = Path(args.cxx).resolve()
    cc = Path(args.cc).resolve() if args.cc else cxx.with_name('clang.exe')
    for path in (cxx, cc, args.opus_lib):
        if not path.is_file():
            parser.error(f'missing {path}')
    rt = args.root.resolve() / 'runtime'
    if not (rt / 'rt_main.cpp').is_file():
        parser.error(f'missing runtime source tree: {rt}')
    out = args.out.resolve()
    obj = args.obj.resolve() if args.obj else out / 'obj'
    generated = []
    if not args.runtime_only:
        if not args.gen:
            parser.error('--gen is required unless --runtime-only is used')
        gen = args.gen.resolve()
        for module, directory in (('main', gen), ('sdk', gen.parent / 'sdk')):
            report_path = directory / 'report.json'
            if not report_path.is_file():
                parser.error(f'missing completed lift: {report_path}')
            try:
                report = json.loads(report_path.read_text(encoding='utf-8'))
            except (ValueError, UnicodeError) as error:
                parser.error(f'invalid lift report {report_path}: {error}')
            if not isinstance(report, dict) or not all(
                type(report.get(key)) is int and report[key] > 0 for key in ('functions', 'tus')
            ):
                parser.error(f'empty or invalid lift: {report_path}')
            # Do not glob stale TUs left by a differently batched generation.
            sources = [directory / f'tu_{i:03d}.cpp' for i in range(report['tus'])]
            sources += [directory / 'ftab.cpp', directory / 'imports.cpp']
            for source in sources:
                if not source.is_file():
                    parser.error(f'incomplete lift: missing {source}')
                generated.append((module, source))
    libraries = [args.opus_lib.resolve()]
    if not args.null:
        if not args.vulkan_sdk:
            parser.error('set VULKAN_SDK or pass --vulkan-sdk')
        sdk = args.vulkan_sdk.resolve()
        for path in (sdk / 'Include' / 'vulkan' / 'vulkan.h',
                     sdk / 'Lib' / 'vulkan-1.lib', sdk / 'Lib' / 'shaderc_combined.lib'):
            if not path.is_file():
                parser.error(f'missing Vulkan SDK file: {path}')
        libraries += [sdk / 'Lib' / 'vulkan-1.lib', sdk / 'Lib' / 'shaderc_combined.lib']
    # These go through clang's response file, so they must remain linker
    # options. Bare `user32.lib` entries are treated as input filenames by
    # clang before lld gets a chance to resolve them through LIB.
    system_libs = ['-lkernel32', '-luser32', '-lgdi32', '-lole32', '-luuid',
                   '-lmfplat', '-lmfuuid']
    rt_cpp = sorted(rt.glob('*.cpp'))
    nvn_c = sorted((rt / 'nvn').glob('*.c'))
    renderer = []
    if not args.null:
        nvn_c = [s for s in nvn_c if s.name not in ('backend_null.c', 'null_stubs.c')]
        renderer = sorted((rt / 'nvn' / 'vulkan').glob('*.c'))
        renderer += sorted((rt / 'nvn' / 'maxwell').glob('*.c'))
    common = '-march=x86-64-v3 -fms-runtime-lib=dll -Wno-everything -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS'
    cpp = f'-std=c++20 -O2 -fno-exceptions {common} -I{quote(rt)}'
    c = f'-std=gnu17 -O2 {common} -I{quote(rt / "nvn")} -I{quote(rt / "nvn" / "shim")}'
    # Clang 22's SLP/AVX2 RegisterCoalescer crashes on some lifted functions.
    # Keep runtime vectorization; disable only that pass for generated code.
    genflags = cpp.replace(' -O2 ', f' {args.gen_opt} ') + ' -fno-slp-vectorize'
    vkflags = c
    if not args.null:
        vkflags += f' -I{quote(sdk / "Include")} -include {quote(rt / "nvn" / "shim" / "compat.h")}'
    out.mkdir(parents=True, exist_ok=True)
    cache = obj / 'ninja-cache'
    cache.mkdir(parents=True, exist_ok=True)
    objects, gen_objects = [], []
    with (out / 'build.ninja').open('w', encoding='utf-8', newline='\n') as f:
        # Ninja also needs the same command/dependency log to reuse objects
        # between backends. The build wrapper locks this work directory.
        f.write(f'builddir = {literal(cache.as_posix())}\n')
        f.write(f'cxx = {quote(cxx)}\ncc = {quote(cc)}\ncppflags = {cpp}\ncflags = {c}\nvkflags = {vkflags}\ngenflags = {genflags}\n')
        f.write(f'pool generated\n  depth = {args.gen_pool}\n')
        for rule, compiler, flags in (('cpp', '$cxx', '$cppflags'), ('c', '$cc', '$cflags'),
                                     ('vk', '$cc', '$vkflags'), ('gen', '$cxx', '$genflags')):
            f.write(f'rule {rule}\n  command = {compiler} {flags} -MMD -MF $out.d -c $in -o $out\n'
                    '  depfile = $out.d\n  deps = gcc\n  description = ' + rule.upper() + ' $out\n')
            if rule == 'gen':
                f.write('  pool = generated\n')

        def edge(source, target, rule, outputs):
            target.parent.mkdir(parents=True, exist_ok=True)
            f.write(f'build {esc(target)}: {rule} {esc(source)}\n')
            outputs.append(target)

        for module, source in generated:
            edge(source, obj / module / (source.stem + '.obj'), 'gen', gen_objects)
        for source in rt_cpp:
            edge(source, out / 'rt' / (source.stem + '.obj'), 'cpp', objects)
        for source in nvn_c:
            edge(source, out / 'nvn' / (source.stem + '.obj'), 'c', objects)
        for source in renderer:
            edge(source, out / 'nvn' / source.parent.name / (source.stem + '.obj'), 'vk', objects)
        f.write('build objs-rt: phony ' + ' '.join(map(esc, objects)) + '\n')
        if args.runtime_only:
            f.write('default objs-rt\n')
        else:
            f.write('build objs-gen: phony ' + ' '.join(map(esc, gen_objects)) + '\n')
            # Response files also handle a full game's objects beyond cmd.exe's
            # 8191-character limit, including quoted paths with drive letters.
            f.write('rule link\n  command = $cxx -fuse-ld=lld -fms-runtime-lib=dll -rtlib=compiler-rt -o $out @$out.rsp -Xlinker /STACK:8388608\n'
                    '  rspfile = $out.rsp\n  rspfile_content = $objects $libraries\n  description = LINK $out\n')
            exe = out / 'inksplash3.exe'
            all_objects = gen_objects + objects
            f.write(f'build {esc(exe)}: link ' + ' '.join(map(esc, all_objects)) +
                    ' | ' + ' '.join(map(esc, libraries)) + '\n')
            f.write('  objects = ' + ' '.join(map(quote, all_objects)) + '\n')
            f.write('  libraries = ' + ' '.join(map(quote, libraries)) + ' ' + ' '.join(system_libs) + '\n')
            f.write(f'default {esc(exe)}\n')
    print(f'{"null" if args.null else "Vulkan"}: {len(gen_objects)} generated objects, '
          f'{len(objects)} runtime objects -> {out}')


if __name__ == '__main__':
    main()
