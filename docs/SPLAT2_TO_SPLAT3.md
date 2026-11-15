# Splatoon 2 → Splatoon 3: transfer analysis of the working native-Linux port

Measured 2026-11-15 against:

- Splat2: `/mnt/SSD Storage/Games/Ports/Switch/Splat2/work/Splat2-ReTotK` (port +
  `baseline/totk-recomp`), data `../base-nsp`, `../img`, `../gen-totk`.
- Splat3: `Work/data/exefs` extracted from the **v11.3.0 update NSP** (`b17c3842…nca`,
  7,266,187,264 B; the v1.0.0 base NSP carries no executable — see §3.2), runtime
  `Work/port/totk-recomp`.

Every number is re-runnable with the commands in §8.

## 0. The numbers that decide the port

| # | Number | Meaning |
|---|---|---|
| 1 | **1,210 imports in Splat3 `main` vs 784 in Splat2** | `kMaxImports = 1024` (`hle.h:5`) is exceeded; `rt_main.cpp:723` `die()`s. 1-line fix |
| 2 | **353 / 1,210 (29.2%) already have an exact-name HLE** | the Splat2 runtime's 449 HLE entries bind ~29% of Splat3's imports unchanged |
| 3 | **+266 (22.0%) are byte-identical import names that Splat2 also had** | Splat2's RE notes/lift decisions apply directly. **619/1,210 = 51.2% transferable on day 1** |
| 4 | **1,196 / 1,210 (98.8%) of Splat3 imports are exported by Splat3's own `sdk`** | the remaining work is lift policy, not HLE authoring |
| 5 | **536 `nvn*` proc names in Splat3 `main`; 455 (84.9%) already implemented** | the NVN→Vulkan renderer carries over; 81 procs to add |
| 6 | **271,721 functions / 20,594,080 instructions in Splat3 `main`** (2.38× / 3.91× Splat2) | lift scale; ~344 generated TUs vs 88 |
| 7 | **SDK 20.5.22 vs Splat2's 1.6.0** (19 releases) | `nn::audio` grew to 185 imports (96 new); OS struct offsets must be re-verified |
| 8 | **`IMP_FAKE = MAIN_BASE + 0x5000000` (80 MB) < main image 102.7 MB** | import thunk addresses land inside `.rodata`; must be moved before the first run |

## 1. Binary / module structure

### 1.1 Splatoon 2 (base NSP, retail)

| Module | NSO bytes | text | image (text+ro+data+bss) | FDE funcs | build id (16 B) |
|---|---:|---:|---:|---:|---|
| main | 23,717,627 | 21,057,640 | 57,249,696 | 114,172 (+1 cold) | `f18ff3cfcbad0884d72abbeb6c540d6b` |
| sdk | 3,032,664 | 2,941,004 | 7,752,520 | 14,426 (+3 cold) | `972cc2555ec0d6a03473f8c0b03e2413` |
| rtld | 13,040 | 11,888 | 24,576 | — | `f682412495a4fdfb967204749c40b68a` |
| subsdk0 (NVN) | 787,508 | 752,260 | 1,744,896 | 4,299 | `1686ae4b62a9c6a3b6ee7a9187ee4932` |
| subsdk1 (nn::vi) | 67,223 | 57,796 | 199,232 | 385 | `b43e2fdcf4b3fad2eb10e3680981223f` |
| subsdk2 | 49,096 | 43,056 | 75,120 | 76 | `de4acf0cb84ec63bf60e0fac815c4d1a` |
| subsdk3 | 174,009 | 172,988 | 278,528 | 496 | `51b3e7996b61c8c5b3a253e89c5ba195` |

All NSO0, flags `0x3f` (all segments compressed). Era strings:

- `sdk`: `SDK MW+Nintendo+NintendoSdk_nnSdk-1_6_0-Release`
- `subsdk0`: `SDK MW+Nintendo+NintendoSDK_NVN-1_6_0-Release`
- `main`: `SDK MW+Nintendo+NintendoSDK_gfx-1_6_0-Release`, NEX `4_1_1-S130-20170227`
- NVN API major: Splat2 needs **53** (`HANDOFF.md:127`, `TOTK_NVN_INT="0=53"`).

