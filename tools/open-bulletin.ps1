<#
.SYNOPSIS
    Open the EGX futures page in your browser so you can save it (Ctrl+S).

.DESCRIPTION
    Runs on EGX trading weekdays (Sunday to Thursday) unless -Force is given.
    On Thursdays it also opens the Investing.com EGX30 and USD/EGP history pages,
    so spot and FX can be refreshed once a week.

    The pages open in your normal browser, which passes the sites' bot checks;
    nothing is downloaded automatically. After you save, tools\collect.ps1 (or
    its scheduled task) imports the files.
#>
param([switch]$Force)

$today = Get-Date
$isEgxDay = $today.DayOfWeek -ne [DayOfWeek]::Friday -and $today.DayOfWeek -ne [DayOfWeek]::Saturday
if (-not $isEgxDay -and -not $Force) { exit 0 }

Start-Process 'https://www.egx.com.eg/en/derivatives.aspx'

if ($today.DayOfWeek -eq [DayOfWeek]::Thursday -or $Force) {
    Start-Process 'https://www.investing.com/indices/egx30-historical-data'
    Start-Process 'https://www.investing.com/currencies/usd-egp-historical-data'
}
