$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskPython=$null
if (Get-Command py -ErrorAction SilentlyContinue) {
  $taskPython=& py -3.12 -c 'import sys; print(sys.executable)' 2>$null
  if ($LASTEXITCODE -ne 0) {$taskPython=$null}
}
if (-not $taskPython) {
  $taskCandidate=Join-Path $env:LOCALAPPDATA 'Programs\Python\Python312\python.exe'
  if (Test-Path -LiteralPath $taskCandidate) {$taskPython=$taskCandidate}
}
if (-not $taskPython) {
  if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {throw 'Install Python 3.12 from https://www.python.org/downloads/windows/ then retry.'}
  & winget install --id Python.Python.3.12 --exact --silent --accept-package-agreements --accept-source-agreements
  if ($LASTEXITCODE -ne 0) {throw 'Python installation failed.'}
  $taskPython=Join-Path $env:LOCALAPPDATA 'Programs\Python\Python312\python.exe'
}
$taskHasSdk=$false
if (Get-Command dotnet -ErrorAction SilentlyContinue) {$taskHasSdk=[bool]((& dotnet --list-sdks) -match '^8\.')}
if (-not $taskHasSdk) {
  if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {throw 'Install .NET SDK 8 from https://dotnet.microsoft.com/download/dotnet/8.0 then retry.'}
  & winget install --id Microsoft.DotNet.SDK.8 --exact --silent --accept-package-agreements --accept-source-agreements
  if ($LASTEXITCODE -ne 0) {throw '.NET SDK installation failed.'}
}
& $taskPython -m venv (Join-Path $taskRoot '.venv')
if ($LASTEXITCODE -ne 0) {throw 'Could not create .venv.'}
$taskVenv=Join-Path $taskRoot '.venv\Scripts\python.exe'
& $taskVenv -m pip install -r (Join-Path $taskRoot 'requirements-dev.txt')
if ($LASTEXITCODE -ne 0) {throw 'Dependency installation failed.'}
Write-Host 'Development dependencies installed. See docs/DEVELOPMENT.md.'
