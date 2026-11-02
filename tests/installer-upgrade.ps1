param([string]$ObsInstall='C:/Program Files/obs-studio',[string]$ISCC='C:/Tools/Inno Setup 6/ISCC.exe')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot;$workspace=Split-Path $root
$previous=Join-Path $workspace 'outputs/previous/0.5-Beta-before-1.0-2026-11-01'
$suite=Join-Path $root ('build/installer/upgrade-'+[Guid]::NewGuid().ToString('N'))
$oldSource=Join-Path $suite 'previous-source';$target=Join-Path $suite 'plugin';$fixture=Join-Path $suite 'OBS'
$key='HKCU:/Software/Microsoft/Windows/CurrentVersion/Uninstall/OBSscreenMapperIsolatedInstallerTest_is1'
if(Test-Path $key){throw 'Another isolated installer test is registered'}
New-Item -ItemType Directory -Force $oldSource,(Join-Path $fixture 'bin/64bit')|Out-Null
Expand-Archive -LiteralPath (Join-Path $previous 'OBS-Screen-Mapper-0.5-Beta-Source.zip') -DestinationPath $oldSource
Expand-Archive -LiteralPath (Join-Path $previous 'OBS-Screen-Mapper-0.5-Beta-Windows-x64-Runtime.zip') -DestinationPath (Join-Path $oldSource 'build/installer/payload')
& $ISCC /DTestBuild (Join-Path $oldSource 'installer/setup.iss') *> (Join-Path $suite 'previous-build.log')
if($LASTEXITCODE){throw 'Cannot build isolated archived 0.5 Beta installer'}
Copy-Item -LiteralPath (Join-Path $ObsInstall 'bin/64bit/obs64.exe') -Destination (Join-Path $fixture 'bin/64bit/obs64.exe')
Set-Content (Join-Path $fixture 'bin/64bit/obs.dll') 'presence only'
Set-Content (Join-Path $fixture 'bin/64bit/Qt6Widgets.dll') 'presence only'
$args=@('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-','/LANG=english',('/DIR="'+$target+'"'),('/OBSROOT="'+$fixture+'"'))
function Run([string]$file,[string[]]$arguments){$p=Start-Process -FilePath $file -ArgumentList $arguments -WindowStyle Hidden -PassThru;if(!$p.WaitForExit(30000)){throw 'Upgrade fixture timeout'};if($p.ExitCode -ne 0){throw "Upgrade fixture failed: $($p.ExitCode)"}}
Run (Join-Path $oldSource 'build/installer/OBS-Screen-Mapper-Engine-Test.exe') ($args+('/LOG="'+(Join-Path $suite 'install-beta.log')+'"'))
if((Get-ItemProperty $key).DisplayVersion -ne '0.5 Beta'){throw 'Archived beta not registered'}
$oldHashes=@{};foreach($name in @('obs-screen-mapper.dll','obs-screen-mapper-ui.dll')){$oldHashes[$name]=(Get-FileHash (Join-Path $target ('bin/64bit/'+$name))).Hash}
$sentinel=Join-Path $target 'data/personal-preset.json';Set-Content $sentinel '{"personal":true}';$sentinelHash=(Get-FileHash $sentinel).Hash
Run (Join-Path $root 'build/installer/OBS-Screen-Mapper-Engine-Test.exe') ($args+('/LOG="'+(Join-Path $suite 'upgrade-1.0.log')+'"'))
if((Get-ItemProperty $key).DisplayVersion -ne '1.0'){throw 'Upgrade did not replace registration with 1.0'}
foreach($name in $oldHashes.Keys){
    if((Get-FileHash (Join-Path $target ('bin/64bit/'+$name))).Hash -ne (Get-FileHash (Join-Path $root ('dist/obs-screen-mapper/bin/64bit/'+$name))).Hash){throw 'Upgrade payload mismatch'}
    $backups=Get-ChildItem (Join-Path $target 'backups') -Recurse -File|Where-Object Name -EQ $name
    if(@($backups|Where-Object {(Get-FileHash $_.FullName).Hash -eq $oldHashes[$name]}).Count -ne 1){throw 'Original beta DLL not backed up exactly once'}
}
if((Get-FileHash $sentinel).Hash -ne $sentinelHash){throw 'Upgrade changed personal preset'}
Run (Join-Path $target 'uninstall/unins000.exe') @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART')
for($i=0;$i -lt 40 -and (Test-Path $key);$i++){Start-Sleep -Milliseconds 100}
if((Test-Path $key) -or (Test-Path (Join-Path $target 'bin/64bit/obs-screen-mapper.dll'))){throw 'Upgrade uninstall left owned state'}
if((Get-FileHash $sentinel).Hash -ne $sentinelHash){throw 'Uninstall changed personal preset'}
$summary=[ordered]@{passed=$true;from='0.5 Beta';to='1.0';registered_upgrade=$true;both_original_dlls_backed_up=$true;personal_preset_preserved=$true;uninstall=$true;directory=$suite;scope='isolated registered installation; no real OBS launch'}
$summary|ConvertTo-Json|Set-Content (Join-Path $root 'build/installer/upgrade-results.json');$summary|ConvertTo-Json
