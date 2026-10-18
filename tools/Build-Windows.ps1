param([string]$CodexExe='')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskPython=Join-Path $taskRoot '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $taskPython)) {throw 'Run Install-Dev-Dependencies.cmd first.'}
if ($CodexExe) {$env:DESKHUB_CODEX_EXE=(Resolve-Path -LiteralPath $CodexExe).Path}
Push-Location $taskRoot
try {
 & dotnet publish desktop-usb/MediaCollector/MediaCollector.csproj -c Release -o desktop-usb/payload/media
 if ($LASTEXITCODE -ne 0) {throw 'Music collector build failed.'}
 & $taskPython tools/package_usb_desktop.py
 if ($LASTEXITCODE -ne 0) {throw 'Payload packaging failed. Set DESKHUB_CODEX_EXE to native codex.exe.'}
 & dotnet publish desktop-usb/Installer/DeskHubSetup.csproj -c Release -o release/desktop-usb
 if ($LASTEXITCODE -ne 0) {throw 'Installer build failed.'}
 & $taskPython -m PyInstaller --noconfirm --onefile --name DeskHubFlash --collect-all esptool --distpath release/flasher tools/flash_firmware.py
 if ($LASTEXITCODE -ne 0) {throw 'Flasher build failed.'}
} finally {Pop-Location}
