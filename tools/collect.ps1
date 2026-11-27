<#
.SYNOPSIS
    Pick up newly downloaded market data, file it, import it and refresh the database.

.DESCRIPTION
    Looks in the inbox folder (Downloads by default) for files saved in the last
    -MaxAgeDays days and recognises:

      bulletin  any .htm/.html containing the EGX Derivatives Market Watch table
      index     "EGX 30 Historical Data*.csv"        (Investing.com)
      fx        "USD_EGP Historical Data*.csv"       (Investing.com)
      tbills    "*T-Bill*.xlsx"                      (T-bill auction workbook)

    Each new file is copied into data/raw/, imported into data/clean/ with egx_db,
    and recorded by SHA-256 in data/raw/ingested.tsv so it is never processed twice
    (originals in the inbox are left alone). If anything was imported, the database
    is reloaded, egx_basis writes data/clean/basis_history.csv and egx_dashboard
    refreshes data/clean/dashboard.html.

    Bulletin trading date: taken from a YYYY-MM-DD in the file name if present,
    otherwise from when the file was saved (Cairo time on this PC):
      Sun-Thu after 14:30          -> that day
      before 10:00, or Fri/Sat     -> the previous Sun-Thu
      Sun-Thu 10:00-14:30          -> skipped: saved mid-session, not a settlement
    A bulletin identical to the previous trading day's (a holiday) is rejected.

    The log is data/logs/collect-YYYY-MM.log.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\collect.ps1
#>
param(
    [string]$Inbox = (Join-Path $env:USERPROFILE 'Downloads'),
    [int]$MaxAgeDays = 14,
    [string]$DataDir = ''  # default: <repo>\data
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$Repo     = Split-Path -Parent $PSScriptRoot
if (-not $DataDir) { $DataDir = Join-Path $Repo 'data' }
$Bin      = Join-Path $Repo 'build\apps\Release'
$EgxDb    = Join-Path $Bin 'egx_db.exe'
$EgxBasis = Join-Path $Bin 'egx_basis.exe'
$EgxDash  = Join-Path $Bin 'egx_dashboard.exe'
$Raw      = Join-Path $DataDir 'raw'
$Clean    = Join-Path $DataDir 'clean'
$Db       = Join-Path $DataDir 'egx.db'
$LogDir   = Join-Path $DataDir 'logs'
$Ledger   = Join-Path $Raw 'ingested.tsv'

New-Item -ItemType Directory -Force $Raw, $Clean, $LogDir | Out-Null
$LogFile = Join-Path $LogDir ('collect-{0}.log' -f (Get-Date -Format 'yyyy-MM'))

function Write-Log([string]$Message) {
    $line = '{0}  {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $Message
    Write-Output $line
    Add-Content -LiteralPath $LogFile -Value $line -Encoding UTF8
}

# Run a native tool; return its exit code and output without letting stderr
# become a terminating error (Windows PowerShell 5.1 behaviour).
function Invoke-Tool([string]$Exe, [string[]]$ToolArgs) {
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = & $Exe @ToolArgs 2>&1 | ForEach-Object { "$_" }
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $saved
    }
    [pscustomobject]@{ Code = $code; Output = (($out | Where-Object { $_ -ne '' }) -join "`n") }
}

function Test-EgxWeekday([datetime]$Day) {
    $Day.DayOfWeek -ne [DayOfWeek]::Friday -and $Day.DayOfWeek -ne [DayOfWeek]::Saturday
}

function Get-PreviousEgxWeekday([datetime]$Day) {
    $d = $Day.Date.AddDays(-1)
    while (-not (Test-EgxWeekday $d)) { $d = $d.AddDays(-1) }
    $d
}

# Trading date of a saved bulletin, or $null when it was saved mid-session.
function Get-BulletinDate([System.IO.FileInfo]$File) {
    if ($File.Name -match '(\d{4}-\d{2}-\d{2})') {
        return [datetime]::ParseExact($Matches[1], 'yyyy-MM-dd', [Globalization.CultureInfo]::InvariantCulture)
    }
    $t = $File.LastWriteTime
    if (-not (Test-EgxWeekday $t)) { return Get-PreviousEgxWeekday $t }
    if ($t.TimeOfDay -ge (New-TimeSpan -Hours 14 -Minutes 30)) { return $t.Date }
    if ($t.TimeOfDay -lt (New-TimeSpan -Hours 10)) { return Get-PreviousEgxWeekday $t }
    return $null
}

