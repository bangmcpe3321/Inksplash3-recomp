# Splatoon 3 on the hardened totk-recomp runtime — adaptation plan

Target: **Splatoon 3, program id `0100C2500FC20000`** (USA base NSP `[0100C2500FC20000][v65536]`,
3.32 GB; update `0100C2500FC20800` = v11.3.0; DLC `0100C2500FC21064/65/66`).
Port tree: `Work/port/totk-recomp` (upstream `a32f709` + the Splat2 runtime/generator fixes, see
`UPSTREAM_DIFF.md`). Pristine upstream stays at `Work/port/upstream-totk-recomp`.

This plan is a checklist, not a design: every item names the file:line that has to change and where the
value comes from. Values marked **[measure]** cannot be known until the NSOs are parsed (step 1), because
they are properties of the game binary, not of the runtime.

---

## 0. Build and run commands (work today, before any Splat3 change)

```bash
# host (WORK is mounted at /w inside the container; the host path has spaces)
Work/port/scripts/build-null.sh -j 8          # -> Work/build-null/totk
Work/port/scripts/build-vk.sh   -j 8          # -> Work/build-vk/totk
Work/port/scripts/run-null.sh                 # 240 s headless, log in Work/logs/run-null-N.log
```

They wrap exactly:

```bash
CC=clang CXX=clang++ python3 tools/recomp/mkninja_linux.py --null \
    --root /w/port/totk-recomp --gen /w/gen/main --obj /w/obj --deps /w/deps \
    --syslib /w/syslib --sysinc /w/deps/libshaderc/include \
    --out /w/build-null --gen-opt -O1 --gen-pool 4
ninja -C /w/build-null
```

`CC=clang CXX=clang++` is mandatory (`mkninja_linux.py:57` defaults to `clang-19`, which does not exist in
the image). The generator needs **capstone**, which is *not* in `localhost/engage-linux-build:latest`:
`python3 -m pip install capstone` at the start of a build session (or bake it into the image).

Sanity state of the imported tree: `ninja -C /w/build-smoke objs-rt` compiles all **27** runtime + null-backend
objects with `clang 22.1.8`, zero diagnostics (see `BUILD_SMOKE.md`). Nothing is game-specific at that layer.

---

## 1. Data: extract, disassemble, generate (Splat3 does not exist here yet)

```bash
# 1.1 exefs + romfs out of the base NSP (never into the repo; Work/data is gitignored territory)
#     Splat2 used hacpack/nut-style extraction into work/base-nsp/{exefs,romfs}; the same layout is expected:
#     exefs = main, sdk, subsdk0..subsdkN, rtld, main.npdm
# 1.2 NSO -> flat image + JSON (records the build id and the MOD0/bss layout)
for m in main sdk subsdk0 subsdk1 subsdk2 subsdk3 subsdk4; do
  [ -f Work/base-nsp/exefs/$m ] && python3 tools/nso.py Work/base-nsp/exefs/$m Work/img/$m
done
# 1.3 function starts (FDE table + BL targets + relocs) -> what gen.py consumes
for m in main sdk subsdk0 subsdk1 subsdk2 subsdk3; do
  [ -f Work/img/$m.bin ] && python3 tools/funcs.py Work/img/$m Work/img/funcs_$m.json
done
# 1.4 lift
python3 tools/recomp/gen.py Work/img/main Work/img/funcs_main.json Work/gen/main
python3 tools/recomp/gen.py Work/img/sdk  Work/img/funcs_sdk.json  Work/gen/sdk --prefix s --plt-all
```

Record from 1.2/1.3 (this is the whole **[measure]** table):

| What | Where it comes from | Feeds |
|---|---|---|
| build id per module | `Work/img/<m>.json` (`build_id`) | attribution only — **nothing in the runtime hardcodes it** |
| module **count and names** | `Work/base-nsp/exefs/` listing | `rt_main.cpp:726-727` (2-module loader) |
| **main image size incl. bss** | `main.json` MOD0 `bss_end` (rounded up in `load_module`) | `IMP_FAKE` spacing, `rt_main.cpp:50` |
| **main import count** | `gen/main/imports.cpp` / `g_imp_count`, printed by `[ld] ftab … imports` | `kMaxImports`, `hle.h:5` |
| **NVN API major/minor** | see §3 | `nvn/objects.c:17`, or `TOTK_NVN_INT=0=N` as a stopgap |
| **heap request** | the game's own `nn::os::SetMemoryHeapSize` call (log line `[os] SetMemoryHeapSize(...)`) | `hle_os_mem.cpp:19-20` sanity numbers |
| **lift coverage per module** | `gen.py` report / `report.json` unhandled counts | whether subsdks must be lifted |

