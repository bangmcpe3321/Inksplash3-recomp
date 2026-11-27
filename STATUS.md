# Status

Updated as milestones land. Every claim links to a log or a measurement.

## Milestones

| # | Milestone | State |
|---|---|---|
| 0 | Runtime baseline hardened + buildable | **done** (`docs/BUILD_SMOKE.md`) |
| 1 | Splat3 ExeFS/RomFS extracted | **done** (v11.3.0 update; BKTR-merged RomFS 22,857 files / 9.37 GiB) |
| 2 | NSO images + function tables | in progress |
| 3 | `main` + `sdk` lifted, unhandled triaged | **done**: main 272,492 fns / 2,051 unhandled; sdk 32,676 fns / 4,760 |
| 3 | `main` + `sdk` lifted, unhandled triaged | pending |
| 4 | Headless boot reaches `nnMain` | **done** (`logs/boot-null-05/06`) |
| 5 | Engine init (heap, NVN device, romfs, UI) | **done**: heap cb000000, NVN 55/15, romfs, audio renderer, HID, save mount, 6.7k init_array entries |
| 6 | First frame, headless | **done**: `logs/boot-null-13`: 5,424 presents, **60.0 fps**, 1920x1080, 5,401 command lists, no crash |
| 7 | First frame, Vulkan window | **done**: `logs/vk-1.log`, 3,015 frames, 1920x1080 window |
| 8 | Title / 2D UI at 720p60 | **done in the same run**: intro cutscene and PlayerMake UI render with text |
| 9 | First 3D scene (PlayerMake / tutorial) | **PlayerMake 3D done**: skinned Inkling, clothing UI, legible text (`frames/vk-01`); tutorial next |
| 10 | Plaza / match scene | pending |

## Environment facts

- Host work dir: `/mnt/SSD Storage/Games/Ports/Switch/Splat3/Work` (mounted at `/w` in containers).
- Runtime repo (this): `Work/port/Splat3-Recomp` — GitHub `azlekayn/Inksplash3-recomp`, Forgejo `wingrepo/Splat3-Recomp`.
- Game data: `Work/data/{exefs,romfs,img,funcs}` (never committed).
- Generated C++: `Work/gen/{main,sdk,...}`; objects: `Work/obj`; builds: `Work/build-*`.
- Generator venv: `Work/toolchain/venv` (capstone 5.0.9).
- Container: `localhost/engage-linux-build:latest`; `CC=clang CXX=clang++` must be set.

## Splat3 target facts (measured)

- Target executable: v11.3.0 update, SDK **20.5.22**, NVN 20.5.22; 3 modules only (`main`, `sdk`, `rtld`).
- `main`: 271,721 FDE functions, 20.59 M instructions, image 102,733,824 B, build id `28c4287aee36f7499da60f3e68b54c70`.
- `sdk`: 32,673 functions, 1.67 M instructions, image 12,929,536 B.
- `main` imports 1,210 (Splat2: 784); 353 exact-name HLE, 266 Splat2-known names, 591 new; 98.8% live in `sdk`.
- NVN: 455/536 proc names already implemented in the runtime's 460-proc layer.
- No `subsdk*`: NVN and `nn::vi` are inside `sdk`; the two-module loader fits.
- Full analysis: `docs/SPLAT2_TO_SPLAT3.md`.

## Data facts

- RomFS: 22,857 files, 10,058,171,877 B, 127 dirs, 33 top-level. `Layout/` 593, `Model/` 3,287, `Sound/` 1,604, `Pack/` 8,210.
- Asset compression is **Zstandard** (`.zs` magic `28 b5 2f fd`), not Yaz0 as in Splat2. `Pack/Params.pack.zs` -> SARC, `Model/*.bfres.zs` -> FRES.
- No `subsdk*`; `main.npdm` lists 49 services, 1 MiB stack, 3 CPU cores.
- Extraction method and re-run recipe: `docs/EXTRACT_REPORT.md`, `scripts/extract/`.

## Boot findings (v11.3.0)

- Modules use **`DT_RELR`** packed relative relocations (528,692 in main, 15,013 in sdk). The
  runtime ignored them, so every `.data` pointer kept its link-time base-0 value and
  `nninitStartup` died on the first dereference. Implemented in `rt_main.cpp relocate()`.
- `HeapSize/Product.Nin_NX_NVN.HeapDef.byml.zs`, `RegionLangMask.txt`,
  `AddressTable/Product.b30...`, `RomConfig/NormalProduct...` all open from the merged romfs.
- NVN: device created, `DeviceGetInteger(0)=55`, `(1)=15` accepted; pools, command buffers and
  draw records appear. The headless null backend records but does not present.
- Current gap: executed instructions the generator lacks (`frintn v1.4s` first). Use
  `scripts/regen.sh <module>` (incremental: only changed TUs are rewritten) + rebuild.

## Boot sequence fixes (in order found)

1. `DT_RELR` (528,692 main / 15,013 sdk) — implemented in `rt_main.cpp relocate()`.
2. `frintn` + the rest of the executed generator gaps (see the gen commit).
3. SDK 20 internal critical section owner id: `ThreadType+0x1B0` must be non-zero and unique, or
   every fresh mutex reads as "locked by the current thread" and `pthread_mutex_lock` aborts.
