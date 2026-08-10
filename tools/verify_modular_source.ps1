param(
    [switch]$Compile
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$srcDir = Join-Path $repoRoot "源码\模块化"

$requiredFiles = @(
    "core/main.c",
    "core/main.h",
    "core/global.c",
    "core/global.h",
    "utils/printf_with_time.c",
    "utils/printf_with_time.h",
    "utils/file_utils.c",
    "utils/file_utils.h",
    "control/value_set.c",
    "control/value_set.h",
    "config/read_option_file.c",
    "config/read_option_file.h",
    "control/foreground_app.c",
    "control/foreground_app.h",
    "control/some_ctrl.c",
    "control/some_ctrl.h",
    "thermal/temp_simulation.c",
    "thermal/temp_simulation.h",
    "thermal/thermal_mount.c",
    "thermal/thermal_mount.h"
)

foreach ($file in $requiredFiles) {
    $path = Join-Path $srcDir $file
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing modular source file: 源码/模块化/$file"
    }
}

if (-not $Compile) {
    Write-Host "Modular source structure is present."
    exit 0
}

$clang = Join-Path $env:LOCALAPPDATA "Android\Sdk\ndk\29.0.14206865\toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android23-clang.cmd"
if (-not (Test-Path -LiteralPath $clang -PathType Leaf)) {
    throw "NDK clang not found: $clang"
}

$outDir = Join-Path $repoRoot "build"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$outFile = Join-Path $outDir "turbo-charge"

$sources = Get-ChildItem -LiteralPath $srcDir -Recurse -Filter "*.c" |
    Sort-Object FullName |
    ForEach-Object { $_.FullName }

$includeArgs = @("-I$srcDir") + @(
    Get-ChildItem -LiteralPath $srcDir -Recurse -Directory |
        Sort-Object FullName |
        ForEach-Object { "-I$($_.FullName)" }
)

& $clang -O2 -s -Wall -Wextra -Wno-unused-parameter @includeArgs -o $outFile @sources
if ($LASTEXITCODE -ne 0) {
    throw "Modular source compile failed with exit code $LASTEXITCODE"
}

Write-Host "Modular source compiled: $outFile"
