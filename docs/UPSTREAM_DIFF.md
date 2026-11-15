# What the Splatoon 2 port changed in the TotK runtime

Upstream baseline: `Muhammad1259-sigma/totk-recomp` @ **a32f709** (`null build: stub the warm-up / play-now /
scene-idle / fence-submit symbols`), MIT. Vendored into Splat2-ReTotK at commit `6472ef3`.

This file records, per file, everything the Splatoon 2 port changed on top of that baseline, and which
Splat2-ReTotK commit introduced it. It is the provenance for the commits at the top of
`port/totk-recomp` (`runtime: import hardened Splat2 baseline fixes`, `gen: import Splat2 generator fixes`,
`build: --sysinc …`).

## How this was measured

```bash
# the vendored tree is the baseline (6472ef3) plus later fixes
git -C "$S2" log --oneline -- baseline/totk-recomp
git -C "$S2" log --oneline 6472ef3..HEAD -- baseline/totk-recomp/<file>      # per file
git -C "$S2" diff --stat 6472ef3..HEAD -- baseline/totk-recomp

# file-by-file against a pristine clone of upstream a32f709
diff -rq --exclude=.git port/totk-recomp "$S2/baseline/totk-recomp"

# the one change that predates 6472ef3 (a local edit made while vendoring)
diff port/upstream-totk-recomp/tools/recomp/gen.py <(git -C "$S2" show 6472ef3:baseline/totk-recomp/tools/recomp/gen.py)
```

Result: **12 files differ** from upstream (10 modified + 2 new: `runtime/hle_svc.cpp` and
`tools/input_sequencer.py`), **797 insertions / 21 deletions** — of which the Splat2 port itself accounts
for 546/21 in 11 files and the rest is the sequencer. Nothing else in the baseline was touched — no
build-system rewrite, no new directory, no clang-flag change.

`$S2` = `/mnt/SSD Storage/Games/Ports/Switch/Splat2/work/Splat2-ReTotK`.

## Provenance table

| File | Δ lines | Splat2-ReTotK commits |
|---|---:|---|
| `runtime/hle_svc.cpp` (new) | +169 | `4d1b5a5`, `d26bd39` |
| `runtime/rt_main.cpp` | +111/−6 | `4d1b5a5`, `7977d74`, `96b36ae`, `160380f`, `a8a034f` |
| `runtime/hle_hid.cpp` | +66/−2 | `5ed997b` |
| `runtime/hle_audio.cpp` | +44/−4 | `4d1b5a5` |
| `runtime/hle_os_thread.cpp` | +46 | `4d1b5a5`, `0f5413e`, `618c5c6`, `160380f`, `862561e`, `36b5ddc` |
| `tools/recomp/gen.py` | +51/−5 | `4d1b5a5`, `ce3bc6e` (+ FADDP, added in `6472ef3`) |
| `runtime/hle_fs.cpp` | +32 | `4d1b5a5` |
| `runtime/hle_os_mem.cpp` | +12/−2 | `4d1b5a5` |
| `runtime/rt.h` | +7/−1 | `4d1b5a5` |
| `runtime/hle_vi.cpp` | +4/−1 | `4d1b5a5` |
| `tools/recomp/mkninja_linux.py` | +4 | `5c50b84` |
| `tools/input_sequencer.py` (new) | +251 | `5ed997b` (game-agnostic half) |

Source commits (Splat2-ReTotK `retotk-baseline`):