4. TPIDR_EL0 thread-local header: `[tpidr+0x10]` (thread id used by the SDK's thread-local allocator)
   must be set when the runtime creates a context; zero matched a zeroed resource owner and
   dereferenced a null per-thread cache (`main 0x383020`).
5. Service namespaces with lifted bodies that need `sm:`/service objects stubbed: `pdm`, `ec`,
   `album`, `ngc`, `swkbd`, `npln` added to `kService` (`pdm` crashed in QueryApplicationPlayStatistics).
6. AES instructions (`aese/aesd/aesmc/aesimc`) implemented byte-wise in `rt.h` for lifted
   `nn::crypto` AES-256-CTR (save/crypto path).

## Headless 60 fps run (boot-null-13)

90 s, `TOTK_LIFT_ALL=1`, null backend: 5,424 presents, steady `60.0 fps`, 1,920x1,080
format 0x25, 5,401 command lists. 92 UNIMPL imports in total, all telemetry/audio/account
(account network availability, prepo PlayReport, one audio voice-slot call). No layout files
opened in this window's trace (fs trace capped): the frames are the startup/logo sequence,
which is the analogue of Splat2's "title 2D UI at 720p60" milestone.

## Vulkan run vk-1 (first windowed session)

100 s, 1,920x1,080 window, Intel/AMD via Vulkan, `TOTK_PRESS="A@240+120x300"`:
- intro cutscene (night sky/horizon) -> PlayerMake character creation, 3,015 frames,
  ~32.5 fps average, 32,602 draws, 3,640 passes, 1.5 GB uploads.
- the skinned 3D Inkling, clothing swatches and UI text all render correctly; the only
  visual oddity is the dark environment (intro lighting), not missing geometry.
- memory: RSS 10.6 GB, swap 6.6 GB, command shadows 113 GB cumulative -> the known
  Splat2 renderer leak is present here too; needs bounding before long sessions.
- 186 draws skipped on "program not built" (shader compile budget), 0 pipeline errors.

## Native Windows port (local v11.2.0 test)

- Native x64 null and Vulkan executables both build with Visual Studio Clang; the Vulkan build
  includes the Win32 surface shim. The current 13-test Windows build/runner/shader/HID suite passes.
- The v11.2.0 lift has 272,489 main functions / 43.40 M instructions and 32,676 SDK functions /
  1.62 M instructions; the reports list 817 main and 3,728 SDK unhandled instructions.
- The local NCA extractor had the patch/base order reversed. It now opens the base NCA with
  `baseNca.OpenFileSystemWithPatch(updateNca, ...)`. The corrected v11.2.0 merge contains 22,837
  files / 10,056,599,109 B / 126 directories and includes
  `System/HeapSize/Product.Nin_NX_NVN.HeapDef.byml.zs` (Malloc 34,816 KiB). The incomplete merge
  had only 7,199 files and fell back to a 32 MiB Malloc heap, below the game's 34 MiB minimum.
- With matching v11.2.0 ExeFS and corrected RomFS, the native null run lasted 60 s, reached
  PlayerMake and recorded 1,882 presents. The Vulkan windowed run reached PlayerMake and recorded
  over 2,100 presents in 60 s, settling near 60 FPS after shader compilation. Both smoke tests
  were intentionally terminated by the runner timeout; neither hit the earlier heap abort.
- The Maxwell GLSL translator now supports the LEA.HI register form that caused earlier fragment
  shader translation failures. In a new 60 s Vulkan run, no unknown-instruction shader failures
  appeared; steady frame samples showed 0 skipped draws (430 draws were skipped during warmup
  before their programs were ready), at about 60 FPS. The Vulkan backend now records precise
  samples-passed queries for NVN ResetCounter/ReportCounter rather than fabricated counts. A 45 s
  validation completed 39,041 reports, including zero and nonzero results, without Vulkan validation
  errors; a committed-heap range check keeps result publication fast. PlayerMake captures at frames
  1800/2100/2400 show readable text and no purple floor pool beneath the avatar. Consecutive-frame
  captures at 1861-1876 had no measured translation in the stationary HUD/world regions, unlike the
  pre-fix reference sequence. The user confirmed the avatar is no longer stuck in ink, but reported
  darker shadows. Wall-paint appearance and gyro aiming still need verification; the subsequent
  newest-first 17-sample HID history correction has compiled but has not been gameplay-tested.
- This is not the v11.3.0 Linux test above and does not establish complete gameplay compatibility.
  The lift still reports unhandled instructions/imports; logs, lifted code, and game data remain
  outside the repository.

## Native Windows v12.0.0 update

- Verified patch title `0100C2500FC20800`, version `2818048`, and merged it with the matching base
  in a separate external work tree. RomFS: 22,865 files / 10,060,103,983 B / 126 directories.
- Regenerated both modules: main 272,586 functions / 43,413,637 instructions / 617 TUs; SDK 32,690
  functions / 1,616,719 instructions / 27 TUs. Unhandled instruction counts remain 817 and 3,728.
- Native Vulkan build succeeded: 648 generated objects plus 33 runtime objects, producing a
  PE32+ AMD64 executable. All 13 Windows build/runner/shader/HID-calibration tests pass.
- At the user's request, the v12 executable was launched interactively and reached PlayerMake; the
  session exited normally after 105.5 seconds and logged about 60 FPS after warm-up. A later 86.3 s
  run loaded Pro Controller factory IMU calibration and delivered batches of 16 live six-axis samples.
  The user reported gyro response was better; residual drift is not confirmed eliminated. These
  smoke runs do not establish full v12 gameplay compatibility; v11.2 results above do not carry over.
