# Splatoon 3 (v11.3.0 update) - extraction report

Date: 2026-11-15 (second pass - the target is now the update NSP)
Host dir: `/mnt/SSD Storage/Games/Ports/Switch/Splat3/Work` (`WORK`)
Input: `NSPs/Splatoon 3 [0100C2500FC20800][v2752512][Up v11.3.0].nsp` (7,267,881,803 bytes)
       plus `NSPs/Splatoon 3 [0100C2500FC20000][USA][v65536].nsp` (3,320,366,404 bytes) as the BKTR base
Keys: `prod.keys` (filtered copy, chmod 600, deleted at the end; values never printed)

## Result

| artifact | size | notes |
|---|---|---|
| `WORK/data/exefs/main` | 51,859,062 | NSO0, build id `28c4287a...` |
| `WORK/data/exefs/main.decompressed` | 100,687,000 | hactool `-t nso --uncompressed` |
| `WORK/data/exefs/main.npdm` | 1,636 | META/ACI0/ACID |
| `WORK/data/exefs/sdk` | 6,204,231 | NSO0, build id `574284b4...` |
| `WORK/data/exefs/sdk.decompressed` | 12,080,616 | |
| `WORK/data/exefs/rtld` | 10,109 | NSO0, build id `263c3426...` |
| `WORK/data/exefs/rtld.decompressed` | 20,744 | |
| `WORK/data/romfs/` | 22,857 files / 10,058,171,877 B (9.37 GiB), 127 dirs | merged v11.3.0 tree |
| `WORK/data/control_romfs/` | 1.4 MB | control.nacp + 14 icons (base NCA) |
| `WORK/data/nsp/` | - | `.tik`/`.cert` of both titles + `manifest.txt` |

Title: `0100c2500fc20800` (patch) / `0100c2500fc20000` (base), version **2752512 (0x2A0000) = 11.3.0**
(confirmed by `control.nacp` "version" string and the CNMT `Patch` record).
Program NCA `b17c38424e33cec90d4407ef33c7848d.nca`, 7,266,187,264 B, keygen 14.

## 1. ExeFS

Unpack (PFS0, `scripts/pfs0_extract.py`), then:

```
ncainfo  <keys> <tik> b17c3842....nca        # shows section layout / patch info
ncaextract <keys> <tik> b17c3842....nca /w/data/exefs -       # LibHac 0.19 (Code section)
hactool -t nso --uncompressed=/w/data/exefs/<f>.decompressed /w/data/exefs/<f>
```

(plain `hactool -t nca --exefsdir` cannot be used: it aborts on the BKTR romfs section
with `Invalid BKTR layout!` before it saves anything.)