| Commit | Subject |
|---|---|
| `6472ef3` | retotk: vendor the TotK Linux baseline and record the Splatoon 2 plan (incl. the FADDP fix) |
| `4d1b5a5` | runtime: Splatoon 2 boots to nnMain and its own engine init on the null backend |
| `0f5413e` | runtime: fill in the nn::os entries the boot trace asked for |
| `5c50b84` | readme: describe this port …; Vulkan build links (`--sysinc`) |
| `7977d74` | debug: printable strings from the crashing guest frame; Vulkan window verified |
| `96b36ae` | debug: string-aware trace, and the UI blocker narrowed to the page layout group |
| `d26bd39` | hle: implement `nn::settings::eq(Language, LanguageCode)`; document the layout mechanism |
| `618c5c6` | runtime: record the thread stack base in the guest `nn::os::ThreadType` |
| `160380f` | runtime: nn::os Event HLEs + exclude friends/pia/nex/time/err/diag from lift-all |
| `862561e` | runtime: implement `nn::os::JamMessageQueue`/`TryJamMessageQueue` |
| `a8a034f` | runtime: stop lifting `nn::prepo` `PlayReport::Add` (telemetry abort) |
| `36b5ddc` | runtime: implement the 1-arg `nn::os::ReleaseSemaphore` overload |
| `5ed997b` | input: PC mapping (mouse aim/WASD/space/shift/F/LMB), host pad file, sequencer |

## Per-file detail

Each entry states the guest symptom that motivated the change, because that is what tells a future port
whether it still applies.

### `runtime/hle_svc.cpp` — NEW, +169 (`4d1b5a5`, `d26bd39`)

The `nn::svc::aarch64::lp64` layer, for syscalls reached from **lifted sdk code** (the runtime HLEs
`nn::os` at the import boundary, but `TOTK_LIFT_ALL` binds main's unimplemented imports to the sdk's real
bodies, and sdk code calls the kernel directly).

