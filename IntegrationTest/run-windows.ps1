# Runs the rideshare app under the locally built Windows profiler, against the
# on-box Pyroscope (WSL2 Docker; see the win-dev-vm README in deployment_tools).
# Builds the app + managed helper from this working tree, so everything tested
# is current. Usage (on the dev VM):
#   C:\dev\pyroscope-dotnet\IntegrationTest\run-windows.ps1 [-Framework net8.0]
param(
  [string]$Framework = "net8.0",
  [string]$ServiceName = "rideshare.dotnet.win.dev",
  [int]$Port = 5000
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot

$profilerDll = Join-Path $repo "profiler\_build\bin\Release-x64\profiler\src\ProfilerEngine\Datadog.Profiler.Native.Windows\Pyroscope.Profiler.Native.dll"
if (-not (Test-Path $profilerDll)) {
  throw "Profiler DLL not found at $profilerDll - build it first (MSBuild command in the win-dev-vm README)."
}

# Ensure the on-box Pyroscope is serving; start (or create) the container in
# WSL2 Docker if not. The keeper holds the WSL VM for this session's lifetime.
# NOTE: probing native commands emit stderr we expect ("no such container");
# under ErrorActionPreference=Stop, redirected stderr becomes a TERMINATING
# NativeCommandError (PS 5.1), so relax it for this block only.
$wsl = "C:\Program Files\WSL\wsl.exe"
$ErrorActionPreference = "Continue"
$ready = curl.exe -s -m 3 http://127.0.0.1:4040/ready 2>$null
if ($ready -ne "ready") {
  Write-Host ">> Starting Pyroscope in WSL2 Docker..."
  Start-Process -WindowStyle Hidden $wsl -ArgumentList "-d", "Ubuntu-26.04", "-u", "root", "--", "sleep", "infinity"
  Start-Sleep 3
  & $wsl -d Ubuntu-26.04 -u root -- docker start pyroscope 2>$null | Out-Null
  if ($LASTEXITCODE -ne 0) {
    # Same pinned image setup-wsl.ps1 pre-pulls (and the integration tests use),
    # so this works without pulling.
    & $wsl -d Ubuntu-26.04 -u root -- docker run -d --restart unless-stopped --name pyroscope -p 4040:4040 "grafana/pyroscope@sha256:0ad897bb457228c7d1133ce7004f83052e635be9daf4a7cc90364317637e9754"
    if ($LASTEXITCODE -ne 0) { Write-Warning "docker run failed - is WSL2 set up? (C:\wsl\setup-wsl.ps1)" }
  }
  $deadline = (Get-Date).AddSeconds(90)
  do {
    $ready = curl.exe -s -m 3 http://127.0.0.1:4040/ready 2>$null
    if ($ready -eq "ready") { break }
    Start-Sleep 5
  } while ((Get-Date) -lt $deadline)
  if ($ready -ne "ready") {
    Write-Warning "Pyroscope not reachable at 127.0.0.1:4040 - uploads will fail. On a NESTED_VIRT=0 instance use the reverse-tunnel fallback (README)."
  }
}
$ErrorActionPreference = "Stop"

Write-Host ">> Publishing rideshare ($Framework)..."
$out = Join-Path $PSScriptRoot "bin\win-run\$Framework"
dotnet publish $PSScriptRoot -c Release --framework $Framework --runtime win-x64 --no-self-contained -o $out
if ($LASTEXITCODE -ne 0) { throw "dotnet publish failed" }

$env:CORECLR_ENABLE_PROFILING = "1"
$env:CORECLR_PROFILER = "{BD1A650D-AC5D-4896-B64F-D6FA25D6B26A}"
$env:CORECLR_PROFILER_PATH = $profilerDll
$env:PYROSCOPE_SERVER_ADDRESS = "http://127.0.0.1:4040"
$env:PYROSCOPE_APPLICATION_NAME = $ServiceName
$env:PYROSCOPE_PROFILING_ENABLED = "1"
$env:PYROSCOPE_PROFILING_ALLOCATION_ENABLED = "true"
$env:PYROSCOPE_PROFILING_CONTENTION_ENABLED = "true"
$env:PYROSCOPE_PROFILING_EXCEPTION_ENABLED = "true"
$env:ASPNETCORE_URLS = "http://localhost:$Port"

Write-Host ""
Write-Host ">> Running rideshare as '$ServiceName' on http://localhost:$Port (/bike /scooter /car ...)"
Write-Host ">> Profiles: http://127.0.0.1:4040 on this box; from your machine: ssh -L 4040:localhost:4040 <instance-id>"
Write-Host ""
& (Join-Path $out "rideshare.exe")
