# Splatoon 3 — SDK 20.5.22 ABI notes (static analysis)

Scope: the six questions in the task. Evidence = Splat3 `sdk` (base `0x7180000000`,
6,698,752 B text) and `main` (base `0x7100000000`, 82,376,320 B text), disassembled with
capstone 5.0.9 from `Work/data/img/{sdk,main}.bin`; addresses are guest (image) addresses.
Runtime references are `Work/port/Splat3-Recomp/baseline/totk-recomp/runtime/…`.

Method notes worth keeping:

* `main` has **no** PLT-`br x17` stubs at the image start — its 1,131 import stubs live at
  `0x7104e8afd0 … 0x7104e8f670` (16 B each: `adrp x16,g; ldr x17,[x16,#off]; add x16,…; br x17`),
  i.e. in the 4 KB hole between `.text` end (`0x7104e8a000`) and `.rodata` (`0x7104e90000`).
  GOT/`.got.plt` = `0x7105f63000 + 8·i` (`DT_JMPREL` r_offsets `0x5f63000…0x5f65xxx`).
  Import *i* → stub = PLT-order, GOT slot = `DT_JMPREL[i].r_offset`.
* `main` resolves its NVN procs **by name at init** into a BSS proc table (device table at
  `0x710601e5f0+`), so NVN "imports" are not GOT calls — see §4.

---

## 1. `nn::os::MemoryInfo` — CONFIRMED, but Splat3 only reads `[0]` and `[1]`

`_ZN2nn2os6detail18DebugImplByHorizon15QueryMemoryInfoEPNS0_10MemoryInfoE`
**sdk `0x718014716c`** (192 B), reached from the public `_ZN2nn2os15QueryMemoryInfoEPNS0_10MemoryInfoE`
(`0x7180143760`, 4 B: `b 0x718064c960` → sdk PLT).

```
7180147180  mov w1, #0x16          ; svcGetInfo(InfoType 22, CUR_PROCESS 0xFFFF8001, Id 0)
71801471a4  mov w1, #0x15          ; svcGetInfo(InfoType 21, …)
71801471ec  stp x9, x20, [x19]     ; +0x00 = info22 , +0x08 = info21
71801471f0  str x10, [x19, #0x20]  ; +0x20 = info21 − ([g_os+0x3f8]+[g_os+0x150]), cached in g_os+0x5a0 = free
71801471fc  str x9,  [x19, #0x10]  ; +0x10 = [g_os+0x3f8]  (heap size, os bookkeeping)
7180147204  str x9,  [x19, #0x18]  ; +0x18 = [g_os+0x400]  (heap used)
718014720c  str x9,  [x19, #0x28]  ; +0x28 = [g_os+0x2d0]  (reserved/peak)
7180147214  str w8,  [x19, #0x30]  ; +0x30 = u32 [g_os+0x2d8]
```

Field offsets: **+0x00 total physical, +0x08 used physical, +0x10 heap size, +0x18 heap used,
+0x20 heap free, +0x28 heap reserved/peak, +0x30 u32**. Highest byte written = `+0x33`;
the caller's frame puts the struct in `sp+8 … sp+0x40` → **struct size 0x38**. Identical to the
Splat2 (SDK 1.6.0) layout the runtime already implements.

Only two `QueryMemoryInfo` call sites exist in `main` (both PLT `0x7104e8dff0`):

* **`main 0x710442f0e0`** (nn::os heap bring-up): `bl QueryMemoryInfo(sp+8)` then
  `0x710442f190 ldp x10,x9,[sp,#8]` / `sub x8,x10,x9` / `and x0,x8,#~0x1FFFFF` →
  `SetMemoryHeapSize`. **It reads only `[0]` and `[1]`** (see §6).
* **`main 0x710453ddfc`** (NVN heap ctor): `0x710453de24 ldp x19,x23,[sp,#8]` then
  `sub x8,x19,x23 / and x4,x8,#~0xFFF` → NVN heap size. Again only `[0]`, `[1]`.

