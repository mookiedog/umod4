# setup_nightly_tests.ps1
#
# Registers, reschedules, enables, disables, or reports on the per-user
# Windows Scheduled Task that runs the umod4 nightly test harness inside
# WSL.
#
# Does NOT require Administrator -- per-user scheduled tasks run fine while
# the screen is locked, as long as the account stays logged in.
#
# Usage:
#   From WSL (easiest): tools/setup_nightly_tests [args]
#
#   From an ordinary PowerShell on Windows:
#     & "tools\setup_nightly_tests.ps1" [-Time "02:00"] [-Distro "Ubuntu"]
#     & "tools\setup_nightly_tests.ps1" -Status
#     & "tools\setup_nightly_tests.ps1" -Disable
#     & "tools\setup_nightly_tests.ps1" -Enable
#
# To change the schedule later, just re-run with new -Time/-Distro values
# -- it updates the existing task in place. -Disable pauses the task
# without unregistering it; -Enable resumes it.

param(
    [string]$Time   = "02:00",
    [string]$Distro = "Ubuntu",
    [switch]$Status,
    [switch]$Disable,
    [switch]$Enable
)

$TaskName = "umod4-nightly-tests"

if ($Status) {
    $task = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    if (-not $task) {
        Write-Host "Task '$TaskName' is not registered. Run 'tools/setup_nightly_tests' to register it."
        exit 0
    }
    $info = Get-ScheduledTaskInfo -TaskName $TaskName
    Write-Host "Task:      $TaskName"
    Write-Host "State:     $($task.State)"
    Write-Host "Last run:  $($info.LastRunTime)  (result: $($info.LastTaskResult))"
    Write-Host "Next run:  $($info.NextRunTime)"
    exit 0
}

if ($Disable) {
    Disable-ScheduledTask -TaskName $TaskName | Out-Null
    Write-Host "Disabled '$TaskName' (still registered -- re-enable with: tools/setup_nightly_tests --enable)"
    exit 0
}

if ($Enable) {
    Enable-ScheduledTask -TaskName $TaskName | Out-Null
    Write-Host "Enabled '$TaskName'."
    exit 0
}

$Command  = "cd ~/projects/umod4 && tests/nightly_run.sh"

$Action   = New-ScheduledTaskAction -Execute "wsl.exe" `
              -Argument "-d $Distro -- bash -lic `"$Command`""
$Trigger  = New-ScheduledTaskTrigger -Daily -At $Time
$Settings = New-ScheduledTaskSettingsSet -StartWhenAvailable `
              -ExecutionTimeLimit (New-TimeSpan -Hours 1) `
              -DontStopOnIdleEnd

Register-ScheduledTask -TaskName $TaskName -Action $Action -Trigger $Trigger `
    -Settings $Settings -Description "Nightly umod4 hardware test run (WSL)" -Force

Write-Host "Registered scheduled task '$TaskName' to run daily at $Time."
Write-Host "View, edit, or 'Run' it manually via: Task Scheduler -> Task Scheduler Library -> $TaskName"