Milestone 0 is done when `gen/main` + `gen/sdk` exist and `build-null.sh` links a `totk` binary. There is no
guarantee the link succeeds before step 2.1 (the `f_7a83e4` weak symbol was the Splat2 version of this class
of breakage — one game's `--hook` symbol missing at link time).

---

## 2. Game definition fields

There is no game-definition struct in this baseline (the newer `nx/` runtime in the Splat2 repo has
`port/platform/game_sp2.cpp` with `{id, title, program_id, …}`; that pattern is the right end state, but it
lands later). For now each field is a `static const` or an env default in the runtime.

| Field | Current value | Where | Splat3 value |
|---|---|---|---|
| Program / title id | `0x0100F2C0115B6000` (TotK) | `launcher/launcher.cpp:59` | **`0x0100C2500FC20000`** |
| Main image base | `0x7100000000` | `rt_main.cpp:48`, and `FTAB_BASE` `rt.h:437` (must stay equal) | keep `0x7100000000` (Horizon convention; Splat2 uses it too) |
| SDK image base | `0x7180000000` | `rt_main.cpp:49` | keep `0x7180000000` **[measure]** — must stay above `main_base + main size` |
| Import fake-code window | `+0x5000000` from `MAIN_BASE` | `rt_main.cpp:50` | **[measure]** must exceed the main image size incl. bss; if Splat3's `main` is bigger, raise it (and the 168 MB `g_ftab` allocation grows with it, `rt_main.cpp:732`) |
| Max main imports | `1024` | `hle.h:5` | **[measure]** Splat2 used 784 of 1024; Splat3 is a bigger game |
| Max sdk imports | `16384` | `rt_main.cpp:41` | keep unless the sdk PLT is bigger |
| NVN API major / minor | `55` / `15` (TotK) | `nvn/objects.c:17` | **[measure]** (`TOTK_NVN_INT=0=N` stopgap, see §3) |
| NVN device limit table | measured-on-Switch values, entries 0..64 | `nvn/objects.c:16-41` | keep; Splat3 may query >64 (`[nvn] DeviceGetInteger(%u)` log names the first one it misses) |
| Guest heap "total" / "program" | `0xD0000000` / `0x5000000` | `hle_os_mem.cpp:19-20` | **[measure]** only feeds `MemoryInfo`; the real heap comes from the game's `SetMemoryHeapSize` |
| Heap fixed address | `0x1000000000` | `hle_os_mem.cpp:134` | keep (shader/pipeline caches bake it into translated shaders) |
| Module loader | `main.bin` + `sdk.bin` only | `rt_main.cpp:726-727` | see §4 |
| Lift whitelist (default `TOTK_LIFT`) | `fontll, util, image, prepo` | `rt_main.cpp:751-757` | re-evaluate per API surface Splat3 actually calls |
| Lift-all service exclusions | 48 namespace prefixes | `rt_main.cpp:789-802` | keep; extend from observed `[ld] lift-all` failures (Splat3's online surface may add e.g. `_ZN2nn3ngc`) |
| romfs root | `TOTK_ROMFS`, else `<exe>/data/romfs` | `hle_fs.cpp:39` | keep (mount names are arguments, not hardcoded — `hle_fs.cpp:222-227`) |
| User data root | `$XDG_DATA_HOME/totk-recomp/userdata` | `hle_fs.cpp:37`, `hle_osd.cpp:236` | rename to `splat3-recomp` (cosmetic, but it is what saves land in) |
| Title-phase heuristics | `"TitleScene"`, `"OpeningField"`, `"Banc/"` | `hle_fs.cpp:129-143` | replace with Splat3's own stage/asset names **[measure]** (Splat3's UI is `Layout/…`, its stages are `Mush/…`-style) |
| Audio renderer layout | SDK **3.x** struct (Splat2-era) | `hle_audio.cpp:561-580` | Splat3 is a 2022 title on a much newer SDK — almost certainly the **5.x** layout the upstream baseline had. Needs a per-game switch, not a global rewrite (see §5.1) |
| Shared font (`nn::pl`) | state 1, never-null buffer | `hle_fs.cpp:420-449` | keep: both Splat games use `nn::pl` with a romfs fallback |
| HID PC mapping | WASD/mouse/Shift/F/LMB, `TOTK_PC_INPUT` | `hle_hid.cpp:930-991` | keep for Splat3 (the mapping is Splatoon-shaped already) |
| Input sequencer | `TOTK_PAD_FILE` 24-byte `PAD1` | `hle_hid.cpp:1003-1025` | keep |

---

## 3. NVN API major is unknown until `main`/`subsdk0` are parsed

`nvnDeviceGetInteger(dev, 0)` is the NVN **API major**, `[1]` the minor. TotK needs 55/15
(`nvn/objects.c:17`), Splatoon 2 needs 53 (`TOTK_NVN_INT="0=53"`, Splat2 `BOOT_SMOKE_NULL.md:82`). The game
compares the value in its own code and aborts if it is wrong, so:

1. Default stays 55 → the run either passes device init or dies right after `[nvn] bootstrap`.
2. Find the compare: in `main`'s nvn-device-init path, `nvnDeviceGetInteger(dev, 0)` is followed by
   `cmp w0, #major` / `b.ne`; in `subsdk0` (the `nvn` library) the same constant appears as the library's
   own version. `objdump`/capstone over `Work/img/main.bin` at the call site is enough — Splat2 found 53
   this way.
3. Sweep as a fallback: `TOTK_NVN_INT="0=52"` … `0=65`, one 30-second headless run each; the wrong value
   aborts immediately, so a sweep is cheap.
4. **Then move it into the tree** — Splat2's own note (`NEXT_PROMPT.md:79`) says it "belongs in the game
   definition, not an env var". Best place today: a `static const u32 kGameNvnMajor` next to `[0]` in
   `nvn/objects.c:16`, with `TOTK_NVN_INT` kept as the override.

The same applies to `[1]` minor (TotK: 15) and to any `DeviceGetInteger` index > 64: the runtime logs
`[nvn] DeviceGetInteger(%u) -> … (beyond LASRecomp table)` and returns 0/`TOTK_NVN_CAPS1`, i.e. any feature
query Splat3 makes beyond the table is a silent "no". Collect those from the first run and fill the table.

---

## 4. 6-module game, 2-module loader

Splat2's exefs has `main, sdk, subsdk0..subsdk3` (6 modules; measured coverage in its `HANDOFF.md`: subsdk0
is `nvn`, subsdk1 `nn::vi`, subsdk2/3 small). The runtime loads **two**:

