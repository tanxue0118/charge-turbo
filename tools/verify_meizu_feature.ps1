param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
)

$ErrorActionPreference = 'Stop'
$failures = New-Object System.Collections.Generic.List[string]

function Test-Contains {
    param(
        [string]$Path,
        [string]$Pattern,
        [string]$Message
    )

    $full = Join-Path $Root $Path
    if (-not (Test-Path -LiteralPath $full)) {
        $failures.Add("Missing file: $Path")
        return
    }

    $text = Get-Content -Raw -Encoding UTF8 -LiteralPath $full
    if ($text -notmatch $Pattern) {
        $failures.Add($Message)
    }
}

function Test-FileMinLength {
    param(
        [string]$Path,
        [int64]$MinLength,
        [string]$Message
    )

    $full = Join-Path $Root $Path
    if (-not (Test-Path -LiteralPath $full)) {
        $failures.Add("Missing file: $Path")
        return
    }

    $item = Get-Item -LiteralPath $full
    if ($item.Length -lt $MinLength) {
        $failures.Add($Message)
    }
}

function Test-NotContains {
    param(
        [string]$Path,
        [string]$Pattern,
        [string]$Message
    )

    $full = Join-Path $Root $Path
    if (-not (Test-Path -LiteralPath $full)) {
        $failures.Add("Missing file: $Path")
        return
    }

    $text = Get-Content -Raw -Encoding UTF8 -LiteralPath $full
    if ($text -match $Pattern) {
        $failures.Add($Message)
    }
}

function Test-Missing {
    param(
        [string]$Path,
        [string]$Message
    )

    $full = Join-Path $Root $Path
    if (Test-Path -LiteralPath $full) {
        $failures.Add($Message)
    }
}

Test-Contains 'charge-boost\option.txt' '(?m)^MEIZU_DEVICE=0$' 'option.txt must include MEIZU_DEVICE=0'
Test-Contains 'charge-boost\option.txt' '(?m)^MEIZU_CHARGE_LEVEL=10$' 'option.txt must include MEIZU_CHARGE_LEVEL=10'
Test-Contains 'charge-boost\option.txt' '(?m)^MEIZU_THERMAL_SCHEME=2$' 'option.txt must include MEIZU_THERMAL_SCHEME=2'

Test-Contains '源码\单文件版\turbo-charge.c' '"MEIZU_DEVICE"' 'turbo-charge.c must register MEIZU_DEVICE option'
Test-Contains '源码\单文件版\turbo-charge.c' '"MEIZU_CHARGE_LEVEL"' 'turbo-charge.c must register MEIZU_CHARGE_LEVEL option'
Test-Contains '源码\单文件版\turbo-charge.c' '"MEIZU_THERMAL_SCHEME"' 'turbo-charge.c must register MEIZU_THERMAL_SCHEME option'
Test-Contains '源码\单文件版\turbo-charge.c' 'MEIZU_THERMAL_FLYME_CLEAR_DIR' 'turbo-charge.c must define the Flyme clear thermal scheme directory'
Test-Contains '源码\单文件版\turbo-charge.c' 'MEIZU_THERMAL_EXTREMEGT_DIR' 'turbo-charge.c must define the extremegt thermal scheme directory'
Test-Contains '源码\单文件版\turbo-charge.c' 'select_thermal_files_dir' 'turbo-charge.c must select thermal file directory by Meizu scheme'
Test-Contains '源码\单文件版\turbo-charge.c' 'sync_meizu_wired_level' 'turbo-charge.c must handle Meizu wired_level updates'
Test-Contains '源码\单文件版\turbo-charge.c' 'handle_meizu_generation_change' 'turbo-charge.c must hot-remount thermal files after MEIZU_DEVICE changes'
Test-Contains '源码\单文件版\turbo-charge.c' 'clamp_meizu_charge_level' 'turbo-charge.c must clamp Meizu charge level to 1-10'
Test-Contains '源码\单文件版\turbo-charge.c' 'write_meizu_wired_level_with_echo' 'turbo-charge.c must write Meizu wired_level with the echo-style helper'
Test-Contains '源码\单文件版\turbo-charge.c' 'MEIZU_WIRED_LEVEL_LEGACY_PATH' 'turbo-charge.c must support the Flyme10 wired_level path'
Test-Contains '源码\单文件版\turbo-charge.c' 'echo %d > %s' 'Meizu wired_level write must use echo redirection instead of set_value'
Test-Contains '源码\单文件版\turbo-charge.c' 'chmod 777 %s' 'Meizu wired_level write must restore write permissions before echo'
Test-Contains '源码\单文件版\turbo-charge.c' 'chmod -w %s' 'Meizu wired_level write must remove write permission after echo'
Test-NotContains '源码\单文件版\turbo-charge.c' 'set_value\s*\(\s*MEIZU_WIRED_LEVEL_PATH' 'Meizu wired_level must not be written through set_value()'
Test-NotContains '源码\单文件版\turbo-charge.c' 'MEIZU_THERMAL_ENGINE_VENDOR_PATH' 'Meizu thermal scheme must not use the old real thermal-engine-v2 vendor path'
Test-NotContains '源码\单文件版\turbo-charge.c' 'MEIZU_THERMAL_ENGINE_PATH' 'Meizu thermal scheme must not use the old real thermal-engine-v2 system path'
Test-Missing 'charge-boost\meizu_files\vendor\bin\thermal-engine-v2' 'old single-file Meizu vendor thermal-engine-v2 must not remain in module resources'
Test-Missing 'charge-boost\meizu_files\system\vendor\bin\thermal-engine-v2' 'old single-file Meizu system thermal-engine-v2 must not remain in module resources'

