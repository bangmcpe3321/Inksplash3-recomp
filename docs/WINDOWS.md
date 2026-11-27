# Native Windows build

This is an experimental native x64 Windows path. It reuses the existing Windows runtime and
renderer shims, with Clang targeting the MSVC ABI. The Linux build/container is unchanged. Use a
legally dumped **matching** Splatoon 3 ExeFS and merged RomFS; this repository does not extract,
distribute, or include game data or keys.

## Prerequisites

- 64-bit Windows, Visual Studio C++ tools with Clang and the Windows SDK, Python 3, CMake, Ninja,
  and Git on `PATH`.
- A CPU supporting x86-64-v3 (including AVX2); the generated runtime is compiled for that target.
- Vulkan SDK for a windowed build (the null/headless build does not need it).
- At least 16 GiB RAM is recommended. Generated guest code is large; keep build parallelism low on
  memory-constrained systems.
- An external work directory on a drive with tens of GiB free. Never place extracted game data,
  generated C++, keys, saves, or build output in this repository.

## Prepare external inputs

In PowerShell, set paths to a fresh work directory and the user's extracted, version-matched game
files. The ExeFS directory must contain `main` and `sdk` NSOs; the RomFS must already be merged
with its base/update layers.

```powershell
$repo = (Get-Location).Path  # run from the repository root
$work = 'D:\Games\Inksplash3-work'
$exefs = 'D:\GameDumps\Splatoon3-v11.2\exefs'
$romfs = 'D:\GameDumps\Splatoon3-v11.2\romfs'
New-Item -ItemType Directory -Force -Path "$work\toolchain\python", "$work\deps" | Out-Null
python -m pip install --target "$work\toolchain\python" 'capstone==5.0.9' lz4
git clone --depth 1 --branch v1.5.2 https://github.com/xiph/opus.git "$work\deps\opus-src"
```

## Lift and build

Run the lift once for this work directory. It refuses to overwrite existing lift outputs; use a
new work directory for another dump/version. The build wrapper compiles Opus into that external
work tree and serializes builds that share it.

```powershell
python scripts\gen-windows.py --exefs $exefs --work $work `
  --python-packages "$work\toolchain\python"

# Headless/null backend first:
& .\scripts\build-windows.ps1 -WorkDir $work -Headless -Jobs 2 -GenJobs 1

# Optional native Vulkan window build (set VULKAN_SDK if it is not already set):
$env:VULKAN_SDK = 'C:\VulkanSDK\1.4.363.1'
& .\scripts\build-windows.ps1 -WorkDir $work -Jobs 2 -GenJobs 1
```

The null build validates startup without rendering. The Vulkan build uses the native Win32 Vulkan
surface shim; it does not link the SDL3 library. Existing unimplemented lifted instructions and HLE
calls can still trap at runtime.

## Run

The runner requires the merged Splatoon 3 marker `Pack/Params.pack.zs`, stores saves and logs only
under the external work directory, and bounds a run to 120 seconds by default.

```powershell
python scripts\run-windows.py --work $work --romfs $romfs --headless --seconds 120
python scripts\run-windows.py --work $work --romfs $romfs --seconds 120
```

Use `--seconds 0` for an interactive window session. The process exit code and log are reported;
the wrapper does not infer success from merely launching the executable.

## v12.0.0 update

The local v12.0.0 patch (title `0100C2500FC20800`, version `2818048`) was extracted and merged
with its matching base into a separate external work tree. The merged RomFS contains 22,865 files
(10,060,103,983 bytes). Both main and SDK were regenerated from that update, without reusing
v11.2.0 generated objects:

| Module | Functions | Instructions | Generated TUs | Unhandled instructions |
| --- | ---: | ---: | ---: | ---: |
| main | 272,586 | 43,413,637 | 617 | 817 |
| SDK | 32,690 | 1,616,719 | 27 | 3,728 |

The native Vulkan executable built successfully from 648 generated objects and 33 runtime objects;
its file header was checked as PE32+ AMD64. All 13 build/runner/shader/HID-calibration tests pass.
At the user's request, the v12.0.0 build was launched interactively and reached PlayerMake. A later
86.3-second run with a Pro Controller loaded factory IMU calibration and returned batches of 16 live
six-axis samples; the user reported gyro response was better. Residual drift has not been confirmed
eliminated. This does not establish complete v12.0.0
compatibility; the earlier v11.2.0 runtime results below do not establish v12.0.0 compatibility.

## v11.2.0 verification scope

The Windows build-graph suite passes all 13 tests. Native null and Vulkan executables both build
with Visual Studio Clang. For v11.2.0, the RomFS merge must open the base NCA with
`baseNca.OpenFileSystemWithPatch(updateNca, ...)`; the previous reversed call omitted files and
selected a fallback heap table that failed the game's 34 MiB minimum check. The corrected merged
RomFS contains 22,837 files (10,056,599,109 bytes) and the expected Switch/NVN heap table.

Using matching v11.2.0 ExeFS and this corrected RomFS, the null backend ran for 60 seconds, reached
PlayerMake and recorded 1,882 presents. The Vulkan windowed build also reached PlayerMake and
recorded over 2,100 presents during a 60-second run, settling near 60 FPS after initial shader
compilation. The Maxwell GLSL translator now supports the LEA.HI register form seen in the earlier
fragment-shader failures. A new 60-second run had no unknown-instruction shader failures; its steady
frame samples reported 0 skipped draws, though 430 draws were skipped during warmup before their
programs were ready. The run stayed near 60 FPS. The Vulkan backend now records real precise
samples-passed queries for `ResetCounter`/`ReportCounter` instead of fabricating visible samples;
the first 45-second validation completed 39,041 reports with both zero and nonzero counts, no
Vulkan validation errors, and about 0.1 us average for the guest-heap result check. PlayerMake
captures at frames 1800, 2100, and 2400 show readable text and no purple floor pool under the
avatar. A 16-frame sequence at frames 1861-1876 kept both the HUD and stationary world regions at
the same pixel position; the pre-fix reference sequence showed the reported scene movement. The
Windows HID layer handles the game's per-sensor gyroscope zero-drift mode getter/setter; a Pro
Controller smoke run exercised both calls and received live motion samples. The user confirmed that
the avatar was no longer stuck in ink, but reported darker shadows. Gyro aiming was still reported
nonfunctional; the subsequent correction retains 17 motion samples and returns actual sample
numbers, newest first, instead of one sample per poll. Shadow darkness and wall-paint appearance also
remain unresolved. The bounded validation runs were deliberately stopped by timeout;
neither logged the previous heap abort. Logs, lifted code, and game assets remain in the external
work directory.

This verifies startup into PlayerMake on Windows, not complete gameplay compatibility. The lift
still reports unhandled instructions and HLE imports, and the repository's documented Linux
measurements target v11.3.0 rather than this v11.2.0 test.