### 1.2 Splatoon 3 (v11.3.0 update)

| Module | NSO bytes | text | image (text+ro+data+bss) | FDE funcs | build id (16 B) |
|---|---:|---:|---:|---:|---|
| main | 51,859,062 | 82,376,320 | 102,733,824 | 271,721 | `28c4287aee36f7499da60f3e68b54c70` |
| sdk | 6,204,231 | 6,698,752 | 12,929,536 | 32,673 | `574284b40c06cf1821a44857939083be` |
| rtld | 10,109 | 9,536 | 24,576 | 33 | `263c3426d28af8d8cce29a9d2da8ff0f` |

**There are no `subsdk*` modules.** NVN and `nn::vi` are folded into `sdk`
(`sdk` contains `…NintendoSDK_NVN-20_5_22-Release`; `main` imports `nvnBootstrapLoader`
and 5 `nn::vi` entries). The runtime's two-module loader (`rt_main.cpp:726-727`) fits
Splat3 without change; Splat2 needed 7 module tables.

Era strings (from `sdk`): `SDK MW+Nintendo+NintendoSdk_nnSdk-20_5_22-Release`,
`…NintendoSDK_NVN-20_5_22-Release`, `…NintendoSDK_NPLN-1_38_1-Release` (NPLN is the
Splat3-era online stack; Splat2 used NEX). NCA header of the v1.0.0 base title says
`SDK Version 13.3.6.0`, but that NCA stores no code (§3.1); the executable we hold is
built with **20.5.22**.

Growth vs Splat2: NSO 2.19×, text 3.91×, image 1.79×. `.dynsym` is 1,293 entries in
Splat3 `main` (Splat2: 146,623 — Splat2 exported almost everything; Splat3 exports 82
symbols and keeps 23,066 in `sdk`).

## 2. Import surface and overlap

### 2.1 Counts

Union of undefined symbols referenced by `DT_JMPREL` + `DT_RELA` (the number
`gen.py` emits as `imports`; PLT-only count in parentheses).

| | Splat2 main | Splat3 main | ratio |
|---|---:|---:|---:|
| imports | 784 (752 PLT) | **1,210 (1,131 PLT)** | 1.54× |
| sdk PLT entries | 4,749 | **7,832** | 1.65× |
| sdk defined symbols | 15,932 | 23,057 (12,690 `nn::`) | 1.45× |

### 2.2 Classification of every Splat3 `main` import

| class | count | % of 1,210 | also sdk-exported |
|---|---:|---:|---:|
| **a. exact-name HLE already in the hardened runtime** | **353** | **29.2%** | 353 (100%) |
| **b. identical name present in Splat2 main's imports** (RE/lift decisions apply) | **266** | **22.0%** | 258 (97.0%) |
| **c. new in Splat3** (no HLE, no Splat2 counterpart) | **591** | **48.8%** | 585 (99.0%) |
| a+b = transferable without new RE | **619** | **51.2%** | — |
| all Splat3 imports liftable from `sdk` | **1,196** | **98.8%** | — |

Reverse direction: 514 of Splat2's 784 imports survive by name (65.6%); 270 are gone
(38 C symbols, 36 `nn::audio`, 19 `nn::friends`, 14 `nn::codec`, 13 `nn::web`,
13 `nn::socket`, 11 `nn::applet`, 11 `nn::hid`, 10 `nn::sf`, 10 `nn::os` …).

### 2.3 Namespace map (Splat3 main)

| namespace | imports | HLE-covered exact | namespace | imports | HLE-covered |
|---|---:|---:|---|---:|---:|
| (C symbols) | 265 | 37 | nn::oe | 22 | 6 |
| nn::audio | 185 | 78 | nn::nfp | 20 | 0 |
| nn::os | 119 | 109 | nn::bcat | 19 | 0 |
| (templates/libstdc++) | 101 | 13 | nn::time | 19 | 8 |
| nn::hid | 53 | 36 | nn::async::detail | 19 | 0 |
| nn::fs | 34 | 25 | nn::crypto::detail | 18 | 0 |
| nn::socket | 30 | 0 | nn::util::detail | 14 | 0 |
| nn::prepo | 30 | 0 | nn::friends | 11 | 0 |
| nn::ldn | 26 | 0 | nn::err | 10 | 0 |
| nn::fontll | 24 | 0 | nn::mem | 9 | 9 |
| nn::image | 23 | 0 | nn::ssl | 9 | 0 |
| nn::util | 23 | 6 | nn::vi | 5 | 5 |
| nn::async | 23 | 0 | | | |
| nn::account | 23 | 6 | | | |