```c
load_module(g_main, dir + "/main.bin");   // rt_main.cpp:726
load_module(g_sdk,  dir + "/sdk.bin");    // rt_main.cpp:727
```

Everything else is served by HLE (`hle_nvn.cpp`, `hle_vi.cpp`) and by lifting the sdk's real bodies under
`TOTK_LIFT_ALL`. `gen.py` is single-module (`gen.py <prefix> <funcs.json> <outdir> [--prefix s]`), and the
build assumes exactly `gen/main` + `gen/sdk` (`mkninja_linux.py:76`).

So there are three options for Splat3, in order of cost:

1. **Do nothing** (expected to be right first): Splat3 also splits `nvn`/`vi` into subsdks; the existing
   `hle_nvn`/`hle_vi` cover them. This is what makes the first boot reach the game's own `nnMain`.
2. **Lift one subsdk into the `sdk` slot**: if a subsdk holds code the game needs and HLE cannot fake
   (e.g. Splat3's `nn::vi` display pipeline or a big video decoder), generate it with `--prefix s` into
   `gen/sdk` **after** the real sdk — both become one address space at `SDK_BASE`, which is wrong unless the
   two images are disjoint and the offsets are rebased. Not recommended.
3. **Extend the loader** (the real fix, only if 1 fails): a module table
   `{name, base, path}` and per-module ftab/plt binding, mirroring what `rt_main.cpp` does for `g_sdk`.
   Reserve `0x800000000` (32 GB) between bases, as Splat2's coverage table does:
   `main 0x7100000000, sdk 0x7180000000, subsdk0 0x7200000000, subsdk1 0x7280000000, …`.
   This touches `Module`, `resolve()`, `relocate()`, `lifted()`, `call_ind` and `rt_call_indirect`, and both
   `gen.py` outputs and `mkninja_linux.py`'s source sets. Budget it as a separate task.

Check before choosing: does `main`'s import table contain symbols that only a subsdk exports (e.g.
`nvnBootstrapLoader`)? In Splat2, `nvnBootstrapLoader` is HLE'd (`hle_nvn.cpp:84`), which is why 2 modules
suffice. If Splat3 imports anything not HLE'd and not in `sdk`, the run stops at `[ld] … unresolved`/`imp_*`
and option 3 becomes mandatory for that module.

