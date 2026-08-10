param(
    [string]$Output = "build\turbo-charge",
    [string]$NdkVersion = "29.0.14206865"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$srcDir = Join-Path $repoRoot "源码\模块化"
$clang = Join-Path $env:LOCALAPPDATA "Android\Sdk\ndk\$NdkVersion\toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android23-clang.cmd"

if (-not (Test-Path -LiteralPath $clang -PathType Leaf)) {
    throw "NDK clang not found: $clang"
}

if (-not (Test-Path -LiteralPath $srcDir -PathType Container)) {
    throw "Source directory not found: $srcDir"
}

$outFile = if ([System.IO.Path]::IsPathRooted($Output)) {
    $Output
} else {
    Join-Path $repoRoot $Output
}

$outDir = Split-Path -Parent $outFile
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

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
    throw "Build failed with exit code $LASTEXITCODE"
}

Write-Host "Built: $outFile"
