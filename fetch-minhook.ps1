$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$third = Join-Path $root 'third_party'
$dest = Join-Path $third 'minhook'
if (Test-Path (Join-Path $dest 'include\MinHook.h')) { exit 0 }
New-Item -ItemType Directory -Force -Path $third | Out-Null
$tmpZip = Join-Path $env:TEMP 'minhook-v1.3.4.zip'
$tmpDir = Join-Path $env:TEMP 'minhook-v1.3.4-src'
Remove-Item -Recurse -Force $tmpDir -ErrorAction SilentlyContinue
Invoke-WebRequest -UseBasicParsing 'https://github.com/TsudaKageyu/minhook/archive/refs/tags/v1.3.4.zip' -OutFile $tmpZip
Expand-Archive -Force $tmpZip $tmpDir
$src = Join-Path $tmpDir 'minhook-1.3.4'
if (!(Test-Path (Join-Path $src 'include\MinHook.h'))) { throw 'Unexpected MinHook archive layout.' }
Remove-Item -Recurse -Force $dest -ErrorAction SilentlyContinue
Move-Item $src $dest
Remove-Item -Recurse -Force $tmpDir -ErrorAction SilentlyContinue
Remove-Item -Force $tmpZip -ErrorAction SilentlyContinue
Write-Host 'Fetched MinHook v1.3.4.'