---

## 5. Known Splat2-specific assumptions in the imported tree

Everything below is true of the code *as imported*. Most are harmless (they are just values), but each one is
a place where Splat3 silently differs.

### 5.1 `hle_audio.cpp` — the 3.x audio layout replaced the 5.x one **for the same symbol**

`hle_audio.cpp:561-587`: `InitializeAudioRendererParameter` now writes the **SDK 3.x** struct
(`p[0]=48000, p[1]=240, p[2]=1, p[4]=1, p[5]=1`) and `IsValidAudioRendererParameter` validates it
(`hle_audio.cpp:575-586`). Upstream (TotK) wrote the 5.x shape (`p[4]=24`). The mangled name is identical
across SDKs — only the structure changed — so this is a **global** behaviour switch, not an added overload.
Splat3 (2022, SDK 13+) very likely needs the 5.x shape back. Fix by making the layout an explicit choice
(`TOTK_AUDIO_LAYOUT=3|5`, default 5 for Splat3) rather than by reverting the Splat2 code.

The pre-5.x **name** spellings are additive and safe (`ListAudioDeviceName`, `GetActiveAudioDeviceName`,
`GetAudioDeviceOutputVolume`, `SetAudioDeviceOutputVolume`, `GetActiveChannelCount`,
`AcquireAudioDeviceSwitchNotification`, `hle_audio.cpp:823-836`), as is `GetMemoryPoolState`
(`hle_audio.cpp:806-811`).

### 5.2 `hle_os_mem.cpp` — heap numbers are Splat2's, and `kAvailable` is TotK's

`hle_os_mem.cpp:19` `kAvailable = 0xD0000000` (3328 MB), `:20` `kProgram = 0x5000000`. The `MemoryInfo`
layout is now real (`+0x10/+0x18/+0x20`, `hle_os_mem.cpp:28-41`): TotK reads `{+0,+8}`, Splat2 reads
`{+0x10,+0x18}`. Both work; the *numbers* are guesses ("main image + stacks, rough"). Splat3's `nnMain` will
call `SetMemoryHeapSize` with its own size (Splat2 used `0xcb000000` = 3248 MB) and then reserve
`heap_size - used` — with `kAvailable`/`kProgram` wrong the computed free size is wrong, and a zero size used
to trap (fixed in `hle_os_mem.cpp:170-172`, keep that). Verify from the first run's
`[os] SetMemoryHeapSize(...) -> heap ...` and `[os] AllocateMemoryBlock(...)` lines.

`hle_os_mem.cpp:134` pins the guest heap at `0x1000000000`; keep it (translated shaders bake the base in).

### 5.3 `rt_main.cpp` — bases, import window, module count, lift lists

* `rt_main.cpp:48-50`: `MAIN_BASE 0x7100000000`, `SDK_BASE 0x7180000000`, `IMP_FAKE = MAIN_BASE+0x5000000`.
  `FTAB_BASE` (`rt.h:437`) is a `#define` that must track `MAIN_BASE`; `g_ftab_n` is derived from the window
  (`rt_main.cpp:732`), i.e. 20.97 M entries ≈ 168 MB reserved **and committed** at startup. Splat2's `main`
  is 47.9 MB, so `0x5000000` fits; a bigger Splat3 image would silently overlap the import window.
* `rt_main.cpp:726-727`: exactly two modules (see §4).
* `rt_main.cpp:751-757`: the default lift whitelist is TotK's text stack (`fontll`, `util`, `image`, `prepo`)
  plus Splat2's prepo `Add` exclusions (`t a8a034f`). Splat3's `nn::fontll`/`util` usage will differ; the
  `prepo` exclusions should stay (telemetry is not wanted anyway).
