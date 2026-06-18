$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$src = Join-Path $root 'main.cpp'
$out = Join-Path $root 'CDriveCleanerWin11R4.exe'

g++ -std=c++17 -O2 -municode -mwindows -finput-charset=UTF-8 -fexec-charset=UTF-8 $src -o $out -lcomctl32 -lshell32 -lole32 -luuid -luxtheme
if ($LASTEXITCODE -ne 0) {
    throw "Build failed. Exit code: $LASTEXITCODE"
}

Write-Host "Generated: $out"
