param(
    [Parameter(Position = 0, Mandatory = $true)]
    [ValidateSet("sdio_init", "scan_once", "scan_ui_check")]
    [string]$Step,

    [string]$Port = "COM4",
    [int]$Baud = 115200,
    [switch]$NoFlash
)

$ErrorActionPreference = "Stop"

function Resolve-PioExe {
    $candidates = @(
        "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe",
        "pio.exe"
    )
    foreach ($c in $candidates) {
        if ($c -eq "pio.exe") {
            $cmd = Get-Command pio.exe -ErrorAction SilentlyContinue
            if ($cmd) { return $cmd.Source }
        } elseif (Test-Path $c) {
            return $c
        }
    }
    throw "pio.exe not found. Install PlatformIO Core or add pio.exe to PATH."
}

function Get-StepEnv([string]$stepName) {
    switch ($stepName) {
        "sdio_init"     { return "firmware_m5tab5_c6_sdio" }
        "scan_once"     { return "firmware_m5tab5_c6_scan" }
        "scan_ui_check" { return "firmware_m5tab5_c6_scan" }
        default         { throw "Unsupported step: $stepName" }
    }
}

function Write-Token([string]$msg) {
    Write-Host $msg -ForegroundColor Cyan
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$logsRoot = Join-Path $repoRoot "logs\tab5_c6_tool"
$runStamp = Get-Date -Format "yyyyMMdd_HHmmss"
$runDir = Join-Path $logsRoot ("run_{0}_{1}" -f $runStamp, $Step)
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
$serialLogPath = Join-Path $runDir "serial.log"
$summaryPath = Join-Path $runDir "summary.json"

$stepEnv = Get-StepEnv $Step
$pioExe = Resolve-PioExe

$result = [ordered]@{
    step = $Step
    env = $stepEnv
    port = $Port
    baud = $Baud
    started_at = (Get-Date).ToString("s")
    attempts = @()
    success = $false
}

$maxAttempts = 2  # auto-retry once
$crashPatterns = @(
    "H_SDIO_DRV: sdio card init failed",
    "Task `"sdio_read`" should not return",
    "Guru Meditation Error",
    "abort()",
    "Backtrace:",
    "PANIC"
)

for ($attempt = 1; $attempt -le $maxAttempts; $attempt++) {
    $attemptInfo = [ordered]@{
        attempt = $attempt
        flashed = $false
        command_sent = $false
        token_start = $false
        token_ok = $false
        token_fail = $false
        crash_seen = $false
        fail_reason = ""
        ap_count = $null
        lines = 0
    }

    Write-Token ("[c6tool] step={0} attempt={1}/{2}" -f $Step, $attempt, $maxAttempts)
    Add-Content -Path $serialLogPath -Value ("=== attempt {0} start {1} ===" -f $attempt, (Get-Date -Format s))

    if (-not $NoFlash) {
        Write-Token ("[c6tool] flashing env={0} port={1}" -f $stepEnv, $Port)
        & $pioExe run -e $stepEnv -t upload --upload-port $Port
        if ($LASTEXITCODE -ne 0) {
            $attemptInfo.fail_reason = "flash_failed"
            $result.attempts += $attemptInfo
            break
        }
        $attemptInfo.flashed = $true
    } else {
        Write-Token "[c6tool] --NoFlash set, skipping upload"
    }

    $serial = New-Object System.IO.Ports.SerialPort $Port, $Baud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One
    )
    $serial.ReadTimeout = 250
    $serial.NewLine = "`n"
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false

    try {
        Start-Sleep -Milliseconds 1200
        $serial.Open()

        Start-Sleep -Milliseconds 600
        $req = @{ id = 9001; cmd = "c6test"; step = $Step } | ConvertTo-Json -Compress
        $serial.WriteLine($req)
        $attemptInfo.command_sent = $true
        Write-Token ("[c6tool] sent: {0}" -f $req)

        $deadline = (Get-Date).AddSeconds(35)
        while ((Get-Date) -lt $deadline) {
            try {
                $line = $serial.ReadLine()
            } catch [System.TimeoutException] {
                continue
            }

            if ([string]::IsNullOrWhiteSpace($line)) { continue }
            $line = $line.TrimEnd("`r", "`n")
            $attemptInfo.lines++
            Add-Content -Path $serialLogPath -Value $line

            if ($line -like ("*[c6test] step={0} start*" -f $Step)) {
                $attemptInfo.token_start = $true
            }
            if ($line -like ("*[c6test] step={0} ok*" -f $Step)) {
                $attemptInfo.token_ok = $true
                break
            }
            if ($line -like ("*[c6test] step={0} fail*" -f $Step)) {
                $attemptInfo.token_fail = $true
                $attemptInfo.fail_reason = "step_fail_token"
                break
            }

            if ($line -match "\[c6test\]\s+ui\.ap_count=(\d+)") {
                $attemptInfo.ap_count = [int]$matches[1]
            }

            foreach ($pattern in $crashPatterns) {
                if ($line -like ("*{0}*" -f $pattern)) {
                    $attemptInfo.crash_seen = $true
                    $attemptInfo.fail_reason = "crash_signature"
                    break
                }
            }
            if ($attemptInfo.crash_seen) { break }
        }
    }
    finally {
        if ($serial -and $serial.IsOpen) { $serial.Close() }
    }

    if ($Step -eq "scan_ui_check") {
        if ($attemptInfo.ap_count -gt 0 -and -not $attemptInfo.crash_seen -and -not $attemptInfo.token_fail) {
            $attemptInfo.token_ok = $true
        } elseif (-not $attemptInfo.fail_reason) {
            $attemptInfo.fail_reason = "ap_count_zero"
        }
    }

    if (-not $attemptInfo.token_ok -and -not $attemptInfo.fail_reason) {
        $attemptInfo.fail_reason = "timeout"
    }

    $result.attempts += $attemptInfo
    if ($attemptInfo.token_ok -and -not $attemptInfo.crash_seen) {
        $result.success = $true
        break
    }
}

$result.finished_at = (Get-Date).ToString("s")
($result | ConvertTo-Json -Depth 8) | Set-Content -Path $summaryPath -Encoding UTF8

if ($result.success) {
    Write-Host ("[c6tool] PASS step={0}" -f $Step) -ForegroundColor Green
} else {
    Write-Host ("[c6tool] FAIL step={0}" -f $Step) -ForegroundColor Red
}
Write-Host ("[c6tool] serial log: {0}" -f $serialLogPath)
Write-Host ("[c6tool] summary  : {0}" -f $summaryPath)

if (-not $result.success) { exit 1 }
exit 0