* `svc::OutputDebugString` → `fwrite` to stderr (surfaces the game's own log and asserts).
* `SleepThread`, `GetCurrentProcessorNumber` (core 0).
* Thread **queries** only (`GetThreadId`, `GetThreadPriority` → 0, `GetThreadCoreMask` → (−1, 0x7),
  `SetThreadPriority`/`SetThreadCoreMask`/`SetThreadActivity` no-ops, `GetLastThreadInfo` zeroed): the
  runtime owns the threads, so these must not pretend to be the kernel.
* Handles/events: `CloseHandle`, `SignalEvent`, `ClearEvent`, `ResetSignal`, `ArbitrateLock/Unlock`,
  `WaitProcessWideKeyAtomic`, `SignalProcessWideKey` — accepted and ignored.
* Memory: `GetInfo` (only the queries `nn::os::detail` makes at boot: allowed masks, `HeapRegionAddress`,
  `TotalMemorySize` 4 GiB, `TotalUsedMemorySize` = committed guest heap; anything else prints a
  `[svc] GetInfo(n) not implemented` line and returns 0), `SetHeapSize`, `QueryMemory`,
  `SetMemoryPermission`/`SetMemoryAttribute` no-ops.
* IPC: `ConnectToNamedPort` writes a dummy handle and returns Result **0xE401 (no such service)**, so a
  lifted service client fails the way it would on a console without the service instead of trapping.
  Each name is printed once (`[svc] ConnectToNamedPort(sm:)`).
* Fatal paths: `diag::GetBacktrace` → 0 frames (the runtime has no libunwind),
  `svc::Break` → prints and continues (a break here is a port gap, not a reason to kill the process),
  `ExitThread` → `rt_unbind_thread()`, `ExitProcess` → `_exit(0)`.
* libc pieces the sdk imports: `fesetenv`/`fegetenv`/`fegetround`/`__fesetround`/`feclearexcept`/
  `feraiseexcept`/`fetestexcept`.
* `nn::settings::eq(Language, LanguageCode)` (`d26bd39`): compares the language enum against the tag of
  the 8-byte `"en-US"`-style code. Stubbed it returned 0 for every query, so the game never matched its
  own language.

### `runtime/rt_main.cpp` — +111/−6 (`4d1b5a5`, `7977d74`, `96b36ae`, `160380f`, `a8a034f`)

* `TOTK_LIFT_ALL=1` (`4d1b5a5`, extended by `160380f`): every main import the sdk implements runs its real
  lifted body instead of the returning-0 stub. HLE still wins. A namespace exclusion list (`kService`,
  `rt_main.cpp:789`) keeps out everything that would call `svcConnectToNamedPort("sm:")`:
  sf, sm, svc, vi, fs, hid, nifm, apm, oe, aoc, pctl, ldn, bcat, nfp, ns, spl, pm, lm, applet, settings,
  mii, nfc, movie, cap, web, usb, pcv, i2c, gpio, psc, pminfo, spsm, ts, htcs, ldr, socket, ssl, account,
  os, pl, audio, friends, pia, nex, time, err, diag, prepo. `TOTK_LIFT_ALL_VERBOSE=1` lists what got bound.
  The load report gains a `lifted-unbound` count.
* `a8a034f`: `!_ZN2nn5prepo10PlayReport3Add,!_ZN2nn5prepo16SystemPlayReport3Add` added to the default
  `TOTK_LIFT` (`rt_main.cpp:750`). The lifted generator's msgpack `WriteString` fails on the game's
  buffer, `Lp::Utl::PlayReportWrapper` aborts on any `Add` error, and dropping telemetry is the right
  port behaviour.
* `7977d74`: the crash handler prints printable strings from the crashing guest frame
  (`[stack] <off> "..."`, 24 max) — the failing layout/control name is usually still on the stack.
* `96b36ae`: `TOTK_TRACE_STR=1` makes `--trace` also print any of x0..x3 that looks like a guest C string
  or a `SafeStringBase` (chars pointer at +0x8).
* `96b36ae`: `TOTK_IND_LOG=1` keeps the last 32 guest indirect calls per thread (`g_ind_log`,
  `rt_ind_note`, `rt_ind_dump`) and dumps them from the crash handler. A tail call leaves no x29 frame, so
  a virtual call that lands in a thunk with the wrong x0 otherwise shows no caller.
* `7977d74` also declares `extern "C" u64 g_rt_heap_base` early in the file for the string helper.

### `runtime/hle_hid.cpp` — +68/−3 (`5ed997b`)

* PC mapping behind `TOTK_PC_INPUT` (default **on**): WASD move, Space = B (jump), Shift = ZL (squid),
  F = R (bomb), LMB = ZR (fire), E/Enter = A, Q = L, V/G = stick clicks, Esc = +, M = −, arrows = d-pad.
  `TOTK_PC_INPUT=0` restores the TotK key map.
* Relative-mouse aim: `SDL_SetWindowRelativeMouseMode` + a leaky integrator (`TOTK_MOUSE_SENS`, default
  2.0) turning mouse velocity into a right-stick deflection, with a 150-unit deadband so an idle mouse
  never overrides a physical pad's right stick.
* `TOTK_PAD_FILE`: a 24-byte `"PAD1" | u32 seq | u64 buttons | s16 lx,ly,rx,ry` state file written by
  `tools/input_sequencer.py` is merged into the host input every frame (`vpad_poll`/`vpad_merge`), with a
  500 ms staleness release so a closed sequencer cannot leave a button held.

### `runtime/hle_audio.cpp` — +48/−3 (`4d1b5a5`)

Splat2's audio client is SDK **3.x**, not the 5.x layout the rest of the file assumed.

* `InitializeAudioRendererParameter` rewritten to the 3.x layout (`+0x00` sampleRate 48000/32000, `+0x04`
  sampleCount = rate/200, `+0x08` ≥ 1, `+0x0c` in [0,0x100], `+0x10` in [1,0x400], `+0x14` ≥ 1,
  `+0x18` in [0,0x100], `+0x30` revision) and `IsValidAudioRendererParameter` implemented to match.
  The real validator also compares `+0x30` against the driver's live revision — a console-only detail,
  deliberately left out.
* `GetMemoryPoolState` → ATTACHED (1) once `RequestAttachMemoryPool` succeeded. It is an IPC query in
  retail, so it has to be HLE'd; the game polls it until it reads 1 and otherwise spins.
* Pre-5.x device-name spellings mapped onto the same single-device model: `ListAudioDeviceName`,
  `GetActiveAudioDeviceName`, `GetAudioDeviceOutputVolume` (returns float 1.0), `SetAudioDeviceOutputVolume`,
  `GetActiveChannelCount`, `AcquireAudioDeviceSwitchNotification`. The 5.x
  `SetAudioDeviceOutputVolume` line was folded into the 3.x one.

### `runtime/hle_os_thread.cpp` — +46 (`4d1b5a5`, `0f5413e`, `618c5c6`, `160380f`, `862561e`, `36b5ddc`)

* `618c5c6`: **the highest-value fix in the whole set.** `CreateThread` (and the main thread) now write
  the stack base into the guest `nn::os::ThreadType` at **`+0x50`**. `sead::MemUtil::isStack` reads it;
  without it every thread's region looks like `[0, size)`, `isStack` returns 0 for every pointer, and
  `Lp::UI::Impl::Name::init` shares a caller stack pointer as a control name — every layout lookup then
  returns null (the "customize page is null" blocker).
* `0f5413e`: `TrySendMessageQueue`, `TryReceiveMessageQueue`, `TimedWaitSystemEvent`,
  `SetThreadNamePointer` (SDK 5+ spelling), `SdkAllocateTlsSlot`, `FlushDataCache`.
* `4d1b5a5`: 7-arg `CreateThread` overload (`…mii`, SDK 5+ with the core hint; the host scheduler ignores
  the hint).
* `160380f`: auto-clear `EventType` family — `WaitEvent`, `TimedWaitEvent`, `TryWaitEvent`. They were
  UNIMPL, which left the Pia scheduler spinning on a plain `nn::os::Event`.
* `862561e`: `JamMessageQueue` / `TryJamMessageQueue` (push-front, even when full). UNIMPL meant the
  `RootTask` worker never received its stop message and the main thread parked in `WaitThread`.
* `36b5ddc`: 1-arg `ReleaseSemaphore(SemaphoreType*)` (release one). UNIMPL, so a semaphore released this
  way never woke its waiter and the title flow stalled after a handful of frames.

### `tools/recomp/gen.py` — +45/−6 (`4d1b5a5`, `ce3bc6e`, plus FADDP from `6472ef3`)

* **FADDP** (`6472ef3`, a local edit made while vendoring — *not* upstream): scalar `FADDP Sd, Vn.2S` and
  `Dd, Vn.2D` (two operands) were `Unhandled`; the vector three-operand forms still go through
  `_pairwise`. Without it one module has 15 unhandled instructions.
* **PIC jump tables** (`4d1b5a5`, `ce3bc6e`): `jump_table()` tries both operand orders; `_jump_table(...,
  swapped=True)` treats the *third* operand of `add d, d, base` as the anchor when the load's base
  register is the same register. The anchor is then `adrp+add`, not an `adr` label (sdk `0x3ccd8`,
  `nn::util::VFormatString`). The table-base scan now also follows `mov scan_reg, xOther` copies while
  keeping the load's base for the anchor test (main `0xf861a4`, `gsys::GBuffer::expand` — its mid-function
  case labels trapped in `call_ind`). Measured: sdk 0→270 and main 0→1108 tables resolved, `br`
  dispatches 648→378 (sdk) and 7468→6360 (main), then 1108→1113 / 6360→6355 with the mov fix.