* `rt_main.cpp:789-802`: the `TOTK_LIFT_ALL` service exclusion list. Generic, but it was grown from
  Splat2's boot path (friends/pia/nex/time/err/diag were added for Splat2's `ConnectToNamedPort("sm:")`
  aborts). Splat3 has a bigger online surface (`nn::nex`, `nn::pia`, `nn::ldn`, `nn::ngc`); expect the same
  class of abort and extend the list, not the HLE.
* `rt_main.cpp:717-720`: `TOTK_IMG` / `<exe>/data/img` and the `main.bin`/`sdk.bin` names — fine.

### 5.4 `nvn/objects.c` — API version and device constants are TotK's

`nvn/objects.c:17` `[0]=55, [1]=15` with the comment "(TotK: requires major == 55, minor >= 15; LASRecomp had
53.207)". Splat2 overrides at run time with `TOTK_NVN_INT="0=53"`. See §3. Entries 50-64 are *measured* on a
Switch in SMO research; entries 0-49 are the LASRecomp table, i.e. plausible-but-not-verified — a Splat3 aborts
on a wrong limit (Splat2's `[57] LINEAR_RENDER_TARGET_STRIDE_ALIGNMENT` bug padded 32×32 RGBA8 rows to 4096 B
and wrote past an 8 KB pool) will look like a graphics corruption, not like a version problem.

### 5.5 `launcher/launcher.cpp` — TotK's title id

`launcher/launcher.cpp:59` `kTitleId = 0x0100F2C0115B6000`, used for the icon (`:398`), the dump check
(`:1004`) and the version step (`:1117`). Splat3: `0x0100C2500FC20000`. The launcher also assumes a
`C:/totk`-shaped dump layout on Windows and an `<exe>/data` layout on Linux; it is a separate, optional
front-end (the game runs without it), so this is a "before shipping", not a "before boot" item.

### 5.6 `hle_vi.cpp` — a TotK main address

`hle_vi.cpp:212` `void f_7a83e4_orig(Ctx*) __attribute__((weak));` and `:213-218` the frame-rate/pacing hook.
`0x7a83e4` is a TotK `main` RVA passed via `gen.py --hook 7a83e4`; for Splat3 the symbol is absent, the hook
returns immediately (`:214`), and `TOTK_SPEED_FIX` does nothing. Harmless now (that is the whole point of the
weak declaration) — but note that the frame-pacing fix it implements is then **not** active for Splat3.

### 5.7 `hle_hid.cpp` — scripts and mapping tuned to TotK's menus

* `hle_hid.cpp:61` `TOTK_AUTO_NEWGAME` = `"DOWN@350,A@420,UP@480,A@540"` — TotK's title menu. Splat2 found it
  does *not* dismiss Splatoon's title (`HANDOFF.md`); a repeating `A` (`TOTK_PRESS="A@120+30x300"`) does.
  Keep the mechanism, replace the script.
* `hle_hid.cpp:44-80` `TOTK_PRESS`: generic frame-scripted buttons; the addresses are not game-specific.
* `hle_hid.cpp:930-991` PC mapping (`TOTK_PC_INPUT`, default on) — Splatoon-shaped already (ZL squid, ZR
  fire, B jump, R bomb). Keep.
* `hle_hid.cpp:993-1031` `TOTK_PAD_FILE` virtual pad + `tools/input_sequencer.py`: generic, keep; the
  *scenarios* are game data.
* `hle_hid.cpp:285-360, 691-800` gyro/IMU (`TOTK_GYRO`, `TOTK_DECK_IMU`) were built for TotK's motion
  puzzles; Splat3 uses gyro aiming, so the dead zone/scale (`:285` "1.3 dps dead zone") needs its own tuning.

### 5.8 `hle_fs.cpp` — title/opening heuristics keyed on TotK asset names

`hle_fs.cpp:129-143`: `rel.find("OpeningField")`, `rel.find("TitleScene")`, `rel.find("Banc/")` drive
`g_title_phase` / `g_in_opening`, which in turn drive the skip-opening overlay and the title-phase log. Splat3
has neither of those paths. Also `hle_fs.cpp:25-29/43-44`: the skip-opening and overlay directories
(`data/overlay/skip_opening/romfs`) exist only for TotK's prologue; for Splat3 the whole mechanism is dead
weight and should be replaced by the game's own scene names (or deleted).