48 namespaces vs Splat2's 43.

**Entirely new namespaces (no Splat2 main import at all):** `nn::async` (23),
`nn::async::detail` (19), `nn::swkbd` (8), `nn::ngc` (7), `nn::album` (6), `nn::pia` (4),
`nn::pdm` (1), `nn::ec` (1), `nn::ro` (1).
**Disappeared from main:** `nn::sf`, `nn::applet`, `nn::settings`, `nn::la` — the IPC
client (`nn::sf`) and applet layer now live in `sdk`, not in `main`.

Top of the 591 new imports by namespace:

| # | namespace | new | example |
|---|---|---:|---|
| 1 | (C symbols) | 187 | — |
| 2 | nn::audio | 96 | `nn::audio::RemoveDelay(AudioRendererConfig*, DelayType*, FinalMixType*)` |
| 3 | (templates) | 76 | — |
| 4 | nn::async | 23 | `nn::async::Executable::Execute(CancellationToken const&)` |
| 5 | nn::prepo | 22 | `nn::prepo::PlayReport::Add(char const*, void const*, unsigned long)` |
| 6 | nn::image | 20 | `nn::image::PngEncoder::Encode` |
| 7 | nn::async::detail | 19 | |
| 8 | nn::socket | 12 | |
| 9 | nn::crypto::detail | 11 | |
| 10 | nn::friends | 10 | |
| 11 | nn::util | 10 | |
| 12 | nn::os | 9 | |
| 13 | nn::fs | 9 | |
| 14 | nn::swkbd | 8 | `nn::swkbd::ShowKeyboard` |
| 15 | nn::ngc | 7 | `nn::ngc::ProfanityFilter::MaskProfanityWordsInText` |
| 16 | nn::bcat / nn::album | 6 / 6 | `nn::album::SaveScreenshot` |
| 17 | nn::account / nn::ssl / nn::err / nn::oe | 5 each | |
| 18 | nn::pia / nn::nifm / nn::hid | 4 each | |
| 19 | nn::pctl / nn::ldn / nn::time / nn::lmem | 3 each | |
| 20 | nn::diag / nn::fontll / nn::crypto | 2 each | |
| 21 | nn::pdm / nn::ec / nn::aoc / nn::ro / nn::nsd | 1 each | |

`nn::prepo` (30 imports, 22 new) and `nn::pia` (4) are telemetry/online and are already
policy-stubbed by the runtime (`kService`, `rt_main.cpp:789-801`).

### 2.4 Renderer surface

The game resolves every NVN proc by name at init (`nvnBootstrapLoader` +
`nvnDeviceGetProcAddress`). Distinct `nvn*` names referenced in Splat3 `main`: **536**.
Already implemented in `runtime/nvn/*.c` (`NVN(proc)` definitions, 460 total):
**455 (84.9%)**. The 81 missing are the modern additive API — transform feedback
(`nvnCommandBufferBeginTransformFeedback`, `…BindTransformFeedbackBuffer(s)`), separate
samplers/images/`…Deferred` binds, `nvnCommandBufferClearBuffer`,
`nvnCommandBufferBindCoverageModulationTable`, plus newer object getters
(`nvnTextureGetSamples`, `nvnSamplerBuilderGetMaxAnisotropy`, …). Full list obtainable
with the command in §8.6.

## 3. Engine and asset layout

### 3.1 Splatoon 2 romfs (extracted)

5,390 files, 3,311,496,287 B (3.31 GB), 17 top-level dirs:

