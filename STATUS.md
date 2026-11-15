# Status

Updated as milestones land. Every claim links to a log or a measurement.

## Milestones

| # | Milestone | State |
|---|---|---|
| 0 | Runtime baseline hardened + buildable | **done** (`docs/BUILD_SMOKE.md`) |
| 1 | Splat3 ExeFS/RomFS extracted | **done** (v11.3.0 update; BKTR-merged RomFS 22,857 files / 9.37 GiB) |
| 2 | NSO images + function tables | in progress |
| 3 | `main` + `sdk` lifted, unhandled triaged | `sdk` done: 32,676 fns / 4,760 unhandled; `main` running |
| 3 | `main` + `sdk` lifted, unhandled triaged | pending |
| 4 | Headless boot reaches `nnMain` | pending |
| 5 | Engine init (heap, NVN device, romfs, UI) | pending |
| 6 | First frame, headless | pending |
| 7 | First frame, Vulkan window | pending |
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