### 5.9 Smaller ones

* `hle_osd.cpp:236` writes settings/saves under `…/totk-recomp` — cosmetic, but it is where Splat3 saves land.
* `hle_osd.cpp:219-227` graphics-menu defaults (`TOTK_RES_SCALE`, `TOTK_FPS=0` = "30 (the game's own)",
  `TOTK_SHADOW_SCALE`): Splat3 is a 60 fps title at 1080p docked; the "30" default and the resolution presets
  are TotK's.
* `hle_nvn.cpp` comment strings and `nvn/objects.c:5` reference `../kernel.h` (LASRecomp) — no behaviour.
* `runtime/rt_main.cpp:521` refers to `totk.exe.map` / `tools/hostprof.py` — profiling only, Windows path.
* `hle_hid.cpp:865` restarts `<exe>/totk` for the skip-opening flow — TotK only.
* No hardcoded build id anywhere in the runtime (`grep build_id runtime/` is empty); the build id only shows
  up in `Work/img/<m>.json` for attribution. Good — nothing to change there.

---

## 6. Which `TOTK_*` variables stop applying

Renaming is *not* a prerequisite for booting; the list exists so the next person does not debug a variable
that means nothing for Splat3. (113 `TOTK_*` names total in the tree; `WM_TOTK_TITLE` is a Win32 message id, not an env var.)

**No meaning at all for Splat3 — remove or ignore**

| Variable | Why |
|---|---|
| `TOTK_SKIP_OPENING`, `TOTK_SKIP_DIR` | TotK's prologue overlay (`hle_fs.cpp:25-29`, `:47`) |
| `TOTK_AUTO_NEWGAME` | TotK title-menu script (`hle_hid.cpp:61`) |
| `TOTK_OVERLAY`, `TOTK_OVERLAY_OUT` | LayeredFS replacement for TotK test data |
| `TOTK_SPEED_FIX` | hooks TotK `main 0x7a83e4` (`hle_vi.cpp:212-218`) |
| `TOTK_WARMUP*` | TotK shader-warm-up screen and its pacing (`hle_vi.cpp:163`, `hle_osd.cpp:177`) |
| `TOTK_UNLOCK_FPS`, `TOTK_DYNRES`, `TOTK_SCAN_VFR*`, `TOTK_VFR_*`, `TOTK_TEST_DEVLOST` | TotK frame-rate research knobs |
| `TOTK_SHADOW_SCALE`, `TOTK_AA`, `TOTK_SSAA`, `TOTK_UPSCALE`, `TOTK_OUT_SCALE`, `TOTK_SHARPEN` | TotK renderer presets (Splat3 needs its own values) |
| `TOTK_CHEATS`, `TOTK_MODS`, `TOTK_INSTALL`, `TOTK_LAUNCHER_*` | TotK launcher/mod plumbing |
| `TOTK_CLOUDSAVE`, `TOTK_STATE` | launcher state for the TotK install |

**Mechanism keeps working, value must be re-measured**

| Variable | Note |
|---|---|
| `TOTK_NVN_INT` | per-game NVN major; Splat2 needed `0=53` (§3) |
| `TOTK_NVN_CAPS1` | feature-query fallback for `DeviceGetInteger` > 64 |
| `TOTK_LIFT`, `TOTK_LIFT_ALL`, `TOTK_LIFT_ALL_VERBOSE` | lists grown from Splat2's import set (§5.3) |
| `TOTK_HEAP_ADDR`, `TOTK_HEAP_WW`, `TOTK_WW`, `TOTK_MEM_LIMIT_MB` | keep address; write-watch is a per-game texture win |
| `TOTK_ALIAS_IMPORT_MB`, `TOTK_GPU_BO` | VRAM-mirror/per-draw-copy knobs; re-benchmark on Splat3's draw load |
| `TOTK_PRESS`, `TOTK_PRESS_CLOCK`, `TOTK_PAD_FILE`, `TOTK_MOUSE_SENS`, `TOTK_PC_INPUT`, `TOTK_PAD_LABELS` | generic; scripts/mappings are per game |
| `TOTK_GYRO`, `TOTK_DECK_IMU`, `TOTK_RUMBLE`, `TOTK_DS_HAPTICS` | gyro aiming is real in Splat3, retune |

