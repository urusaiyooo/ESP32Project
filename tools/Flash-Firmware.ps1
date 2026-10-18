param([Parameter(ValueFromRemainingArguments=$true)][string[]]$Options)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskPython=Join-Path $taskRoot '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $taskPython)) {throw 'Source version needs development dependencies. Or download the self-contained Windows release.'}
& $taskPython (Join-Path $PSScriptRoot 'flash_firmware.py') @Options
exit $LASTEXITCODE
