# Runtime-only build smoke test (imported tree)

Purpose: prove that the imported `runtime/` compiles on its own, with no generated game code, so that any
later first-boot failure is the game's, not the import's.

A real build needs `--gen` to point at lifted C (which needs the NSOs). For this test a **dummy** gen tree
supplies the two translation units that `mkninja_linux.py` expects (`gen/main/*.cpp` and the `gen/sdk/*.cpp`
sidecar directory), and only the `objs-rt` ninja target is built — the link is never attempted, so the
missing `g_ftab_init` / `g_s_ftab_init` / `g_imp_names` symbols from the generated code do not matter.

## Inputs

```bash
cd "/mnt/SSD Storage/Games/Ports/Switch/Splat3/Work"
mkdir -p gen-smoke/main gen-smoke/sdk
printf 'extern "C" int smoke_gen_dummy(int x) { return x + 1; }\n' > gen-smoke/main/tu_smoke.cpp
printf 'extern "C" int smoke_sdk_dummy(int x) { return x + 2; }\n' > gen-smoke/sdk/tu_smoke.cpp
```

`--gen /w/gen-smoke/main` puts the sdk sidecar at `/w/gen-smoke/sdk` (`mkninja_linux.py:76`), which is why
the dummy directory is laid out that way. `--obj /w/obj-smoke` keeps these objects out of the real cache.

## Commands (exactly as run)

```bash
podman run --rm -v "$PWD:/w:z" -w /w/port/totk-recomp localhost/engage-linux-build:latest bash -lc '
set -e
export CC=clang CXX=clang++
python3 tools/recomp/mkninja_linux.py --null \
  --root /w/port/totk-recomp --gen /w/gen-smoke/main --obj /w/obj-smoke \
  --deps /w/deps --syslib /w/syslib --sysinc /w/deps/libshaderc/include \
  --out /w/build-smoke --gen-opt -O1 --gen-pool 4
ninja -C /w/build-smoke -j8 objs-rt
'
```

## Result — PASS, first attempt, no source changes needed

```
backend: null | 2 generated objects, 27 runtime objects -> /w/build-smoke
ninja: Entering directory `/w/build-smoke'
[1/27] CXX /w/build-smoke/rt/hle_account.o
[2/27] CXX /w/build-smoke/rt/hle_libc.o
[3/27] CXX /w/build-smoke/rt/hle_codec.o
[4/27] CXX /w/build-smoke/rt/hle_oe.o
[5/27] CXX /w/build-smoke/rt/hle_os_mem.o
[6/27] CXX /w/build-smoke/rt/hle_nvn.o
[7/27] CXX /w/build-smoke/rt/hle_movie.o
[8/27] CXX /w/build-smoke/rt/hle_printf.o
[9/27] CXX /w/build-smoke/rt/hle_time.o
[10/27] CXX /w/build-smoke/rt/hle_util.o
[11/27] CXX /w/build-smoke/rt/rt_core.o
[12/27] CXX /w/build-smoke/rt/hle_fs.o
[13/27] CXX /w/build-smoke/rt/hle_svc.o
[14/27] CXX /w/build-smoke/rt/hle_osd.o
[15/27] CC /w/build-smoke/nvn/backend_null.o
[16/27] CXX /w/build-smoke/rt/hle_hid.o
[17/27] CC /w/build-smoke/nvn/formats.o
[18/27] CC /w/build-smoke/nvn/astc.o
[19/27] CC /w/build-smoke/nvn/tegra.o
[20/27] CC /w/build-smoke/nvn/null_stubs.o
[21/27] CC /w/build-smoke/nvn/cmdbuf.o
[22/27] CC /w/build-smoke/nvn/objects.o
[23/27] CXX /w/build-smoke/rt/hle_vi.o
[24/27] CXX /w/build-smoke/rt/hle_audio.o
[25/27] CXX /w/build-smoke/rt/plat_posix.o
[26/27] CXX /w/build-smoke/rt/rt_main.o
[27/27] CXX /w/build-smoke/rt/hle_os_thread.o
```

* 27/27 objects, exit 0, no warnings (the flags carry `-Wno-everything`, so this is a compile-success test,
  not a lint).
* `hle_svc.cpp` (the new file) is compiled — object 13.
* Toolchain: `clang version 22.1.8 (Fedora 22.1.8-4.fc44)`, `ninja 1.13.2`.
* The Vulkan backend is *not* covered here (`--null` skips `runtime/nvn/vulkan/*` and `maxwell/*`); it needs
  `deps/vulkan-headers`, which is present, and is covered by `build-vk.sh` once real `gen` exists.

## Python tooling check

```bash
python3 -m py_compile tools/recomp/gen.py tools/recomp/mkninja_linux.py tools/input_sequencer.py   # ok
```

**`capstone` is missing from the image** (`ModuleNotFoundError: No module named 'capstone'`), and `gen.py`
imports it at module level. `python3 -m pip install capstone` works in the image (installs 5.0.7) but is not
persisted by `--rm`; either put it in the image or run it at the start of every generation session. This does
not affect the runtime build or this smoke test.

## Cleanup

`gen-smoke/`, `obj-smoke/`, `build-smoke/` are throwaway (they hold no game data). They are outside the repo;
delete them whenever the space matters.