**Keep as-is (paths, debug, plumbing)**

`TOTK_IMG`, `TOTK_ROMFS`, `TOTK_USERDATA`, `TOTK_FONT`, `TOTK_HEADLESS`, `TOTK_AUDIO*`, `TOTK_VOLUME`,
`TOTK_FPS`/`TOTK_FPS_OVERLAY`, `TOTK_RES_SCALE`, `TOTK_FULLSCREEN`, `TOTK_VSYNC_SLEEP`, `TOTK_SLEEP_PRECISE`,
`TOTK_TRACE_FS`, `TOTK_TRACE_STR`, `TOTK_TRACE_MAX`, `TOTK_TRACE_ALL`, `TOTK_IND_LOG`, `TOTK_IMP_STATS`,
`TOTK_EVRING_DUMP`, `TOTK_PROF`, `TOTK_WATCH*`, `TOTK_PRIORITY`, `TOTK_AFFINITY`, `TOTK_NO_SYNTH`,
`TOTK_OSD`, `TOTK_GFX_MENU`, `TOTK_SETTINGS*`, `TOTK_SHOT_*`, `TOTK_PERF*`, `TOTK_PGO*`, `TOTK_TEST_SECONDS`,
`TOTK_INPUT_TICK`, `TOTK_DPAD_30`, `TOTK_NO_FOCUS`, `TOTK_VIEWOFF0`, `TOTK_HANDHELD`, `TOTK_AUX*`,
`TOTK_PRECOMPILE*`, `TOTK_ALIAS_NO_WATCH`, `TOTK_NVN_PROBE`, `TOTK_WARMUP_START`.

The eventual rename (`TOTK_` → `SPL3_` or, better, per-game settings) is a mechanical pass over 113 names;
do it after Splat3 boots, so the upstream diff stays readable.

---

## 7. Order of work and what "done" looks like at each step

| # | Step | Verification |
|---|---|---|
| 1 | Extract base NSP, `nso.py` + `funcs.py` for main/sdk/subsdk* | `Work/img/main.bin` exists, `[measure]` table filled |
| 2 | Generate `gen/main`, `gen/sdk`; `build-null.sh` | links `Work/build-null/totk`; `[ld] ftab N functions, M imports` printed |
| 3 | First headless boot with `run-null.sh` | reaches `nnMain`; the log names the first missing piece (HLE name, NVN version, or a lifted-body trap) |
| 4 | Fill §2 fields (import count, image size, NVN major) | `[ld] … imports (…)` fits `kMaxImports`; device init passes |
| 5 | Boot to title | `[nvn] first present`, then a stable `[vsync]` loop; the fs trace shows `Layout/…`-style opens |
| 6 | Input + newgame | `TOTK_PRESS` script reaches the plaza/tutorial; frame captures via `LARECOMP_SHOT_AT` |
| 7 | Perf pass | draw/pass/upload counts per 2 s, guest-heap copy path (`TOTK_ALIAS_IMPORT_MB`, `TOTK_GPU_BO`) |

Expected first blockers, in order of likelihood (from the Splat2 boot history): a missing `nn::os`/`nn::pl`
entry (HLE it, `hle_svc.cpp`-style), an NVN API-version compare (§3), a `TOTK_LIFT_ALL` service that calls
`ConnectToNamedPort` (§5.3), then a lifted-body trap inside the game's own UI/engine init (crash report +
`TOTK_IND_LOG=1` + `TOTK_TRACE_STR=1`).

---

## 8. Boundaries

* No game data, keys, generated C or built binaries in `port/totk-recomp`. `Work/gen`, `Work/obj`,
  `Work/build-*`, `Work/img`, `Work/base-nsp` all stay outside the repo (its `.gitignore` already excludes
  `gen/`, `data/`, `extract/`, `romfs/`, `exefs/`, `build/`, `*.o`).
* The tree must stay a **clean superset of upstream** — one file per fix, each traceable to a Splat2 commit
  (`UPSTREAM_DIFF.md`). Game-specific behaviour goes behind a switch or into a game-definition file, not into
  a rewrite of a shared symbol (§5.1 is the counter-example to avoid).
* The Splat2 tree is read-only reference. Podman only for builds.
