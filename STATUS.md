# Status

Updated as milestones land. Every claim links to a log or a measurement.

## Milestones

| # | Milestone | State |
|---|---|---|
| 0 | Runtime baseline hardened + buildable | **done** (`docs/BUILD_SMOKE.md`) |
| 1 | Splat3 ExeFS/RomFS extracted | **ExeFS done** (v11.3.0 update; base v1.0 NCA has no code); RomFS/BKTR pending |
| 2 | NSO images + function tables | in progress |
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
- Runtime repo (this): `Work/port/Splat3-Recomp` — Forgejo `wingrepo/Splat3-Recomp`.
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
