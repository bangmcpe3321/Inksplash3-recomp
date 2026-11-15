# Splat3-Recomp

Static recompilation of **Splatoon 3** (base title `0100C2500FC20000`, USA v65536) to native
Linux x86-64, built on the MIT-licensed [totk-recomp](https://github.com/Muhammad1259-sigma/totk-recomp)
runtime and NVN-on-Vulkan renderer.

This is the sibling project of `Splat2-ReTotK`. The runtime baseline under `baseline/totk-recomp/`
is the same vendored tree plus every game-agnostic fix made while getting Splatoon 2 to boot,
render its title screen, PlayerMake and the tutorial 3D room (see `docs/`).

## Status

See [`STATUS.md`](STATUS.md). Nothing is claimed to work until a logged run shows it.

## Layout

| Path | What |
|---|---|
| `baseline/totk-recomp/` | vendored runtime: recompiler, HLE, NVN/Vulkan backend (MIT) |
| `docs/` | provenance diff, adaptation plan, measurements |
| `scripts/` | container build/run wrappers (host and in-container modes) |

Game data (ExeFS, RomFS, keys, saves), generated C++ and build output live **outside** this
repository, under the work directory, and are never committed.

## Build and run

The build container is `localhost/engage-linux-build:latest`; the work tree is mounted at `/w`
(the host path contains a space). Host-side wrappers:

```sh
scripts/build-null.sh          # headless binary   -> /w/build-null/totk
scripts/build-vk.sh            # Vulkan window     -> /w/build-vk/totk
scripts/run-null.sh            # headless boot, log -> /w/logs/
```

The generated C++ comes from `scripts/gen_all.sh` (in the work tree, not this repo).

## Legal

No game code, game data, keys or saves are distributed here. The runtime baseline is MIT and
keeps its upstream attribution. You need your own legally dumped copy of the game.
