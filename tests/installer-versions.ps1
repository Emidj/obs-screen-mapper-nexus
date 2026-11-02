param([string]$RuntimeCache)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$workspace=Split-Path $root
if(!$RuntimeCache){$RuntimeCache=Join-Path $workspace 'work/obs-compatibility/runtimes'}
$suite=Join-Path $root ('build/installer/version-matrix-'+[Guid]::NewGuid().ToString('N'))
$key='HKCU:/Software/Microsoft/Windows/CurrentVersion/Uninstall/OBSscreenMapperIsolatedInstallerTest_is1'
if(Test-Path $key){throw 'Another isolated installer test is registered'}
$engine=Join-Path $root 'build/installer/OBS-Screen-Mapper-Engine-Test.exe'
$results=[Collections.Generic.List[object]]::new()
function RunEngine([string]$File,[string[]]$Arguments){
    $p=Start-Process -FilePath $File -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    if(!$p.WaitForExit(30000)){throw 'Installer matrix timeout'}
    return $p.ExitCode
}
$cases=@(
 @{v='31.0.0';root='C:/OBS-Test-Runtimes/31.0.0';ok=$false},
 @{v='31.1.2';root='C:/OBS-Test-Runtimes/31.1.2';ok=$false},
 @{v='32.0.4';root=(Join-Path $RuntimeCache '32.0.4');ok=$false},
 @{v='32.1.2';root=(Join-Path $RuntimeCache '32.1.2');ok=$false},
 @{v='32.2.0';root=(Join-Path $RuntimeCache '32.2.0');ok=$true},
 @{v='32.2.1';root=(Join-Path $RuntimeCache '32.2.1');ok=$true},
 @{v='32.2.2';root='C:/Program Files/obs-studio';ok=$true},
 @{v='33.0.0';root='C:/OBS-Test-Runtimes/33.0.0';ok=$true},
 @{v='33.0.1';root=(Join-Path $RuntimeCache '33.0.1');ok=$true},
 @{v='32.3.0';synthetic=$true;ok=$false},@{v='33.1.0';synthetic=$true;ok=$false},@{v='34.0.0';synthetic=$true;ok=$false})
foreach($case in $cases){
    $directory=Join-Path $suite $case.v
    $fixture=Join-Path $directory 'OBS';$target=Join-Path $directory 'plugin'
    New-Item -ItemType Directory -Force (Join-Path $fixture 'bin/64bit')|Out-Null
    $exe=Join-Path $fixture 'bin/64bit/obs64.exe'
    if($case.synthetic){
        $code=Join-Path $directory 'version.cs'
        Set-Content $code ('[assembly:System.Reflection.AssemblyFileVersion("'+$case.v+'.0")] class VersionFixture{static void Main(){}}')
        & "$env:WINDIR/Microsoft.NET/Framework64/v4.0.30319/csc.exe" /nologo /target:winexe /platform:x64 "/out:$exe" $code
        if($LASTEXITCODE){throw 'Cannot compile synthetic version fixture'}
    }else{Copy-Item -LiteralPath (Join-Path $case.root 'bin/64bit/obs64.exe') -Destination $exe}
    Set-Content (Join-Path $fixture 'bin/64bit/obs.dll') 'presence-only; OBS is not launched'
    Set-Content (Join-Path $fixture 'bin/64bit/Qt6Widgets.dll') 'presence-only'
    $log=Join-Path $directory 'install.log'
    $exit=RunEngine $engine @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-','/LANG=english',('/DIR="'+$target+'"'),('/OBSROOT="'+$fixture+'"'),('/LOG="'+$log+'"'))
    $installed=Join-Path $target 'bin/64bit/obs-screen-mapper.dll'
    if($case.ok){
        if($exit -ne 0 -or !(Test-Path $installed)){throw "Supported OBS rejected: $($case.v)"}
        if((Get-FileHash $installed).Hash -ne (Get-FileHash (Join-Path $root 'dist/obs-screen-mapper/bin/64bit/obs-screen-mapper.dll')).Hash){throw 'Installed hash mismatch'}
        if((Get-ItemProperty $key).DisplayVersion -ne '1.0'){throw 'Wrong installed version'}
        $uninstall=Join-Path $target 'uninstall/unins000.exe'
        if((RunEngine $uninstall @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART')) -ne 0){throw 'Matrix uninstall failed'}
        for($i=0;$i -lt 40 -and (Test-Path $key);$i++){Start-Sleep -Milliseconds 100}
        if((Test-Path $installed) -or (Test-Path $key)){throw 'Matrix uninstall left owned state'}
    }elseif($exit -eq 0 -or (Test-Path $installed) -or (Test-Path $key)){throw "Unsupported OBS accepted: $($case.v)"}
    $results.Add([ordered]@{version=$case.v;synthetic=[bool]$case.synthetic;expected_accept=$case.ok;installer_exit=$exit;passed=$true})
    $results|ConvertTo-Json -Depth 4|Set-Content (Join-Path $suite 'results.json')
}
$summary=[ordered]@{passed=$true;versions=$results.Count;directory=$suite;scope='installer version fixtures; no real OBS launch';results=$results}
$summary|ConvertTo-Json -Depth 6|Set-Content (Join-Path $root 'build/installer/version-matrix-results.json')
$summary|ConvertTo-Json -Depth 6
