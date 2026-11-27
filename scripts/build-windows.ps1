param(
    [Parameter(Mandatory = $true)][string]$WorkDir,
    [switch]$Headless,
    [switch]$RuntimeOnly,
    [ValidateRange(1, 64)][int]$Jobs = 2,
    [ValidateRange(1, 64)][int]$GenJobs = 1,
    [string]$VulkanSDK = $env:VULKAN_SDK
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$root = Join-Path $repo 'baseline\totk-recomp'
$work = [IO.Path]::GetFullPath($WorkDir)
if ($work -eq $repo -or $work.StartsWith($repo + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'WorkDir must be outside the repository.'
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio C++ Build Tools with Clang and the Windows SDK.' }
$vs = & $vswhere -latest -products '*' -property installationPath
if (-not $vs) { throw 'Visual Studio C++ Build Tools were not found.' }
$devcmd = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'
# Populate only this build process's environment, not the user's global PATH.
$environment = & $env:ComSpec /d /c "call `"$devcmd`" -arch=x64 -host_arch=x64 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the x64 Visual Studio toolchain.' }
foreach ($line in $environment) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
}
$clang = Join-Path $vs 'VC\Tools\Llvm\x64\bin\clang.exe'
$clangxx = Join-Path $vs 'VC\Tools\Llvm\x64\bin\clang++.exe'
if (-not (Test-Path -LiteralPath $clangxx)) { throw "Missing Visual Studio Clang: $clangxx" }
foreach ($tool in @('python', 'cmake', 'ninja')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Missing $tool on PATH." }
}
[IO.Directory]::CreateDirectory($work) | Out-Null
try {
    $buildLock = [IO.File]::Open((Join-Path $work '.build.lock'), [IO.FileMode]::OpenOrCreate,
        [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
} catch [IO.IOException] {
    throw "Cannot lock $work. Finish any other build using this work directory first."
}
try {
    $deps = Join-Path $work 'deps'
    $opusSource = Join-Path $deps 'opus-src'
    $opusBuild = Join-Path $deps 'opus-build'
    $opusInstall = Join-Path $deps 'opus-install'
    $opusLib = Join-Path $opusInstall 'lib\opus.lib'
    if (-not (Test-Path -LiteralPath $opusLib)) {
        if (-not (Test-Path -LiteralPath (Join-Path $opusSource 'CMakeLists.txt'))) {
            throw "Missing Opus source. Clone xiph/opus tag v1.5.2 into $opusSource (see docs/windows.md)."
        }
        & cmake -S $opusSource -B $opusBuild -G Ninja "-DCMAKE_C_COMPILER=$clang" `
            -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$opusInstall" `
            -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL `
            -DOPUS_BUILD_SHARED_LIBRARY=OFF -DOPUS_BUILD_TESTING=OFF -DOPUS_BUILD_PROGRAMS=OFF
        if ($LASTEXITCODE -ne 0) { throw 'Opus configuration failed.' }
        & cmake --build $opusBuild --parallel $Jobs
        if ($LASTEXITCODE -ne 0) { throw 'Opus build failed.' }
        & cmake --install $opusBuild
        if ($LASTEXITCODE -ne 0) { throw 'Opus installation failed.' }
    }
    $backend = if ($Headless) { 'build-null' } else { 'build-vk' }
    $out = Join-Path $work $backend
    $arguments = @((Join-Path $root 'tools\recomp\mkninja_windows.py'),
        '--root', $root, '--gen', (Join-Path $work 'gen\main'), '--out', $out,
        '--obj', (Join-Path $work 'obj'), '--opus-lib', $opusLib,
        '--cc', $clang, '--cxx', $clangxx, '--gen-pool', "$GenJobs")
    if ($Headless) { $arguments += '--null' }
    if ($RuntimeOnly) { $arguments += '--runtime-only' }
    if ($VulkanSDK) { $arguments += @('--vulkan-sdk', $VulkanSDK) }
    & python @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Windows Ninja generation failed.' }
    & ninja -C $out -j $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'Windows build failed.' }
    if ($RuntimeOnly) { Write-Output "Native runtime compiled (no guest executable): $out" }
    else { Write-Output "Built: $(Join-Path $out 'inksplash3.exe')" }
} finally {
    $buildLock.Dispose()
}