| dir | files | size | dir | files | size |
|---|---:|---:|---|---:|---:|
| Model | 1,367 | 2,637.8 MB | Message | 35 | 2.4 MB |
| Sound | 2,075 | 574.1 MB | Map | 103 | 1.9 MB |
| Layout | 365 | 36.9 MB | Etc | 8 | 0.4 MB |
| Effect | 1 | 27.7 MB | Shader | 2 | 0.2 MB |
| Pack | 5 | 10.8 MB | Rumble | 45 | 0.2 MB |
| Font | 6 | 6.1 MB | Mush | 31 | 0.6 MB |
| System | 16 | 4.4 MB | SLink2 | 1 | 0.6 MB |
| Env | 60 | 4.1 MB | ELink2 | 1 | 0.3 MB |
| Param | 1,269 | 3.4 MB | | | |

Extensions: `.szs` 1,935 (2,721 MB), `.barslist` 1,310, `.bprm` 1,240, `.bars` 633
(120 MB), `.bfstm` 120 (454 MB), `.bnvib` 45, `.byml` 41, `.brbl` 24, `.pack` 5.
1,733/1,935 `.szs` carry the `.Nin_NX_NVN.szs` platform suffix.

Structure: `Layout/X.szs` = Yaz0 → SARC → one `.arc` = SARC →
`anim/*.bflan` + `*.bflyt` (FLYT/FLAN/FCPX, `nn::ui2d`). No `Scene/`, no `Actor/`:
scene composition is code (`Lp::Sys`), actors live in `Model/` + `Param/`.

Middleware in `main` (mangled-name occurrences in 21 MB text): `Lp::` 11,394,
`sead::` 4,606, `agl::` 3,303, `gsys::` 2,490, `nn::atk` 1,837, `nn::vfx` 513,
`nn::g3d` 305, `nn::font` 259. The `nn`ware layer is statically linked into `main`.

### 3.2 Splatoon 3 (blocked, with measured bounds)

`Work/data/romfs` is **not extracted**. The v1.0.0 base Program NCA stores neither an
ExeFS nor the RomFS metadata: its ExeFS section is a 49.4 MB virtual range with an empty
sparse table, and the ~510 KB RomFS dir/file meta tables fall in a sparse hole
(`EXTRACT_REPORT.md:174-195`). Splat3 ships its payload in the day-one patch, so the
target is the v11.3.0 update, whose Program NCA is a **BKTR** patch over the base image
and needs `--basenca`-style merging.

Measured from the headers (not estimates):

| quantity | Splat3 | Splat2 | ratio |
|---|---:|---:|---:|
| Program NCA | 7,266,187,264 B (update) | ≈3.34 GB (romfs 3.31 + exefs 27.9 MB) | 2.2× |
| ExeFS virtual section | 49.4 MB (`0x2F1C000`) | 27.9 MB (7 NSOs) | 1.77× |
| RomFS virtual size | **4.66 GB** (`0x115F54000`) | 3.31 GB | 1.41× |
| RomFS stored (sparse) | 3.32 GB (`0xC5CC0000`) | 3.31 GB | 1.00× |

So the *installed* asset volume is about the same, but the sparse layout hides 1.28 GB
of holes. Asset-family overlap (Layout/Model/Effect/Pack/Sound) cannot be computed until
the BKTR merge produces a directory tree; the tooling to do it (`yaz0try.py`,
`szs_list.py`) is title-independent (it parses generic Yaz0 + SARC, one level down; the
second `.arc` level needs only `sarc_list()` called again — verified on Splat2's
`Cmn_ActionGuide_00`).

## 4. Lift / generator reuse

All four generator fixes are **title-independent instruction-shape fixes**, and
`Work/port/totk-recomp/tools/recomp/gen.py` is **byte-identical** to Splat2's copy
(`cmp`: gen.py 1,453 lines, `mkninja_linux.py`, `nso.py`, `funcs.py` all SAME).

| Fix | Location | Why it transfers |
|---|---|---|
| scalar `FADDP Sd, Vn.2S` | `gen.py:995` | encoding gap, not game-specific |
| PIC jump tables (`add d,d,base`) | `gen.py:389` | compiler codegen shape |
| cold fragments at non-FDE `BL` targets | `gen.py:1388` | any `-O2` build |
| `mov`-following jump-table base | `gen.py:441-445` | same |

Measured lift inputs (FDE scan, same algorithm as `tools/funcs.py`, incl. capstone
linear sweep):

