param([string]$ObsInstall='C:\Program Files\obs-studio',[string]$Label='presets-optimized')
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$previousPath=$env:PATH
try {
    $env:PATH="$ObsInstall\bin\64bit;$previousPath"
    & (Join-Path $root 'build-ui\Release\preset-benchmark.exe') (Join-Path $root 'build-ui\preset-benchmarks') (Join-Path $root "build\benchmarks\$Label.csv") 2>&1 | Tee-Object (Join-Path $root "build\benchmarks\$Label.log")
    if($LASTEXITCODE){throw "Preset benchmark failed: $LASTEXITCODE"}
} finally {$env:PATH=$previousPath}
