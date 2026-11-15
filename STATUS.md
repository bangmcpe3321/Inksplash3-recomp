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
| 6 | First frame, headless | **done**: `present #5 tex 1920x1080` in `logs/boot-null-11` (null backend records, no output) |
| 7 | First frame, Vulkan window | next after the AES rebuild |
| 8 | Title / 2D UI at 720p60 | pending |
| 9 | First 3D scene (PlayerMake / tutorial) | pending |
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
