param(
    [string]$Sdk = "$env:LOCALAPPDATA/Android/Sdk",
    [string]$Serial,
    [string]$RomPath
)
$ErrorActionPreference = 'Stop'
$repoPath = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $repoPath 'build/native-android'
$ndkPath = Join-Path $Sdk 'ndk/27.3.13750724'
$cmakeExe = Join-Path $Sdk 'cmake/3.31.6/bin/cmake.exe'
$ninjaExe = Join-Path $Sdk 'cmake/3.31.6/bin/ninja.exe'
$adbExe = Join-Path $Sdk 'platform-tools/adb.exe'
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
Invoke-Checked $cmakeExe @('-S', (Join-Path $repoPath 'native'), '-B', $buildPath, '-G', 'Ninja',
    "-DCMAKE_TOOLCHAIN_FILE=$ndkPath/build/cmake/android.toolchain.cmake", '-DANDROID_ABI=arm64-v8a',
    '-DANDROID_PLATFORM=android-33', '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_MAKE_PROGRAM=$ninjaExe")
Invoke-Checked $cmakeExe @('--build', $buildPath)
$deviceArgs = @()
if ($Serial) { $deviceArgs = @('-s', $Serial) }
Invoke-Checked $adbExe ($deviceArgs + @('push', "$buildPath/thor_tests", '/data/local/tmp/thor_tests'))
Invoke-Checked $adbExe ($deviceArgs + @('push', "$ndkPath/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so", '/data/local/tmp/libc++_shared.so'))
$testCommand = 'chmod 755 /data/local/tmp/thor_tests; LD_LIBRARY_PATH=/data/local/tmp /data/local/tmp/thor_tests'
if ($RomPath) {
    $privateRom = (Resolve-Path -LiteralPath $RomPath).Path
    Invoke-Checked $adbExe ($deviceArgs + @('push', $privateRom, '/data/local/tmp/thor-test-rom.sfc'))
    $testCommand += ' /data/local/tmp/thor-test-rom.sfc'
}
Invoke-Checked $adbExe ($deviceArgs + @('shell', $testCommand))
