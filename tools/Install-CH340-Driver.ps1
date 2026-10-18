$ErrorActionPreference='Stop'
$taskIdentity=[Security.Principal.WindowsIdentity]::GetCurrent()
$taskAdmin=New-Object Security.Principal.WindowsPrincipal($taskIdentity)
if (-not $taskAdmin.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
 Start-Process powershell.exe -Verb RunAs -ArgumentList ('-NoProfile -ExecutionPolicy Bypass -File "'+$PSCommandPath+'"') -Wait
 exit
}
$taskDirs=@((Join-Path $PSScriptRoot 'drivers\CH340'),(Join-Path $PSScriptRoot '..\drivers\CH340'))
$taskFolder=$taskDirs | Where-Object {Test-Path -LiteralPath $_} | Select-Object -First 1
if (-not $taskFolder) {throw 'Offline driver missing. Download CH341SER from https://www.wch.cn/downloads/CH341SER_EXE.html'}
$taskInfs=Get-ChildItem -LiteralPath $taskFolder -Recurse -Filter '*.inf'
if (-not $taskInfs) {throw 'No INF driver found.'}
foreach ($taskInf in $taskInfs) {
 & pnputil.exe /add-driver $taskInf.FullName /install
 if ($LASTEXITCODE -notin @(0,3010)) {throw 'Driver installation failed; see Windows output.'}
}
Write-Host 'CH340 driver installed. Reconnect the USB cable if needed.'
Read-Host 'Press Enter to close'