function Get-Kind([System.IO.FileInfo]$File) {
    $name = $File.Name
    switch -Regex ($File.Extension.ToLowerInvariant()) {
        '^\.html?$' {
            if (Select-String -LiteralPath $File.FullName -Pattern 'MarketDerivatives1_GridView1' -SimpleMatch -Quiet) {
                return 'bulletin'
            }
        }
        '^\.csv$' {
            if ($name -like 'EGX 30 Historical Data*') { return 'index' }
            if ($name -like 'USD_EGP Historical Data*' -or $name -like 'USD EGP Historical Data*') { return 'fx' }
        }
        '^\.xlsx$' {
            if ($name -like '*T-Bill*') { return 'tbills' }
        }
    }
    return $null
}

# Copy into data/raw/<sub>/<name>, never overwriting a different file.
function Copy-ToRaw([System.IO.FileInfo]$File, [string]$Sub, [string]$Name) {
    $dir = Join-Path $Raw $Sub
    New-Item -ItemType Directory -Force $dir | Out-Null
    $base = [IO.Path]::GetFileNameWithoutExtension($Name)
    $ext = [IO.Path]::GetExtension($Name)
    $dest = Join-Path $dir $Name
    $n = 2
    while (Test-Path -LiteralPath $dest) {
        if ((Get-FileHash -LiteralPath $dest).Hash -eq (Get-FileHash -LiteralPath $File.FullName).Hash) { return $dest }
        $dest = Join-Path $dir ('{0}_{1}{2}' -f $base, $n, $ext)
        $n++
    }
    Copy-Item -LiteralPath $File.FullName -Destination $dest
    $dest
}

# ---- single instance ---------------------------------------------------------
$mutex = New-Object System.Threading.Mutex($false, 'egx-quant-collect')
if (-not $mutex.WaitOne(0)) { Write-Output 'Another collect run is in progress; exiting.'; exit 0 }