| module | text | FDE functions | instructions | FDE coverage | `BL` targets | `BL` not FDE start |
|---|---:|---:|---:|---:|---:|---:|
| Splat2 main | 21,057,640 | 114,172 | 5,261,384* | 0.9994 | 32,120 | 701 |
| Splat2 sdk | 2,941,004 | 14,426 | 714,802* | 0.9718 | 4,575 | — |
| **Splat3 main** | **82,376,320** | **271,721** | **20,594,080** | **0.9921** | **57,135** | **1,834** |
| **Splat3 sdk** | **6,698,752** | **32,673** | **1,674,688** | **0.9650** | **9,903** | **6,579** |
| Splat3 rtld | 9,536 | 33 | 2,384 | 0.9681 | 23 | 1 |

\* Splat2: FDE/BL columns from `Splat2/work/img/funcs_*.json`; instruction counts from
`Splat2/work/gen-totk/*/report.json` (exact generator counts).

The 1,834 non-FDE `BL` targets in Splat3 main are the cold-fragment population fix #3
must synthesise (Splat2 had 701 in main, of which the generator emitted 1 function:
`0x13a6f2c`). `sdk` has 6,579 — most are `bl` into data/veneer, worth triaging after the
first gen run.

Splat3 `Work/gen/` does not exist yet, so functions/insns/unhandled for a real
generation are unknown; the expected TU count at `--tu-insns 60000` (`gen.py:1343`) is
**344 for main** (Splat2: 88) and **28 for sdk** (Splat2: 12). Splat2's unhandled
baseline to beat: main **0**, sdk **1,657** (AES + halfword atomics).

## 5. Runtime reuse inventory

Splat2 changed 12 files vs upstream `a32f709` (+797/−21). `Work/port/totk-recomp` already
contains all of it (commits `836bca0`, `aeab59a`), and `ninja objs-rt` compiles all 27
runtime objects clean.

| File | Symbol / change | Verdict | Reason |
|---|---|---|---|
| `hle_svc.cpp` (new, +169) | `nn::svc::aarch64::lp64` layer (OutputDebugString, SleepThread, thread/event/handle syscalls, ticks, map/unmap) | **title-independent** | needed by any lifted sdk code; Splat3's sdk is 1.65× bigger and will hit more of it |
| `hle_os_thread.cpp:108-111` | `ThreadType+0x50` = stack base (CreateThread + main thread) | **title-independent** | `sead::MemUtil::isStack` reads it; Splat3 also uses sead |
| `hle_os_thread.cpp:142` | 7-arg `CreateThread` | **title-independent** | core `nn::os` |
| `hle_os_thread.cpp` | `SetThreadNamePointer`, `SdkAllocateTlsSlot`, `FlushDataCache` | **title-independent** | core `nn::os` |
| `hle_os_thread.cpp` | 1-arg `ReleaseSemaphore` | **title-independent** | overload used by Splat2's engine |
| `hle_os_thread.cpp` | `WaitEvent`/`TimedWaitEvent`/`TryWaitEvent`/`TimedWaitSystemEvent` | **title-independent** | auto-clear event family |
| `hle_os_thread.cpp` | `TrySend/TryReceive/Jam/TryJamMessageQueue` | **title-independent** | sead/Pia plumbing |
| `hle_os_mem.cpp:28-41` | `MemoryInfo` layout (+0x10 heap size, +0x18 used, +0x20 free) | **verify-first** | layout comes from Splat2's sdk; Splat3 SDK 20 may have grown the struct |
| `hle_os_mem.cpp:169` | zero-size `AllocateMemoryBlock` → bump pointer | **title-independent** | defensive |
| `hle_fs.cpp:427-431` | `nn::pl` shared font (state 1, never-null 4 KB, `TOTK_FONT`) | **title-independent** | Splat3 imports 3 of the 4 (`GetSharedFontSize/Address/LoadState`) and all 3 are already HLE'd; same romfs-font fallback |
| `hle_audio.cpp:564-600` | `Initialize/IsValidAudioRendererParameter` (SDK-3.x layout) | **Splat2-specific** | Splat3 has 185 audio imports including `AudioRendererConfig`/`FinalMixType` APIs — new ABI |
| `hle_audio.cpp` | device enumeration, channel count, switch notification | **likely reusable** | host policy, not ABI |
| `hle_hid.cpp:930-960` | PC input mapping, `TOTK_PRESS`, `vpad_*` | **likely reusable** | bindings are Splatoon-shaped already |
| `hle_vi.cpp:212-214` | weak `f_7a83e4` guard | **TotK-specific** | address only exists in TotK |
| `rt_main.cpp:746-777` | `TOTK_LIFT` prefix list + `!excl` | **likely reusable** | policy, re-tune per API surface |
| `rt_main.cpp:788-801` | `TOTK_LIFT_ALL` + 47-namespace `kService` exclusion | **title-independent** | Splat3 needs `_ZN2nn4async` considered: pure library → lift, not service |
| `rt_main.cpp` (prepo) | prepo stays stubbed, `PlayReport::Add` un-lifted | **title-independent** | telemetry dropped on purpose |
| `rt.h`/`rt_main.cpp` | `TOTK_IND_LOG`, string trace, `TOTK_TRACE_FS` | **title-independent** | debug tooling |
| `nvn/*.c` | 460 `NVN(proc)` implementations + Vulkan backend | **title-independent** | 455 of Splat3's 536 proc names already covered |

