# Inksplash3-recomp

Native x64 Windows static recompilation of **Splatoon 3 v12.0.0** (base title
`0100C2500FC20000`, update title `0100C2500FC20800`), built on the MIT-licensed
[totk-recomp](https://github.com/Muhammad1259-sigma/totk-recomp) runtime and NVN-on-Vulkan renderer.
The Windows port is experimental; see [`STATUS.md`](STATUS.md) for its tested scope.

This work builds on [azlekayn's Inksplash3-recomp](https://github.com/azlekayn/Inksplash3-recomp)
and the game-agnostic runtime fixes developed while getting Splatoon 2 to boot, render its title
screen, PlayerMake and tutorial 3D room (see `docs/`). The runtime baseline under
`baseline/totk-recomp/` retains its upstream MIT attribution.

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

## Existing Linux build scripts

The repository retains its original Linux container workflow (`localhost/engage-linux-build:latest`)
for the Linux build. Its work tree is mounted at `/w` (the host path contains a space); wrappers:

```sh
scripts/build-null.sh          # headless binary   -> /w/build-null/totk
scripts/build-vk.sh            # Vulkan window     -> /w/build-vk/totk
scripts/run-null.sh            # headless boot, log -> /w/logs/
```

The generated C++ comes from `scripts/gen_all.sh` (in the work tree, not this repo).

## Native Windows port

An experimental native x64 Windows build is available using Visual Studio Clang, the Windows
SDK, and the Vulkan SDK. See [`docs/WINDOWS.md`](docs/WINDOWS.md) for prerequisites, isolated
work-directory commands, and the current verification limits. You must provide your own matching
Splatoon 3 ExeFS and merged RomFS; game files, keys, generated code, and build output stay outside
the repository. Windows support is not yet a claim of complete gameplay compatibility.

## Legal

No game code, game data, keys or saves are distributed here. The runtime baseline is MIT and
keeps its upstream attribution. You need your own legally dumped copy of the game.
