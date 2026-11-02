param([string]$ISCC='C:/Tools/Inno Setup 6/ISCC.exe',[switch]$TestBuild)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
& (Join-Path $root 'translations\generate.ps1')
& (Join-Path $root 'installer\build-assets.ps1')
$stage=Join-Path $root 'build\installer\payload'
$outputs=Join-Path (Split-Path $root) 'outputs'
$expected=@{'obs-screen-mapper.dll'='B6EDF60E80F308729B8AD03D6B722AA71062C66036E3F977ED2F40363C296B14';'obs-screen-mapper-ui.dll'='DBCDAEDDDA51B536F4815004A52012B12D011F397436D1DB155B7E7902C46DBA'}
foreach($name in $expected.Keys){$path=Join-Path $root "dist\obs-screen-mapper\bin\64bit\$name";if((Get-FileHash $path).Hash -ne $expected[$name]){throw "Binary changed since transform validation: $name"}}
$logs=@{'build\validation.log'=312;'build\gpu-validation.log'=292;'build\polygon-validation.log'=13;'build\mesh-validation.log'=19;'build\mask-validation.log'=15;'build-ui\editor-validation.log'=199;'build-ui\preset-validation.log'=48}
foreach($entry in $logs.GetEnumerator()){$lines=Get-Content (Join-Path $root $entry.Key);if(@($lines|Where-Object{$_ -match '^FAIL:'}).Count -or @($lines|Where-Object{$_ -match '^PASS:'}).Count -ne $entry.Value){throw "Invalid validation log: $($entry.Key)"}}
$localeLog=Get-Content (Join-Path $root 'build-ui\locale-validation.log') -Raw
if($localeLog -match '(?m)^FAIL:' -or $localeLog -notmatch 'PASS: 218 locale checks complete'){throw 'Locale tests must pass before packaging'}
New-Item -ItemType Directory -Force $stage,$outputs,(Join-Path $stage 'bin\64bit'),(Join-Path $stage 'data\examples') | Out-Null
foreach($name in $expected.Keys){Copy-Item (Join-Path $root "dist\obs-screen-mapper\bin\64bit\$name") (Join-Path $stage "bin\64bit\$name") -Force}
Copy-Item (Join-Path $root 'LICENSE.txt'),(Join-Path $root 'NOTICE.txt') (Join-Path $stage 'data') -Force
Copy-Item (Join-Path $root 'docs\PROBAR.txt') (Join-Path $stage 'data\PROBAR.txt') -Force
Set-Content (Join-Path $stage 'data\VERSION.txt') 'OBS Screen Mapper 1.0' -Encoding utf8
Copy-Item (Join-Path $root 'docs\examples\presets\escenario-curvo-6-zonas.mapper.json') (Join-Path $stage 'data\examples') -Force
$starter=Join-Path $root 'docs\examples\presets\starter'
if(Test-Path $starter){
    New-Item -ItemType Directory -Force (Join-Path $stage 'data\examples\starter') | Out-Null
    Copy-Item (Join-Path $starter '*.mapper.json') (Join-Path $stage 'data\examples\starter') -Force
}
$hashLines=@();$files=@();$checks=@()
foreach($file in (Get-ChildItem $stage -File -Recurse|Where-Object Name -ne 'SHA256SUMS.txt'|Sort-Object FullName)){
    $relative=[IO.Path]::GetRelativePath($stage,$file.FullName)
    $hash=(Get-FileHash $file.FullName).Hash
    $hashLines+="$hash  $relative"
}
Set-Content (Join-Path $stage 'data\SHA256SUMS.txt') $hashLines -Encoding ascii
foreach($file in (Get-ChildItem $stage -File -Recurse|Sort-Object FullName)){
    $relative=[IO.Path]::GetRelativePath($stage,$file.FullName)
    $directory=Split-Path $relative
    $hash=(Get-FileHash $file.FullName).Hash
    $files+='Source: "..\build\installer\payload\'+$relative+'"; DestDir: "{app}\'+$directory+'"; Flags: ignoreversion'
    $checks+="    if not StageAndBackup('$($file.Name)', '$relative', '$hash') then begin Result := FmtMessage(CustomMessage('PayloadError'), ['$($file.Name)']); exit; end;"
}
Set-Content (Join-Path $root 'installer\payload-files.iss') $files -Encoding utf8
Set-Content (Join-Path $root 'installer\payload-checks.iss') $checks -Encoding utf8
$arguments=@();if($TestBuild){$arguments+='/DTestBuild'};$arguments+=(Join-Path $root 'installer\setup.iss')
& $ISCC @arguments
if($LASTEXITCODE){throw 'Installer compilation failed'}
$engine=Join-Path $root $(if($TestBuild){'build\installer\OBS-Screen-Mapper-Engine-Test.exe'}else{'build\installer\OBS-Screen-Mapper-1.0-Engine.exe'})
$launcher=if($TestBuild){Join-Path $root 'build\installer\OBS-Screen-Mapper-Installer-Test.exe'}else{Join-Path $outputs 'OBS-Screen-Mapper-1.0-Windows-x64-Setup.exe'}
& (Join-Path $root 'installer\build-launcher.ps1') -EnginePath $engine -OutputExe $launcher -TestBuild:$TestBuild
if(-not $TestBuild){
    Compress-Archive -Path (Join-Path $stage 'bin'),(Join-Path $stage 'data') -DestinationPath (Join-Path $outputs 'OBS-Screen-Mapper-1.0-Windows-x64-Runtime.zip') -Force
    Copy-Item (Join-Path $root 'docs\PROBAR.txt') (Join-Path $outputs 'OBS-Screen-Mapper-LEEME.txt') -Force
    $sources=@('src','ui','tests','docs','installer','translations','README.md','LICENSE.txt','NOTICE.txt','build.ps1','build-ui.ps1','benchmark.ps1','benchmark-presets.ps1','package.ps1')|ForEach-Object{Join-Path $root $_}
    Compress-Archive -Path $sources -DestinationPath (Join-Path $outputs 'OBS-Screen-Mapper-1.0-Source.zip') -Force
    Get-ChildItem $outputs -File -Filter 'OBS-Screen-Mapper-1.0-*'|Where-Object Extension -in '.exe','.zip'|Sort-Object Name|ForEach-Object{(Get-FileHash $_.FullName).Hash+'  '+$_.Name}|Set-Content (Join-Path $outputs 'OBS-Screen-Mapper-1.0-SHA256SUMS.txt') -Encoding ascii
}