The update's ExeFS section is a **complete** PFS0 (58.1 MB, `layer1 offset 0x8000,
size 0x376283e`), not patched, so it extracts standalone. Modules:

| file | size | decompressed | build id (bytes 0x40..0x60) | segments |
|---|---|---|---|---|
| main | 51,859,062 | 100,687,000 | `28c4287aee36f7499da60f3e68b54c70da382d75000000000000000000000000` | text 0x0-0x4e90000, rodata 0x4e90000-0x5ac8000, rwdata 0x5ac8000-0x6006000, bss 0x6006000-0x61fa000 |
| sdk | 6,204,231 | 12,080,616 | `574284b40c06cf1821a44857939083bee5716820000000000000000000000000` | text 0x0-0x664000, rodata 0x664000-0xb30000, rwdata 0xb30000-0xb86000, bss 0xb86000-0xc56000 |
| rtld | 10,109 | 20,744 | `263c3426d28af8d8cce29a9d2da8ff0ff9a76456000000000000000000000000` | text 0x0-0x3000, rodata 0x3000-0x4000, rwdata 0x4000-0x6000, bss 0x6000-0x7000 |
| main.npdm | 1,636 | - | - | - |

No `subsdk*` modules in this title (only main + sdk + rtld).
All `.decompressed` files keep the `NSO0` header and have text/rodata/data laid out
contiguously (offsets 0, 0x…, … as listed; `scripts/nso_info.py` prints/validates this).

### SDK / NVN strings

sdk:
```
SDK MW+Nintendo+NintendoSDK_NVN-20_5_22-Release
SDK MW+Nintendo+NintendoSdk_nnSdk-20_5_22-Release
SDK MW+Nintendo+NintendoSDK_libz-20_5_22-Release
SDK MW+Nintendo+NintendoSdk_BuildTarget-64Bit
```
main:
```
SDK MW+Nintendo+NintendoSDK_libcurl-20_5_22-Release
SDK MW+Nintendo+NintendoSDK_NPLN-1_38_1-Release
SDK MW+Nintendo+NintendoSDK_Google_APIs_For_NPLN-1_38_1-Release
SDK MW+Nintendo+NintendoSDK_OpenSSL_For_NPLN-0_0_0-Release
SDK MW+Nintendo+NintendoSDK_gRPC_For_NPLN-0_0_0-Release
NintendoSDK default user interface
__nnDetailNintendoSdkNsoFile
```
i.e. **SDK 20.5.22** with NVN and **NPLN 1.38.1** (Nintendo's newer network stack);
`NEX-for_NX` is *not* present (this title uses NPLN, not NEX).

### NPDM (main.npdm)

```
name                     Application
title id                 0x0100c2500fc20000
title id range           0x0100000000010000 - 0x01ffffffffffffff
main thread stack size   0x00100000 (1 MiB), priority 44, default cpu 0
is 64 bit                true, address space type 3, pool partition 0
process category         0, is_retail true
filesystem access        0x4000000000000000
service access (49)      acc:u0 aoc:u apm appletOE audin:u audout:u audren:u banana bcat:u
                         bsd:u bsdcfg caps:su caps:u csrng ectx:aw friend:u fsp-srv hid htc
                         htc:tenv htcs hwopus irs ldn:u ldr:ro lm mii:u mm:u mnpp:app nfc:mf:u
                         nfc:user nfp:user ngc:u ngct:u nifm:u nim:eca notif:a nsd:u ntc nvdrv
                         pcm pctl pl:u prepo:u set sfdnsres ssl time:u vi:u
kernel caps              kernel_flags (prio 59 max / 28 min, cpu 0..2), syscalls (full list),
                         application_type, min_kernel_version, handle_table_size, debug_flags
```
Full machine-readable dump: `WORK/logs/main.npdm.json` (hactool `-t npdm --json`).

## 2. RomFS (BKTR merge)

The update's RomFS section is an **AesCtrEx** ("BKTR" INDIRECT) patch over the base
title's RomFS: `relocation tree` maps each merged virtual range either to the *patch*
image (5748 ranges, 6.71 GiB) or to the *base* image (674 ranges, 2.67 GiB);
the `encryption tree` gives per-subsection AES-CTR "generation" values.

Nothing off the shelf worked:
* `hactool 1.4.0` rejects the layout (`Invalid BKTR layout!`: it requires the two trees to
  be adjacent and to end at the section end; here there is patch data between them and
  0x200 slack after them).
* `LibHac 0.19` (`nca.OpenFileSystemWithPatch`) throws `IndexOutOfRangeException` on the
  8-bucket relocation tree; without a base it fails with `Offset must be a multiple of 16`.

So the merge is implemented in `WORK/scripts/merge_bktr.c` (validated against Atmosphere's
`NcaFileSystemDriver`/`AesCtrCounterExtendedStorage`/`SparseStorage`):

```
relocation entry  { u64 virt; u64 phys; u32 is_patch }          (stride 0x14)
encryption entry  { s64 offset; u8 encrypted; u8[3]; s32 generation } (stride 0x10)
virtual range [virt_i, virt_{i+1}) (last ends at tree total)