* **Cold functions** (`4d1b5a5`): a direct `BL` can target code the FDE scan does not list as a function
  (a function's cold half placed right after its hot part — main `0x13a6f2c`, sdk `0x871c8/0x87454/0x87468`).
  A synthesized function is added at each such target, bounded by the next FDE start. `TOTK_NO_SYNTH=1`
  disables it.
* `bisect` import added for the above.

### `runtime/hle_fs.cpp` — +32 (`4d1b5a5`)

`nn::pl` shared-font service: `RequestSharedFontLoad` → 0, `GetSharedFontLoadState` → **1** (the game spins
in `main 0x1275240` until it reads 1), `GetSharedFontSize`, `GetSharedFontAddress`. The address must never
be null (the engine parses what it is given; null → crash, all-zero → rejected and it falls back to its
romfs fonts). `TOTK_FONT=<file>` substitutes a real font through the same loader the OSD uses.

### `runtime/hle_os_mem.cpp` — +14/−3 (`4d1b5a5`)

* `MemoryInfo` real layout: `+0x00` total physical, `+0x08` total used, `+0x10` heap size, `+0x18` heap
  used, `+0x20` heap free, `+0x28` heap reserved/peak, `+0x30` u32. TotK's `nninitStartup` reads `{+0, +8}`
  (unchanged behaviour); Splat2's `nnMain` reads `{+0x10, +0x18}` right after
  `SetMemoryHeapSize(0xcb000000)` + `AllocateMemoryBlock(0x2000000)` and reserves the difference as its
  main heap. With those two zero, `VirtualAlloc(0)` failed.
* `AllocateMemoryBlock(size 0)` returns the bump pointer and takes nothing instead of trapping on
  `VirtualAlloc(a, 0)`.

### `runtime/rt.h` — +8/−1 (`4d1b5a5`)

`g_ind_log` + `rt_ind_note`/`rt_ind_dump` declarations, and `call_ind` notes every resolved indirect call
when `TOTK_IND_LOG=1`.

### `runtime/hle_vi.cpp` — +5/−2 (`4d1b5a5`)

`f_7a83e4_orig` (the TotK `--hook 7a83e4` target) is declared `__attribute__((weak))` and the hook returns
early when it is absent. Another game's `main` has no such address; without the weak symbol the runtime
does not link at all.

### `tools/recomp/mkninja_linux.py` — +4 (`5c50b84`)

`--sysinc DIR` adds an include directory (for `--syslib` archives such as a locally built
`libshaderc_combined.a`) to `cxxflags` and `cflags`. Upstream assumed the distro's shaderc headers are on
the default include path.

## Deliberately NOT imported

These exist in Splat2-ReTotK but are game-specific and have no place in a Splat3 baseline:

| Path | Why it stays out |
|---|---|
| `tools/patches/*` | binary/romfs patches for Splatoon 2 assets (skip-opening overlay, gameover/quickmenu fixes) |
| `tools/scenarios/*` | input scripts for Splatoon 2's menus (`boot-to-tutorial.txt`) |
| `tools/{ghidra,nso,rework,ryujinx,static,wsl,linux/…}` at the repo root | Splat2 investigation scaffolding, not baseline (the baseline has its own `tools/linux/build_deps.sh`) |
| `port/` (repo root) | the Splat2 decompilation workspace |
| `sp2win/`, `harness/`, `reference/`, `*.md` at the repo root | Splat2 notes and references |
| `work/gen-totk/*`, `work/img/*`, romfs | generated C / game data — never committed |

`tools/input_sequencer.py` **is** imported: it is the host end of `TOTK_PAD_FILE` and contains no game
knowledge. Its scenario files are the game-specific part and are not copied.

## Verification of the imported tree

```bash
$ diff -rq --exclude=.git Work/port/totk-recomp "$S2/baseline/totk-recomp"
Only in Work/port/totk-recomp/tools: input_sequencer.py
```

i.e. `port/totk-recomp` == the hardened Splat2 baseline, plus the sequencer, on top of upstream `a32f709`.
`git diff a32f709..HEAD --stat` here is 12 files / +797 −21 (of which 11 files / +546 −21 are the baseline
fixes, and +251 is `tools/input_sequencer.py`).
