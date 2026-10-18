$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskPython=Join-Path $taskRoot '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $taskPython)) {throw 'Run Install-Dev-Dependencies.cmd first.'}
Push-Location $taskRoot
try {
 & $taskPython -m platformio run -d firmware -j 1
 if ($LASTEXITCODE -ne 0) {throw 'Firmware build failed.'}
 & $taskPython tools/package_s3.py
 if ($LASTEXITCODE -ne 0) {throw 'Firmware packaging failed.'}
} finally {Pop-Location}