`kService` entries that are title policy, not platform facts: `_ZN2nn5prepo` (telemetry)
and `_ZN2nn3pia`/`_ZN2nn3nex`/`_ZN2nn7friends` (Splat2's online stack). The other 43 are
generic service namespaces.

## 6. Porting worklist

Ordered by blocking-ness. Effort: S < 1 day, M 1–3 days, L > 3 days.

| # | Item | (a) Why it blocks | (b) Start from | (c) Effort | (d) Risk |
|---|---|---|---|---|---|
| 1 | Raise `kMaxImports` 1024 → 2048 and move `IMP_FAKE` from `+0x5000000` to ≥ `+0x6400000` (`hle.h:5`, `rt_main.cpp:50,723,732`) | **hard stop**: 1,210 > 1,024 → `die("too many imports")`; and 80 MB fake window overlaps the 102.7 MB main image (lands in `.rodata`). Must stay 4×1210 B below `SDK_BASE` (`0x7180000000`, 25.3 MB headroom above the image) | `rt_main.cpp:48-50` | S | **high** if missed — silent aliasing/corruption |
| 2 | Measure NVN API major/minor and set `device_integer()[0..1]` (`nvn/objects.c:17`) | device init compares the value the game asks for; SDK 20.5's NVN is far newer than TotK's 55/15 and Splat2's 53 | find the `nvnDeviceGetInteger(dev,0)` compare in `main` (same method as Splat2), or `TOTK_NVN_INT=0=N` | S | medium |
| 3 | Run `scripts/gen_all.sh main sdk` (with capstone) and triage `unhandled` | 20.6 M instructions; Splat2's sdk left 1,657 unhandled, Splat3's sdk is 2.3× bigger | `gen.py` (identical), `Work/scripts/gen_all.sh` | M (gen) / L (build) | medium: ~344 TUs, long compile |
| 4 | Re-verify `nn::os` ABI: `ThreadType+0x50`, `MemoryInfo`, event/semaphore sizes | wrong offsets corrupt silently; the `+0x50` bug cost Splat2 a day | `hle_os_thread.cpp:108`, `hle_os_mem.cpp:28` | M | **high** |
| 5 | `nn::audio` for SDK 20.5: 185 imports, 96 new (`AudioRendererConfig`, `FinalMixType`, `DelayType`, `AuxType`) | no sound, and `InitializeAudioRendererParameter` with the wrong layout can fail renderer init and stall the audio thread | `hle_audio.cpp:564` (Splat2 3.x path) + the 5.x path already in the file | L | medium |
| 6 | Add the 81 missing NVN procs (transform feedback, separate/deferred binds, `ClearBuffer`, coverage modulation) | a missing proc returns 0 → null function pointer or no draw | `runtime/nvn/cmdbuf.c`, `objects.c` | M | medium |
| 7 | Decide lift policy for the 591 new imports; 585 are `sdk` exports, so extend `TOTK_LIFT`/`kService` (`nn::async` is a pure library → lift; `nn::ngc`/`nn::swkbd`/`nn::album`/`nn::ec`/`nn::pdm` are services → HLE/stub) | each unbound import is a stub returning 0 | `rt_main.cpp:746-801` | M | low (98.8% liftable) |
| 8 | BKTR romfs merge (base + update) to get `Work/data/romfs` | cannot boot past `nnMain` without romfs; also blocks asset-overlap measurement | `EXTRACT_REPORT.md:192-195`; hac2l in `Work/deps/hac2l` | L | **high** (sparse + BKTR) |
| 9 | Fix romfs path expectations (`.Nin_NX_NVN.szs` suffix may differ; Splat3's UI is `Layout/…`) | file-not-found at boot | `hle_fs.cpp:129-143` heuristics | S | low |
| 10 | UI/Lp::UI (FLYT) bring-up | title screen, menus | Splat2 layout work + `szs_list.py` | L | high |

## 7. What we get for free, and the head start

Free (measured, not estimated):

- **The entire generator and build system.** `gen.py`/`mkninja_linux.py`/`nso.py`/
  `funcs.py` are byte-identical to the working Splat2 copies; all four generator fixes
  are shape-based and title-independent. Splat3's main lifts with the same code path.
- **The NVN→Vulkan renderer.** 455 of the 536 proc names Splat3 references are already
  implemented (460 procs total), including the command-buffer, texture/sampler, and
  Maxwell→SPIR-V paths. New work: 81 additive procs.
- **29.2% of the import surface exactly, 51.2% with Splat2 provenance.** 353 imports hit
  an existing HLE entry; 266 more are names Splat2 also imported, so the Splat2 HLE and
  RE notes apply unchanged.
- **A 98.8%-liftable boundary.** 1,196 of 1,210 imports are exported by Splat3's own
  `sdk`; the port does not have to author 1,210 HLE functions, only choose what to lift.
- **The whole runtime hardening layer**: 449 HLE entries, `nn::svc` syscall layer,
  event/semaphore/message-queue implementations, `nn::os::MemoryInfo`, shared-font
  fallback, PC input mapping + scripted pad, traces (`TOTK_IND_LOG`, `TOTK_TRACE_FS`),
  and the `TOTK_LIFT_ALL` service-exclusion policy. All of it compiles in the Splat3
  tree today (`BUILD_SMOKE`, 27 objects).
- **Asset tooling**: Yaz0/SARC listing is format-generic; Splat2's `Layout` pipeline is
  a two-level SARC that the same parser walks.

Head start, stated conservatively: **~100% of the generator/build and ~100% of the NVN
renderer (455/536 proc names) carry over; ~51% of the 1,210-import HLE/RE surface is
already solved (353 exact HLE + 266 Splat2-identical), and ~99% is liftable from the
shipped `sdk`.** What is genuinely new is SDK 20.5-era `nn::audio` (96 imports),
`nn::async` (42), the 81 NVN procs, OS struct re-verification, and the BKTR romfs.

Not free (the real cost): the build (344 TUs vs 88), the audio ABI, and the UI layer.

## 8. Re-run commands

### 8.1 NSO header

```bash
python3 - <<'EOF'
import struct,os
for p in ["exefs/main","exefs/sdk","exefs/rtld"]:
    d=open(p,'rb').read(0x100)
    print(p,os.path.getsize(p),hex(struct.unpack_from('<I',d,0x0c)[0]),
          struct.unpack_from('<III',d,0x10),struct.unpack_from('<III',d,0x20),
          struct.unpack_from('<III',d,0x30),hex(struct.unpack_from('<I',d,0x3c)[0]),d[0x40:0x50].hex())
EOF
```

### 8.2 Flat image + MOD0/dynamic (Splat3 `.decompressed` = 0x100-byte prefix + image)

```bash
python3 - <<'EOF'
import struct
for m in ["main","sdk"]:
    d=open(f"exefs/{m}.decompressed","rb").read()
    pre=struct.unpack_from('<I',d,0x10)[0]
    img=d[pre:]
    mod0=img.find(b'MOD0')
    dyn=mod0+struct.unpack_from('<i',img,mod0+4)[0]
    print(m,"prefix",pre,"mod0",mod0,"dyn",dyn)
EOF
```

### 8.3 Imports (union of JMPREL + RELA undefined symbols)

```bash
python3 - <<'EOF'
import struct, json
def load(m):
    d=open(f"exefs/{m}.decompressed","rb").read(); pre=struct.unpack_from('<I',d,0x10)[0]
    img=d[pre:]; mod0=img.find(b'MOD0'); dyn=mod0+struct.unpack_from('<i',img,mod0+4)[0]
    t={}; p=dyn
    while True:
        tag,v=struct.unpack_from('<QQ',d,p); p+=16
        if tag==0: break
        t.setdefault(tag,[]).append(v)
    return d,pre,t
d,pre,t=load("main")
st=t[6][0]+pre; stt=t[5][0]+pre; seen={}
def nm(o):
    e=d.index(b'\0',stt+o); return d[stt+o:e].decode('utf8','replace')
for k,sk in ((23,2),(7,8)):
    base=t[k][0]+pre; sz=t[sk][0]
    for i in range(0,sz,24):
        _,ri,_=struct.unpack_from('<QQq',d,base+i); s=ri>>32
        if s and struct.unpack_from('<H',d,st+s*24+6)[0]==0:
            seen[s]=nm(struct.unpack_from('<I',d,st+s*24)[0])
print(len(seen))                 # Splat3 main: 1210
json.dump(sorted(seen.values()),open("/dev/shm/s3_main_imports.json","w"))
EOF
```

### 8.4 Classification (a/b/c)

```bash
python3 - <<'EOF'
import json
s3=set(json.load(open('/dev/shm/imp_sets.json'))['s3']); s2=set(json.load(open('/dev/shm/imp_sets.json'))['s2'])
hle=set(open('/dev/shm/hle_syms.txt').read().split())|set(open('/dev/shm/hle_c.txt').read().split())
a=s3&hle; b=(s3&s2)-hle; c=s3-s2-hle
print(len(s3),len(a),len(b),len(c))
EOF
```

(`hle_syms.txt`/`hle_c.txt` come from §8.5. `imp_sets.json` is the pair of import-name
lists produced by the §8.3 parser for Splat3 `main` and for Splat2 `main`
(`Splat2/work/img/main.bin` + `main.json`, same union rule).)

### 8.5 Runtime HLE names

```bash
cd Work/port/totk-recomp
grep -rhoP 'HLE_AS\(\s*\w+\s*,\s*"[^"]+"' runtime/*.cpp | sed 's/.*"\(.*\)"/\1/' | sort -u
grep -rhoP '^HLE\(\w+\)' runtime/*.cpp | sed 's/HLE(//;s/)//' | sort -u
```

### 8.6 NVN coverage

```bash
python3 - <<'EOF'
import re,glob
impl=set()
for f in glob.glob('Work/port/totk-recomp/runtime/nvn/*.c'):
    impl|=set(re.findall(r'^NVN\(([A-Za-z0-9_]+)\)',open(f).read(),re.M))
d=open('Work/data/exefs/main.decompressed','rb').read()
names=set(x.decode() for x in re.findall(rb'nvn[A-Z][A-Za-z0-9]{2,40}',d))
print(len(impl),len(names),len(names&impl)); print(sorted(names-impl))
EOF
```

### 8.7 FDE / instruction counts

`tools/funcs.py` needs an `nso.py` JSON; the same numbers without writing files:

```bash
python3 - <<'EOF'
import struct
d=open('Work/data/exefs/main.decompressed','rb').read()
pre=struct.unpack_from('<I',d,0x10)[0]; img=d[pre:]
mod0=img.find(b'MOD0'); eh=mod0+struct.unpack_from('<i',img,mod0+16)[0]
print('eh',eh,'enc',img[eh:eh+4].hex())
EOF
# then the DWARF sdata4 walk from tools/funcs.py lines 26-54
```

(`Work/gen/main/report.json` will give exact `functions`/`insns`/`unhandled` once
`gen_all.sh` has run; it does not exist yet.)