`+0x10/+0x18/+0x20` (the heap triple the runtime cares about) are **not read anywhere in
`main`**; they are written by the sdk impl only. Keeping them sane is harmless and still correct.

**VERDICT 1 — matches runtime.** `hle_os_mem.cpp:34-42` needs no offset change; only worth a
comment that Splat3 consumes `mi[0]/mi[1]` (so `kAvailable=0xD0000000`,
`kProgram=0x5000000` are the load-bearing constants, `hle_os_mem.cpp:16-18`).

---

## 2. `nn::os::ThreadType` (SDK 20) — size 0x1C0; stack base **+0x50**, stack size **+0x58**

Size: `_ZN2nn2os6detail23SetupThreadObjectUnsafeEPNS0_10ThreadTypeEPFvPvES4_S4_mi`
**sdk `0x71801418d0`** starts with `mov w2,#0x1c0; bl memset` → **`sizeof(ThreadType) = 0x1C0`**
(the runtime's 0x200 `_aligned_malloc` is merely oversized, which is fine).

Field map written by that function (x19 = ThreadType*; args x1=fn, x2=arg, x3=stack, x4=stackSize, w5=prio):

| off | size | what | evidence |
|---|---|---|---|
| +0x00/+0x08 | 2×u64 | intrusive list node, self-linked | `7180141928 stp x19,x19,[x19]` |
| +0x10/+0x18 | 2×u64 | waiter-list node, self-linked | `718014194c stp x8,x8,[x19,#0x10]` (x8=tt+0x10) |
| +0x40 | u8 | **state** (1 = initialised; **3 = "has stack info"**) | `7180141944 strb w25(=1),[x19,#0x40]`; `GetThreadStackInfo` requires `==3` |
| +0x41/+0x42 | u8/u16 | flags / suspend-ish, 0 | `718014192c`, `7180141938` |
| +0x44 | **s16** | **priority** (read `ldrsh` by the manager, `+0x1c` added for svcCreateThread) | `7180141930 strh w20,[x19,#0x44]`; `7180148134 ldrsh w10,[x1,#0x44]` |
| +0x46 | u16 | unknown (1) | `7180141914 strh w25,[x19,#0x46]` |
| +0x48 | void* | **original stack base** (saved; restored into +0x50 by `DeleteAliasStackUnsafe`) | `7180141958 stp x22,x22,[x19,#0x48]`; `7180141aa0 ldr x8,[x19,#0x48]; str x8,[x19,#0x50]` |
| +0x50 | void* | **live stack base** ← everything reads this (alias VA while aliased) | `stp x22,x22,[x19,#0x48]`; `CreateAliasStackUnsafe 7180141a6c str x8,[x19,#0x50]` |
| +0x58 | size_t | **stack size** | `718014195c stp x21,x23,[x19,#0x58]` (x21=stackSize) |
| +0x60 | void* | thread argument | same `stp`, x23=arg |
| +0x68 | fn ptr | entry point | `7180141964 stp x24,xzr,[x19,#0x68]` |
| +0x70 | void* | alternate stack-info provider (0 in the normal path; `GetThreadStackInfo` then uses `[x9+0x40]`/`[x9+0x48]`) | `7180141964 stp x24,xzr,[x19,#0x68]` (+0x70 = 0); read at `0x718014476c` |
| +0x78…+0xF8 | 0x100 | svc thread context (`ThreadContextInfoAarch64`) | zeroed `+0x78…+0x178` |
| +0x100 | u8 | `FsInlineContext` (atomic; `nn::sf::Set/GetFsInlineContext` = `add x0,#0x100`) | sdk `0x7180151690` |
| +0x180 | char[0x20] | **inline name buffer** | `Setup` `str x8(=tt+0x180),[x19,#0x1a0]`; `SetThreadName 0x7180140990` copies ≥0x20 |
| +0x1a0 | const char* | **name pointer** (`GetThreadNamePointer` = `ldr x0,[x0,#0x1a0]` @ `0x7180140c10`) | `0x7180140be8 str x1,[x8,#0x1a0]` |
| +0x1a8/+0x1ac | — | internal light-lock/atomic members (ctor `0x718064be90`/`bea0`) | `7180141910/718014191c` |
| +0x1b0 | u32 | **core number** (svcGetThreadCoreNumber) | `7180147fec`…`7180148020 str w8,[x19,#0x1b0]` |
| +0x1b4 | u32 | 0 | `7180141934` |
| +0x1b8 | u64 | **thread handle** (svcCreateThread out) | `7180148184`…`7180148190 str x8,[x19,#0x1b8]` |

Proof that **+0x50/+0x58** are the live stack range (three independent readers):

```
ThreadManagerImplByHorizon::CreateThread  sdk 0x7180148114:
  718014812c  ldp x8, x9, [x1, #0x50]   ; x8 = stack base, x9 = stack size
  7180148150  add x21, x9, x8           ; sp = base + size  → svcCreateThread(x3 = x21-0x20)

GetThreadStackInfo  sdk 0x7180144760:
  7180144760  ldrb w8,[x2,#0x40] / cmp #3 / b.ne → returns (0,0) unless state==3
  7180144788  add x10,x2,#0x50 … ldr x9,[x9]  → *base_out = [tt+0x50]  (or [impl+0x40])
  7180144770  add x8,x2,#0x58  … ldr x8,[x8]  → *size_out = [tt+0x58]  (or [impl+0x48])

DebugImplByHorizon::GetCurrentStackInfo  sdk 0x71801470d8  ← what main actually reaches:
  71801470d8  mrs x8, tpidrro_el0
  71801470dc  ldr x9, [x8, #0x1f8]      ; [TLS+0x1F8] = current ThreadType*
  71801470e0  ldr x10,[x9, #0x70]       ; alternate provider (0 in the normal path)
  71801470e4  add x11,x9,#0x50 / 71801470e8 add x9,x9,#0x58
  71801470f0  add x12,x10,#0x40 / 71801470f8 add x10,x10,#0x48
  7180147108  ldr x9,[x11] … ldr x10,[x10]  → base/size out
```

**TLS slot: none in `ThreadType`.** Per-thread TLS is the 0x200-byte `ThreadLocalRegion` at
`TPIDRRO_EL0`, and **the current `ThreadType*` pointer lives at TLS `+0x1F8`**
(`0x71801470dc`); `+0x1F0` holds a second internal pointer (ctor
`0x7180148038 mrs tpidrro_el0; ldr x8,[x8,#0x1f0]; msr tpidr_el0,x8`). `nn::os::TlsSlot`
values are process-global (`AllocateTlsSlot`/`GetTlsValue`), not stored in `ThreadType`.

`main` side: main has **no direct `[x,#0x50]` stack-base read** any more. Its sead
`MemUtil::isStack` equivalent now calls `nn::os::GetCurrentStackInfo` and range-checks
(`main 0x7103dbef34`, also inlined at `0x7103dba22c`, `0x7103dd2a0c`, `0x7103de3278`,
`0x7103de3c28`, …):

```
7103dbf010  bl 0x7104e8da40            ; nn::os::GetCurrentStackInfo(&base,&size)
7103dbf014  ldr x8,[x29,#0x18]         ; base
7103dbf020  ldr x9,[sp,#8]             ; size
7103dbf028  add x9,x9,x8               ; base+size
7103dbf02c  cmp x21,x9
7103dbf030  ccmp x21,x8,#0,lo          ; base <= ptr < base+size
```
(Splat2's `main 0x13a1f30` read `[[[g]+0x80]+0xe0]+0x50` directly; SDK 20 routes it through
`GetCurrentStackInfo`. The runtime HLEs that call at `hle_os_thread.cpp:211`, so the
`+0x50` requirement only bites when the sdk body is used — `TOTK_LIFT_ALL=1`.)

**VERDICT 2 — base offset CONFIRMED (+0x50), but the runtime must also write `+0x58`**
(stack size) and, for the lifted-sdk path, `+0x40 = 3` (the state `GetThreadStackInfo`
requires) plus `+0x48` (saved base) / `+0x41 = 0` (not-aliased); and `rt_tpidrro()` must
publish the current `ThreadType*` at TLS `+0x1F8`.
**Change:** `hle_os_thread.cpp:142` and `:151` (`*(u64*)(tt+0x58)=stack_size; *(u64*)(tt+0x48)=stack;
*(u8*)(tt+0x40)=3; *(u8*)(tt+0x41)=0;`), `hle_os_thread.cpp:111` (same for the main thread),
`rt_main.cpp:260` (`rt_tpidrro`: write `t_self->type` at `tlr+0x1F8`, 0 at `+0x1F0`).
The existing `+0x50` writes stay as they are.

---

## 3. Event / Semaphore / MessageQueue layouts (offsets the sdk bodies touch)

All three are PODs whose bodies are in `sdk`; the runtime never reads them (host-side
`HThread`/`HEvent`/`HSema`/`HQueue` registries keyed by the guest address), so this section is
informational — but it is what a *lifted* `nn::os` body would require.

**`EventType`** — `InitializeEvent` sdk `0x718013bdc0`, `WaitEvent` `0x718013bf00`,
`SignalEvent` `0x718013be30`, `ClearEvent` `0x718013bed0`:

| off | type | meaning |
|---|---|---|
| +0x00/+0x08 | u64 | waiter list prev/next, self-linked (`718013bdf0 stp x21,x21,[x21]`) |
| +0x10 | u8 | **signalled flag** (`WaitEvent`: `718013bf20 ldrb w8,[x20,#0x10]; cbnz` → skip wait) |
| +0x11 | u8 | second copy of the initial-signal bool (written by `InitializeEvent`, not read by Wait/Signal/Clear) |
| +0x12 | u8 | **clear mode** (`1` = auto-clear → `WaitEvent` `718013bf5c strb wzr,[x20,#0x10]`) |
| +0x13 | u8 | initialised (1) |
| +0x14 | u64 (unaligned) | **signal counter** (`SignalEvent` `adds w8,w8,#1` at +0x14, carry → +0x18; `WaitEvent` `ldur x22,[x20,#0x14]`) |
| +0x1c | — | internal critical section (`InitializeEvent 718013bdd4 add x0,x0,#0x1c`) |
| +0x20 | — | internal condition variable (`718013be94 add x0,x20,#0x20`) |

**`SemaphoreType`** — `InitializeSemaphore` sdk `0x718013fc30`, `ReleaseSemaphore` `0x718013fe10`:

| off | type | meaning |
|---|---|---|
| +0x00/+0x08 | u64 | waiter list |
| +0x10 | u8 | initialised (1) |
| +0x14 | s32 | **count** (`ReleaseSemaphore 718013fe30/3c: ldr w8,[x20,#0x14]; add #1; str`) |
| +0x18 | s32 | **max count** (`InitializeSemaphore 718013fc64 stp w20,w19,[x21,#0x14]`) |
| +0x1c / +0x20 | — | critical section / condition variable |

**`MessageQueueType`** — `InitializeMessageQueue` sdk `0x718013cf80`, `JamMessageQueue` `0x718013d2a0`:

| off | type | meaning |
|---|---|---|
| +0x00/+0x08 | u64 | waiter list (self) |
| +0x10/+0x18 | u64 | receive-waiter list (self, `stp x8,x8,[x21,#0x10]`) |
| +0x20 | u64* | **buffer** (`InitializeMessageQueue str x20,[x21,#0x20]`, used `str x21,[x11,w9,sxtw#3]`) |
| +0x28 | s32 | **capacity** (`ldp w9,w8,[x20,#0x28]; cmp w8,w9; b.lt` = full loop) |
| +0x2c | s32 | **current count** |
| +0x30 | s32 | **jam index** (push-front: `w9 = (jam>0 ? count : 0) + jam − 1` → `stp w8,w9,[x20,#0x2c]`) |
| +0x34 | u8 | initialised (1) |
| +0x38 | — | critical section (`add x0,x0,#0x38`) |
| +0x3c / +0x40 | — | condition variables (full / not-empty) |

**VERDICT 3 — matches runtime** (the HLEs at `hle_os_thread.cpp:432,467,469,529,593,600` are
address-keyed host state; no guest offsets are read or written, so the SDK-20 layout cannot
break them). Only note the jam/push-front semantics confirmed: `+0x30` index, `+0x2c` count
(`hle_os_thread.cpp:600` `q_send(..., jam=true)` already models it).

---

## 4. NVN API version: **major 55, minor ≥ 15** — the TotK default is correct

`nvnBootstrapLoader` is a real `main` import (GOT `0x5f647e8`, stub `0x7104e8dfa0`).
Exactly two call sites, both in the NVN device bring-up **`main 0x71045299ac`** (2,988 B):

```
7104529acc  adrp x0,#0x7104ff6000 / add #0x4fe  ; "nvnDeviceInitialize"
7104529aec  bl 0x7104e8dfa0                     ; nvnBootstrapLoader("nvnDeviceInitialize") → x24
7104529af4  adrp x0,#0x7104f24000 / add #0xbc6  ; "nvnDeviceGetProcAddress"
7104529afc  bl 0x7104e8dfa0                     ; → x23
7104529b14  bl 0x7100a79670                     ; fill the 501-entry device proc table (below)
7104529b18  ldr x8,[0x7105f47058] / ldr x8,[x8] ; nvnDeviceBuilderSetDefaults
7104529b68  ldr x8,[0x7105f47060] / ldr x8,[x8] ; nvnDeviceBuilderSetFlags
           …
7104529bc0  ldr x27,[0x7105f46d28]             ; slot = nvnDeviceGetInteger
7104529bd0  mov w1,wzr                          ; which = 0  (NVN_DEVICE_INFO_API_MAJOR_VERSION)
7104529bd4  ldr x8,[x27] / blr x8                ; DeviceGetInteger(dev, 0, &out)  [x0=dev, x2=sp+0x10]
7104529be8  mov w1,#1                           ; which = 1  (…_API_MINOR_VERSION)
7104529bec  blr x8                               ; → sp+0xc
7104529bf0  ldr w8,[sp,#0x10]
7104529bf4  cmp w8, #0x37                        ; ← ★ major must be 55
7104529bf8  b.ne #0x710452a504                   ;    (abort the device init)
7104529bfc  ldr w8,[sp,#0xc]
7104529c00  cmp w8, #0xf                         ; ← ★ minor must be >= 15
7104529c04  b.lt #0x710452a504
```

So the game **validates and rejects** the version (returns from the whole NVN init on
mismatch — no print, no proc selection; failure leaves the renderer uninitialised).
Expected pair: **55 / ≥15 — exactly `device_integer()`'s `[0]=55,[1]=15`
(`nvn/objects.c:17`)**. Nothing to override, `TOTK_NVN_INT` is *not* needed for Splat3
(unlike Splat2's 53).

The proc table is filled by **`main 0x7100a79670`** (14,980 B): 501 `getProcAddress(dev,name)`
calls, each result stored through the pointer at `main .data 0x7105f47000+off` into the BSS
table at `0x710601e5f0+`. Pattern per proc (`…/main 0x7100a79680`):
`adrp x1,str` / `add x1` / `ldr x8,[0x7105f470xx]` / `str x0,[x8]` / `mov x0,dev` / `blr x19`
— the *name* in a block is fetched by that block's `blr` and stored by the **next** block, so
`name(block N) → offset(block N+1)`. Verified head of the table:

| g_off | proc (string) | string addr |
|---|---|---|
| 0x58 | `nvnDeviceBuilderSetDefaults` | `0x7104ef2708` |
| 0x60 | `nvnDeviceBuilderSetFlags` | `0x710501fa54` |
| 0x68 | `nvnDeviceBuilderGetFlags` | `0x7104fc5f5c` |
| 0x70 | `nvnDeviceInitialize` | `0x7104ff64fe` |
| 0x78 | `nvnDeviceFinalize` | `0x7104f423c0` |
| 0x80 | `nvnDeviceSetDebugLabel` | `0x7104fbbb52` |
| 0x88 | `nvnDeviceGetProcAddress` | `0x7104f24bc6` |
| 0xd28 (`0x7105f46d28`, table+0x30) | **`nvnDeviceGetInteger`** | `0x7104f882e3` |

⇒ `x27` in the `cmp` block above *is* `nvnDeviceGetInteger`, called
`DeviceGetInteger(device, which, int* out)` (out at `x2`; note the 3-argument form — the
return value is ignored, the game reads `*out`).

Other device constants the same function asks for (via the same `DeviceGetInteger` slot):
`0x14`(20), `0x18`(24), `0x19`(25), `0x1a`(26) — used for size/alignment maths at
`0x710452a1e8…0x710452a22c`; and `0x55`(85), `0x52`(82), `0x5c`(92) — used as the *default*
queue command/compute/control memory sizes when `[dev+0xc0..0xc8] == -1`
(`0x7104529d7c/0x7104529dbc/0x7104529dfc`). `device_integer()` only defines indices < 64, so
82/85/92 read back **0**. Harmless for the current Vulkan shim (those setters are no-ops), but
they must be filled if the shim ever honours queue memory sizes.

**VERDICT 4 — matches runtime.** `nvn/objects.c:15-17` (`[0]=55,[1]=15`) is exactly what
Splat3 demands; `nvn/objects.c:67-72` already writes the out-pointer (`AP(2)`), which is the
calling convention used here (3-arg form: `device, which, int* out`). No change. Follow-up
(non-blocking): add `device_integer` entries for 82/85/92.

---

## 5. `nn::audio::AudioRendererParameter` in SDK 20 — struct is **0x38**, fields unchanged, two new checks

`InitializeAudioRendererParameter` **sdk `0x7180218830`** (88 B):

```
7180218840  stp xzr,xzr,[x0,#0x28] / 7180218844 str xzr,[x0] / 7180218848 stp [x0,#0x18] / 718021884c stp [x0,#8]
            → memset(p, 0, 0x38)                       ★ size 0x38 (runtime uses 0x34)
7180218850  bl 0x7180650e40                              ; = nn::audio::common::GetCurrentRevision()
718021885c  str w0, [x19, #0x34]                          ★ +0x34 = u32 revision
7180218870  stp x8, x9, [x19]     (x8 = 0x000000F00000BB80, x9 = 1)
            → +0x00 = 0xBB80 (48000), +0x04 = 0xF0 (240), +0x08 = 1
7180218874  mov x8,#0x100000001 / 7180218878 stp x8,xzr,[x19,#0x10]
            → +0x10 = 1, +0x14 = 1, +0x18 = 0
7180218864  strb wzr,[x19,#0x20]; 7180218868 stur xzr,[x19,#0x24]; 718021886c stur xzr,[x19,#0x2c]
```

`IsValidAudioRendererParameter` **sdk `0x7180218730`** (256 B) reads, in order:

```
7180218740  ldp w8,w9,[x0,#0x2c]      ; +0x2c, +0x30
7180218748  ldr w21,[x0,#0x34]        ; revision
718021875c  (w8==1 && w9==1) || (w8==0 && w9==0)   ← ★ NEW check pair
7180218768  bl 0x7180650e40 ; 718021876c cmp w21,w0 ; b.ne → 0   ★ +0x34 must equal live revision
7180218774  +0x00 == 48000 (0xBB80) or 32000 (0x7D00)
7180218790  +0x04 == 240 (0xF0) or 160 (0xA0)
71802187a4  sampleCount == ((sampleRate>>3)*5243)>>17    (i.e. exactly rate/200)
71802187b8  +0x08 >= 1     71802187e8 < 0x101
71802187c4  +0x10 >= 1     71802187f4 < 0x401
71802187d0  +0x14 >= 1     71802187fc < 0x101
71802187dc  +0x18 >= 0     7180218800 < 0x101
71802187e4  +0x0c >= 0     7180218804 < 0x101
```

Field map: **+0x00 sampleRate, +0x04 sampleCount, +0x08 mixBufferCount, +0x0c subMixCount,
+0x10 voiceCount, +0x14 sinkCount, +0x18 effectCount, +0x1c (untouched), +0x20 u8
(isVoiceDropEnabled, 0), +0x24 u32, +0x28 u64, +0x2c/+0x30 u32 (must match each other, 0 or 1),
+0x34 u32 revision.** So the 3.x numeric layout the runtime implements (p[0..6] = +0x00…+0x18)
**is unchanged in SDK 20** — only the size grew (0x34→0x38) and the two trailing fields are
newly validated.

Impact for Splat3: `main` imports `InitializeAudioRendererParameter` (GOT `0x5f649f0`) but
**not** `IsValidAudioRendererParameter`, and `OpenAudioRenderer` is HLE'd, so the missing
`+0x34` revision and the `0x34`-byte memset are not fatal today. They break as soon as the sdk
`nn::audio` bodies are lifted or if the game copies the struct and revalidates.

**VERDICT 5 — needs a (small) change:** `hle_audio.cpp:571` `memset(…, 0x34)` → `0x38`;
set `p[0x34/4]` (i.e. `*(u32*)(p+0x34)`) to the revision constant the sdk uses
(`nn::audio::common::GetCurrentRevision()`; a constant per SDK — any fixed value works as long
as the same value is used for `+0x34` and in the check), and add the
`{+0x2c,+0x30}` (both-0 or both-1) + `+0x34 == revision` clauses to
`hle_audio.cpp:576-584`.

---

## 6. Heap: `SetMemoryHeapSize` argument is computed, not constant → **0xCB000000**; then 0x2200000 carved out

Only one call site of `_ZN2nn2os17SetMemoryHeapSizeEm` (GOT `0x5f63710`, stub
`0x7104e8bdf0`): **`main 0x710442f1c8`**, inside `main 0x710442f0e0` (nn::os bring-up):

```
710442f0f0  mov w0,#1 / bl 0x7104e8bde0      ; nn::oe::SetCpuBoostMode(1)
710442f108  mov w9,#0x2200000                ; 34 MiB
710442f120  str w9,[x8]                      ; g_alloc_block_size = 0x2200000
710442f130  bl 0x7104e8dff0                   ; nn::os::QueryMemoryInfo(&mi)      [mi at sp+8]
710442f190  ldp x10, x9, [sp, #8]             ; x10 = mi[0] (total), x9 = mi[1] (used)
710442f1ac  sub x8, x10, x9
710442f1b8  and x0, x8, #0xffffffffffe00000   ; (total − used) & ~2 MiB  ← the argument
710442f1c8  bl 0x7104e8bdf0                   ; nn::os::SetMemoryHeapSize(x0)
710442f1cc  mov w19,#0x2200000
710442f1d8  bl 0x7104e8be00                   ; nn::os::AllocateMemoryBlock(&blk, 0x2200000)
710442f1f0  bl 0x7100a77a80                   ; …then the engine's root-heap ctor over it
710442f204  bl 0x7104e8be10                   ; nn::os::SetMemoryAllocatorForThreadLocal(fn,fn)
```

With the runtime's own `MemoryInfo` values (`kAvailable = 0xD0000000`,
`kProgram = 0x5000000`, `g_heap_used = 0` at that point — `hle_os_mem.cpp:16-18,37`):

```
heap = (0xD0000000 − 0x5000000) & ~0x1FFFFF = 0xCB000000 = 3,405,824,000 B (3.17 GiB)
```

`0xCB000000` is already 2 MiB-aligned, so no rounding is lost; the following
`AllocateMemoryBlock(0x2200000)` = 35,651,584 B (34 MiB) is committed out of it, and the NVN
heap ctor (`main 0x710453ddfc`) later takes `(mi[0]−mi[1]) & ~0xFFF` as its own size.
Consequence: the runtime's `kAvailable`/`kProgram` are the only knobs — shrinking either
shrinks the game heap 1:1, and `mi[1]` must stay well below `mi[0]`
(a zero difference would call `SetMemoryHeapSize(0)`).

**NPDM (`Work/data/exefs/main.npdm`, 1,636 B, META):** process name `Application`,
ACI0 @ `0x470` (size `0x1F4`, program id `0x0100C2500FC20000`), ACID @ `0x280`
(magic `ACID`, body size `0x2E4`, version 0). ACID fields: `+0x0C = 3`,
`+0x10 = 0x10000`, `+0x14 = 0x1000000`, `+0x18 = 0xFFFFFFFF`, `+0x1C = 0x01FFFFFF`, then the
three (offset,size) capability tables `+0x20 = (0x240,0x2C)` FsAccess,
`+0x28 = (0x270,0x144)` ServiceAccess, `+0x30 = (0x3C0,0x24)` KernelAccess.
Splat2's NPDM (`…/Splat2/work/base-nsp/exefs/main.npdm`) has **identical values** at every one
of those offsets (`0x10000`/`0x1000000`/`0x3`), i.e. there is **no title-specific
`system_resource_size` here** — nothing is reserved for a system resource (treat as 0), and it
is not consulted by this heap path anyway.

**VERDICT 6 — no runtime change needed.** `hle_os_mem.cpp:126-165` (`SetMemoryHeapSize`,
reserve `0xCB000000`) and `:166-179` (`AllocateMemoryBlock`, 34 MiB bump) already satisfy the
sequence; the load-bearing constants are `hle_os_mem.cpp:16-18` and the `mi[0]/mi[1]` values at
`hle_os_mem.cpp:37`.

---

## Summary of runtime edits implied by the above

| # | verdict | file:line | edit |
|---|---|---|---|
| 1 | matches | `hle_os_mem.cpp:34-42` | none (only a comment: Splat3 reads `mi[0]/mi[1]`) |
| 2 | **needs change** | `hle_os_thread.cpp:142`, `:151`, `:111` | add `+0x58`=stack_size, `+0x48`=stack, `+0x40`=3, `+0x41`=0; keep `+0x50` |
| 2 | **needs change** | `rt_main.cpp:260` | `rt_tpidrro`: store current `ThreadType*` at `tlr+0x1F8` (`+0x1F0` = internal copy) |
| 3 | matches | `hle_os_thread.cpp:432-611` | none (host-side state only) |
| 4 | matches | `nvn/objects.c:15-17`, `:67-72` | none (55/15 + out-pointer already right) |
| 4 | follow-up | `nvn/objects.c:15` | add `[82]`,`[85]`,`[92]` (queue cmd/compute/control sizes) |
| 5 | **needs change** | `hle_audio.cpp:571` | `memset 0x34` → `0x38`, set `+0x34` = revision |
| 5 | **needs change** | `hle_audio.cpp:576-584` | add `+0x2c/+0x30` pair check and `+0x34 == revision` |
| 6 | matches | `hle_os_mem.cpp:16-18,126-179` | none (heap = 0xCB000000, block = 0x2200000) |
