param(
 [string]$CMake='C:\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
 [string]$Qt='C:/SDK/obs-deps-qt6-2025-07-11-x64',
 [string]$ObsSdk='C:/SDK/obs-studio-32.2.2',
 [string]$ObsInstall='C:\Program Files\obs-studio'
)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
function Invoke-EditorCmake([string[]]$Arguments) {
 $start=[System.Diagnostics.ProcessStartInfo]::new();$start.FileName=$CMake;$start.UseShellExecute=$false;$start.WorkingDirectory=$root
 foreach($argument in $Arguments){$start.ArgumentList.Add($argument)}
 $keys=@($start.Environment.Keys | Where-Object {$_ -ieq 'Path'});$values=@($keys | ForEach-Object {$start.Environment[$_]})
 foreach($key in $keys){$start.Environment.Remove($key)|Out-Null};$start.Environment['Path']=$values -join ';'
 $process=[System.Diagnostics.Process]::Start($start);$process.WaitForExit();if($process.ExitCode){throw "Editor CMake failed: $($process.ExitCode)"}
}
Invoke-EditorCmake @('-S',(Join-Path $root 'ui'),'-B',(Join-Path $root 'build-ui'),'-G','Visual Studio 17 2022','-A','x64',"-DCMAKE_PREFIX_PATH=$Qt", "-DOBS_HEADERS=$ObsSdk", "-DCORE_BUILD=$root\build", "-DOBS_IMPORT=$root\build\obs.lib")
Invoke-EditorCmake @('--build',(Join-Path $root 'build-ui'),'--config','Release')
Copy-Item -LiteralPath (Join-Path $root 'build-ui\Release\obs-screen-mapper-ui.dll') -Destination (Join-Path $root 'dist\obs-screen-mapper\bin\64bit\obs-screen-mapper-ui.dll') -Force
$previousPath=$env:PATH;$previousPlugin=$env:QT_PLUGIN_PATH
try {
 $env:PATH="$ObsInstall\bin\64bit;$previousPath";$env:QT_PLUGIN_PATH="$ObsInstall\bin\64bit"
 & (Join-Path $root 'build-ui\Release\preset-test.exe') (Join-Path $root 'build-ui\preset-tests') 2>&1 | Tee-Object (Join-Path $root 'build-ui\preset-validation.log')
 if($LASTEXITCODE){throw "Preset test failed: $LASTEXITCODE"}
 & (Join-Path $root 'build-ui\Release\editor-test.exe') (Join-Path $root 'dist\obs-screen-mapper\bin\64bit\obs-screen-mapper.dll') (Join-Path $root 'dist\obs-screen-mapper\data') (Join-Path $ObsInstall 'data\libobs') (Join-Path $root 'build-ui\captures') 2>&1 | Tee-Object (Join-Path $root 'build-ui\editor-validation.log')
 if($LASTEXITCODE){throw "Editor test failed: $LASTEXITCODE"}
 & (Join-Path $root 'build-ui\Release\locale-test.exe') (Join-Path $root 'dist\obs-screen-mapper\bin\64bit\obs-screen-mapper.dll') (Join-Path $root 'dist\obs-screen-mapper\data') $ObsInstall (Join-Path $root 'build-ui\locales') 2>&1 | Tee-Object (Join-Path $root 'build-ui\locale-validation.log')
 if($LASTEXITCODE){throw "Locale test failed: $LASTEXITCODE"}
} finally {$env:PATH=$previousPath;$env:QT_PLUGIN_PATH=$previousPlugin}