is_patch=1 -> update section @ (0x4000 + phys),
              iv = BE64(generation | secure<<32) || BE64((0x4000 + phys)/0x10)
              (generation from the encryption entry covering phys, secure = 2)
is_patch=0 -> base section @ phys, resolved through the base SparseInfo bucket tree
              (holes = zeros), iv = BE64(0 | 2<<32) || BE64((0x1C000 + phys)/0x10)
```

Content keys (the non-obvious part, same rule as for the base title):
`content_key = AES-128-ECB-decrypt(titlekek[keygen-1], ticket.TitleKeyBlock[0x00:0x10])`
=> `titlekek_0d` for both NCAs here (keygen 14). Verified by reproducing LibHac's
`GetDecryptedTitleKey()` (sha256 `ee3891a2…`) and by decrypting the ExeFS PFS0.

Run:

```
# both content keys derived from the two tickets, then:
merge_bktr <update.nca> <base.nca> <update-key> <base-key> \
           reloc.bin enc.bin base_sparse.bin merged.bin      # 10.08 GB, ~17 s
romfs_extract.py merged.bin 0x12D8000 /w/data/romfs          # 22,857 files, ~18 s
```

Verification of the resulting tree:

* 33 top-level directories, 127 directories total, **22,857 files, 10,058,171,877 bytes**,
  no zero-length files.
* `Layout/` (593 files, 97 MB), `Model/` (3,287, 5.3 GB), `Sound/` (1,604, 2.6 GB),
  `System/` (6, 484 KB), `Pack/` (8,210, 88 MB), plus UI/, Bake/, Effect/, Shader/, Mals/, ...
* `System/RegionLangMask.txt` is plain text (`JPja USen USes … TWzh / Normal / b30`).
* 21,550 `*.zs` files are **Zstandard** frames (`28 b5 2f fd`), not Yaz0:
  `zstd -dc Pack/Params.pack.zs | head -c4` = `SARC`,
  `Layout/Bonus_01.Nin_NX_NVN.blarc.zs` -> `SARC`,
  `Mals/EUde.Product.b30.sarc.zs` -> `SARC`,
  `Model/Obj_TVMov005.bfres.zs` -> `FRES`.
  (so a port needs a zstd decoder; a Yaz0/SARC-only reader is not enough)

## 3. Base NSP is a stub (kept from the first pass)

`af15ab60a7a03a634ae69a48e3ea2a14.nca` (base Program NCA) declares an ExeFS section whose
SparseInfo has an **empty entry table** (`physical_offset = EOF`), and its RomFS
directory/file meta tables fall inside a sparse hole; the file size is fully accounted for
by header+logo+stored romfs holes. Hence the base NSP alone yields neither code nor a
named file tree - it is only usable as the BKTR *base* (674 ranges, 2.67 GiB reused above).

## 4. Reproduction

`WORK/scripts/extract_splat3.sh` runs the whole pipeline (podman only, ~1 minute wall
clock: unpack 4 s, ExeFS 1 s, merge 17 s, romfs extraction 18 s):
PFS0 unpack of both NSPs -> hactool build -> key filtering -> ExeFS extraction
(LibHac) + NSO decompression + NPDM JSON -> BKTR tree extraction -> merge ->
RomFS extraction -> verification.

## 5. Warnings

* `prod.keys` has 33 malformed 34-hex-digit `*_master_kek_source_*` lines; hactool refuses
  the file unfiltered (`filter_keys.py` keeps 198/231).
* `hactool 1.4.0` has no ticket support (`--tik` does not exist) and no SparseInfo/BKTR
  support for these layouts; it is used only for NSO/NPDM/RomFS utilities here.
* Raw NCAs, the 10 GB merged image, the bucket trees and all key files were deleted after
  the run; peak extra disk usage was ~21 GB (merged image + extracted tree).
* Nothing was written into any git repository.
