param(
 [string]$Module=(Join-Path $PSScriptRoot 'dist\obs-screen-mapper\bin\64bit\obs-screen-mapper.dll'),
 [string]$Label='optimized',
 [string]$Zig='C:/Tools/zig-0.15.2\zig.exe',
 [string]$ObsSdk='C:/SDK/obs-studio-32.2.2',
 [string]$ObsInstall='C:\Program Files\obs-studio'
)
$ErrorActionPreference='Stop';$root=$PSScriptRoot;$build=Join-Path $root 'build';$bench=Join-Path $build 'benchmarks'
New-Item -ItemType Directory -Force $bench,(Join-Path $bench 'references') | Out-Null
$env:ZIG_LIB_DIR=Join-Path (Split-Path $Zig) 'lib';$env:ZIG_GLOBAL_CACHE_DIR=Join-Path $build 'zig-cache';$env:ZIG_LOCAL_CACHE_DIR=Join-Path $build 'zig-local'
$symbols=@('gs_timer_create','gs_timer_destroy','gs_timer_begin','gs_timer_end','gs_timer_get_data','gs_timer_range_create','gs_timer_range_destroy','gs_timer_range_begin','gs_timer_range_end','gs_timer_range_get_data','gs_flush','obs_add_tick_callback','obs_remove_tick_callback','obs_source_get_signal_handler','signal_handler_connect','signal_handler_disconnect')
$definition=Get-Content (Join-Path $build 'obs.def') -Raw
foreach($symbol in $symbols){if($definition -notmatch "(?m)^$symbol`r?`$"){$definition+="$symbol`n"}}
Set-Content (Join-Path $bench 'obs-benchmark.def') $definition -Encoding ascii
& $Zig dlltool -m i386:x86-64 -d (Join-Path $bench 'obs-benchmark.def') -l (Join-Path $bench 'obs-benchmark.lib')
if($LASTEXITCODE){throw 'Benchmark import library failed'}
& $Zig c++ -target x86_64-windows-gnu -std=c++17 -O2 -Wall -Wextra -Werror "-I$ObsSdk\libobs" "-I$build" (Join-Path $root 'tests\benchmark.cpp') (Join-Path $bench 'obs-benchmark.lib') -ld2d1 -ld3d11 -lole32 -lshlwapi -luuid -lxmllite -luser32 -o (Join-Path $bench 'benchmark.exe')
if($LASTEXITCODE){throw 'Benchmark compilation failed'}
$previousPath=$env:PATH
try{$env:PATH="$ObsInstall\bin\64bit;$previousPath";& (Join-Path $bench 'benchmark.exe') $Module (Join-Path $root 'dist\obs-screen-mapper\data') $ObsInstall (Join-Path $bench "$Label.csv") $Label (Join-Path $bench 'references') 2>&1 | Tee-Object (Join-Path $bench "$Label.log");if($LASTEXITCODE){throw "Benchmark failed: $LASTEXITCODE"}}finally{$env:PATH=$previousPath}
