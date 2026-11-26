<#
.SYNOPSIS
    Register (or remove) the Windows scheduled tasks for daily data collection.

.DESCRIPTION
    Creates two tasks for the current user, in the Task Scheduler folder \egx-quant\:

      Open EGX bulletin     Sun-Thu at 14:45: opens the EGX futures page in your
                            browser (and on Thursdays the Investing.com EGX30 and
                            USD/EGP pages). You save it with Ctrl+S.
      Collect market data   Every 30 minutes from 14:40 to 23:40, and at logon:
                            runs tools\collect.ps1 to import whatever you saved.

    Both run only while you are logged on, need no administrator rights and
    store no password. Remove them with -Remove.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\install-tasks.ps1
    powershell -ExecutionPolicy Bypass -File tools\install-tasks.ps1 -Remove
#>
param([switch]$Remove)

$ErrorActionPreference = 'Stop'
$folder = '\egx-quant\'
$openName = 'Open EGX bulletin'
$collectName = 'Collect market data'

if ($Remove) {
    foreach ($n in $openName, $collectName) {
        if (Get-ScheduledTask -TaskPath $folder -TaskName $n -ErrorAction SilentlyContinue) {
            Unregister-ScheduledTask -TaskPath $folder -TaskName $n -Confirm:$false
            Write-Output "Removed $folder$n"
        }
    }
    exit 0
}

$tools = $PSScriptRoot
$user = [Security.Principal.WindowsIdentity]::GetCurrent().Name
$principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -ExecutionTimeLimit (New-TimeSpan -Minutes 10) -MultipleInstances IgnoreNew

function New-PsAction([string]$Script) {
    New-ScheduledTaskAction -Execute 'powershell.exe' -WorkingDirectory (Split-Path -Parent $tools) -Argument (
        '-NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File "{0}"' -f (Join-Path $tools $Script))
}

# 1. Open the bulletin page on trading weekdays.
$openTrigger = New-ScheduledTaskTrigger -Weekly -DaysOfWeek Sunday, Monday, Tuesday, Wednesday, Thursday -At '14:45'
Register-ScheduledTask -TaskPath $folder -TaskName $openName -Force `
    -Description 'egx-quant: open the EGX futures page so it can be saved after the close (Ctrl+S).' `
    -Action (New-PsAction 'open-bulletin.ps1') -Trigger $openTrigger -Principal $principal -Settings $settings | Out-Null
Write-Output "Registered $folder$openName  (Sun-Thu 14:45)"

# 2. Collect every 30 minutes through the afternoon and evening, and at logon.
$daily = New-ScheduledTaskTrigger -Daily -At '14:40'
$daily.Repetition = (New-ScheduledTaskTrigger -Once -At '14:40' -RepetitionInterval (New-TimeSpan -Minutes 30) `
        -RepetitionDuration (New-TimeSpan -Hours 9)).Repetition
$logon = New-ScheduledTaskTrigger -AtLogOn -User $user
Register-ScheduledTask -TaskPath $folder -TaskName $collectName -Force `
    -Description 'egx-quant: import newly downloaded market data and refresh the database (tools\collect.ps1).' `
    -Action (New-PsAction 'collect.ps1') -Trigger @($daily, $logon) -Principal $principal -Settings $settings | Out-Null
Write-Output "Registered $folder$collectName  (every 30 min 14:40-23:40, and at logon)"