try {
    foreach ($exe in $EgxDb, $EgxBasis) {
        if (-not (Test-Path -LiteralPath $exe)) {
            Write-Log "ERROR: $exe not found. Build first: cmake --build build --config Release"
            exit 1
        }
    }
    if (-not (Test-Path -LiteralPath $Inbox)) { Write-Log "ERROR: inbox $Inbox not found"; exit 1 }

    # Ledger: hash -> result, so each file is handled once.
    $seen = @{}
    if (Test-Path -LiteralPath $Ledger) {
        foreach ($line in Get-Content -LiteralPath $Ledger -Encoding UTF8) {
            $h = ($line -split "`t")[0]
            if ($h -and $h -ne 'sha256') { $seen[$h] = $true }
        }
    } else {
        Set-Content -LiteralPath $Ledger -Value "sha256`tprocessed`tkind`tsource`tresult" -Encoding UTF8
    }
    function Add-Ledger([string]$Hash, [string]$Kind, [string]$Source, [string]$Result) {
        $clean = ($Result -replace "[`t`r`n]+", ' ').Trim()
        Add-Content -LiteralPath $Ledger -Encoding UTF8 -Value (
            "{0}`t{1}`t{2}`t{3}`t{4}" -f $Hash, (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $Kind, $Source, $clean)
        $seen[$Hash] = $true
    }

    # ---- find new files ----
    $cutoff = (Get-Date).AddDays(-$MaxAgeDays)
    $settled = (Get-Date).AddSeconds(-60)  # skip files still being written
    $candidates = @(Get-ChildItem -LiteralPath $Inbox -File |
        Where-Object { $_.LastWriteTime -ge $cutoff -and $_.LastWriteTime -le $settled -and
                       $_.Extension -notmatch '^\.(crdownload|part|tmp)$' })

    $work = @()
    foreach ($f in $candidates) {
        $kind = Get-Kind $f
        if (-not $kind) { continue }
        $hash = (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash
        if ($seen.ContainsKey($hash) -or ($work | Where-Object { $_.Hash -eq $hash })) { continue }
        $date = $null
        if ($kind -eq 'bulletin') { $date = Get-BulletinDate $f }
        $order = @{ tbills = 0; index = 1; fx = 2; bulletin = 3 }[$kind]
        $sortDate = if ($date) { $date } else { $f.LastWriteTime }
        $work += [pscustomobject]@{ File = $f; Kind = $kind; Hash = $hash; Date = $date; Order = $order; Sort = $sortDate }
    }

    if ($work.Count -eq 0) { exit 0 }  # nothing new: stay quiet

    $imported = 0
    foreach ($w in ($work | Sort-Object Order, Sort)) {
        $f = $w.File
        $r = $null
        # (Checked outside the switch: 'continue' inside a PowerShell switch only leaves the switch.)
        if ($w.Kind -eq 'bulletin' -and -not $w.Date) {
            Write-Log "SKIP  $($f.Name): saved $($f.LastWriteTime.ToString('ddd HH:mm')), during the trading session. Save the page after 14:30."
            Add-Ledger $w.Hash 'bulletin' $f.Name 'skipped: saved during trading session'
            continue
        }
        switch ($w.Kind) {
            'bulletin' {
                $day = $w.Date.ToString('yyyy-MM-dd')
                $dest = Copy-ToRaw $f 'egx\bulletins' ("egx-futures-$day.html")
                $r = Invoke-Tool $EgxDb @('import-futures', $dest, $day, $Clean)
            }
            'index' {
                $dest = Copy-ToRaw $f 'investing' ('{0}_{1}' -f $f.LastWriteTime.ToString('yyyyMMdd-HHmm'), $f.Name)
                $r = Invoke-Tool $EgxDb @('import-investing', 'index', $dest, $Clean)
            }
            'fx' {
                $dest = Copy-ToRaw $f 'investing' ('{0}_{1}' -f $f.LastWriteTime.ToString('yyyyMMdd-HHmm'), $f.Name)
                $r = Invoke-Tool $EgxDb @('import-investing', 'fx', $dest, $Clean)
            }
            'tbills' {
                $dest = Copy-ToRaw $f 'cbe' ('{0}_{1}' -f $f.LastWriteTime.ToString('yyyyMMdd-HHmm'), $f.Name)
                $r = Invoke-Tool $EgxDb @('import-tbills', $dest, $Clean)
            }
        }
        if ($r.Code -eq 0) {
            $imported++
            Write-Log "OK    $($w.Kind) $($f.Name)"
            foreach ($l in ($r.Output -split "`n")) { Write-Log "        $l" }
            Add-Ledger $w.Hash $w.Kind $f.Name 'ok'
        } else {
            Write-Log "FAIL  $($w.Kind) $($f.Name)"
            foreach ($l in ($r.Output -split "`n")) { Write-Log "        $l" }
            Add-Ledger $w.Hash $w.Kind $f.Name ('failed: ' + $r.Output)
        }
    }

    if ($imported -gt 0) {
        $r = Invoke-Tool $EgxDb @('load', $Db, $Clean)
        if ($r.Code -ne 0) { Write-Log "FAIL  load: $($r.Output)"; exit 1 }
        Write-Log 'OK    database reloaded'
        $r = Invoke-Tool $EgxBasis @('--db', $Db, '--out', (Join-Path $Clean 'basis_history.csv'))
        if ($r.Code -ne 0) { Write-Log "FAIL  egx_basis: $($r.Output)"; exit 1 }
        foreach ($l in (($r.Output -split "`n") | Select-Object -Last 4)) { Write-Log "        $l" }
        if (Test-Path -LiteralPath $EgxDash) {
            $r = Invoke-Tool $EgxDash @('--db', $Db, '--out', (Join-Path $Clean 'dashboard.html'))
            if ($r.Code -ne 0) { Write-Log "FAIL  egx_dashboard: $($r.Output)" } else { Write-Log "OK    $($r.Output.Trim())" }
        }

        # Warn when spot lags the futures (the basis is then computed against a stale close).
        $lastDate = {
            param($path)
            if (-not (Test-Path -LiteralPath $path)) { return $null }
            $last = Get-Content -LiteralPath $path -Tail 1
            if ($last -match '^(\d{4}-\d{2}-\d{2})') { return [datetime]$Matches[1] }
            return $null
        }
        $spotDate = & $lastDate (Join-Path $Clean 'egx30.csv')
        $futDate = & $lastDate (Join-Path $Clean 'futures.csv')
        if ($spotDate -and $futDate -and $spotDate -lt $futDate) {
            Write-Log ("NOTE  EGX30 closes end {0:yyyy-MM-dd} but futures run to {1:yyyy-MM-dd}: download EGX 30 Historical Data from Investing.com to refresh spot." -f $spotDate, $futDate)
        }
    }
} finally {
    $mutex.ReleaseMutex()
    $mutex.Dispose()
}
