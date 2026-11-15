# Status

Updated as milestones land. Every claim links to a log or a measurement.

## Milestones

| # | Milestone | State |
|---|---|---|
| 0 | Runtime baseline hardened + buildable | **done** (`docs/BUILD_SMOKE.md`) |
| 1 | Splat3 ExeFS/RomFS extracted | pending |
| 2 | NSO images + function tables | pending |
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