Test-Contains 'charge-boost\webroot\index.html' 'sw-MEIZU_DEVICE' 'WebUI must include MEIZU_DEVICE switch'
Test-Contains 'charge-boost\webroot\index.html' 'sel-MEIZU_CHARGE_LEVEL' 'WebUI must include MEIZU_CHARGE_LEVEL selector'
Test-Contains 'charge-boost\webroot\index.html' 'sel-MEIZU_THERMAL_SCHEME' 'WebUI must include MEIZU_THERMAL_SCHEME selector'
Test-Contains 'charge-boost\webroot\index.html' 'sw-dual-cell' 'WebUI must include dual-cell display switch'

Test-Contains 'charge-boost\webroot\app.js' 'MEIZU_DEVICE' 'app.js must load/save MEIZU_DEVICE'
Test-Contains 'charge-boost\webroot\app.js' 'MEIZU_CHARGE_LEVEL' 'app.js must load/save MEIZU_CHARGE_LEVEL'
Test-Contains 'charge-boost\webroot\app.js' 'MEIZU_THERMAL_SCHEME' 'app.js must load/save MEIZU_THERMAL_SCHEME'
Test-Contains 'charge-boost\webroot\app.js' 'dualCell' 'app.js must keep dual-cell display in local UI settings'
Test-Contains 'charge-boost\webroot\app.js' 'displayMultiplier' 'app.js must apply display multiplier to shown current/power values'

Test-Contains '源码\单文件版\turbo-charge.c' 'thermal_flyme_clear' 'turbo-charge.c must reference the Flyme clear resource directory'
Test-Contains '源码\单文件版\turbo-charge.c' 'thermal_extremegt' 'turbo-charge.c must reference the extremegt resource directory'
Test-FileMinLength 'charge-boost\meizu_files\thermal_extremegt\system\vendor\etc\thermal.high.conf' 300 'extremegt thermal config must be present'
Test-FileMinLength 'charge-boost\meizu_files\thermal_extremegt\system\vendor\etc\thermal-engine.conf' 300 'extremegt thermal-engine config must be present'
Test-Contains 'charge-boost\meizu_files\thermal_extremegt\system\vendor\etc\thermal.high.conf' 'actions mz_chg' 'extremegt thermal configs must include mz_chg action'
Test-Contains 'charge-boost\meizu_files\thermal_extremegt\system\vendor\etc\thermal.high.conf' 'action_info\s+8000 7000 6000 5000 4000 3000 2000' 'extremegt thermal configs must include relaxed charge action_info'
Test-FileMinLength 'charge-boost\meizu_files\thermal_extremegt\system\vendor\etc\display\thermallevel_to_fps.xml' 100 'extremegt thermallevel_to_fps.xml must be present'
Test-Contains 'charge-boost\meizu_files\thermal_flyme_clear\system\vendor\etc\thermal.high.conf' '^$' 'Flyme clear thermal.high.conf must be an empty file'
Test-Missing 'charge-boost\meizu_files\thermal_extremegt\system\vendor\etc\meizu_charging.ini' 'extremegt meizu_charging.ini must not be included in thermal scheme resources'
Test-Missing 'charge-boost\meizu_files\thermal_extremegt\system\vendor\bin\init.kernel.post_boot-kalama.sh' 'extremegt post_boot script must not be included in thermal scheme resources'

if ($failures.Count -gt 0) {
    Write-Host "Meizu feature verification failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Meizu feature verification passed." -ForegroundColor Green
