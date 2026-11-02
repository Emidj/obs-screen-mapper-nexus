param([string]$ObsInstall='C:\Program Files\obs-studio',[ValidateSet('spanish','english','german','french','italian')][string]$Language='spanish')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$suite=Join-Path $root ('build\installer\tests-'+[Guid]::NewGuid().ToString('N'))
$target=Join-Path $suite 'Instalación ñ 測試'
$fixture=Join-Path $suite 'OBS de prueba ñ'
$testKey='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\OBSscreenMapperIsolatedInstallerTest_is1'
$realKey='HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\OBSscreenMapper_is1'
if(Test-Path $testKey){throw 'A previous isolated installer test is registered; inspect it before testing again.'}
if(-not ([IO.Path]::GetFullPath($target)).StartsWith(([IO.Path]::GetFullPath($root)+'\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Test target outside workspace'}
New-Item -ItemType Directory -Force $suite,(Join-Path $fixture 'bin\64bit') | Out-Null
$results=[Collections.Generic.List[string]]::new()
function Check([bool]$Condition,[string]$Label){if(-not $Condition){throw "FAIL: $Label"};$results.Add("PASS: $Label");$results|Set-Content (Join-Path $suite 'checks.txt') -Encoding utf8}
function Run([string]$File,[string[]]$Arguments){
    $process=Start-Process -FilePath $File -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    if(-not $process.WaitForExit(30000)){throw 'Isolated installer process did not finish within 30 seconds'}
    return $process.ExitCode
}
function RegistrySnapshot([string]$Key){if(Test-Path $Key){return (Get-ItemProperty $Key|ConvertTo-Json -Depth 5)};return ''}
$realBefore=RegistrySnapshot $realKey
$setup=Join-Path $root 'build\installer\OBS-Screen-Mapper-Installer-Test.exe'
$common=@('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-',('/LANG='+$Language),('/DIR="'+$target+'"'),('/OBSROOT="'+$fixture+'"'))
function Install([string]$Log){return (Run $setup ($common+('/LOG="'+(Join-Path $suite $Log)+'"')))}
Copy-Item (Join-Path $ObsInstall 'bin\64bit\obs64.exe') (Join-Path $fixture 'bin\64bit\obs64.exe')
# The installer checks existence, version and PE architecture; no OBS is launched.
Set-Content (Join-Path $fixture 'bin\64bit\obs.dll') 'fixture-presence-only'
Set-Content (Join-Path $fixture 'bin\64bit\Qt6Widgets.dll') 'fixture-presence-only'
New-Item -ItemType Directory -Force (Join-Path $fixture 'config\obs-studio\basic\scenes') | Out-Null
$scene=Join-Path $fixture 'config\obs-studio\basic\scenes\keep.json'
Set-Content $scene '{ invalid JSON: must remain untouched'
$sceneHash=(Get-FileHash $scene).Hash
$installedDll=Join-Path $target 'bin\64bit\obs-screen-mapper.dll'
$dummy=$null
try {
    $invalidArgs=@('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-',('/LANG='+$Language),('/DIR="'+$target+'"'),('/OBSROOT="'+(Join-Path $suite 'missing')+'"'),('/LOG="'+(Join-Path $suite 'missing-obs.log')+'"'))
    Check ((Run $setup $invalidArgs) -ne 0 -and -not (Test-Path $installedDll)) 'missing OBS rejected without deployment'
    Move-Item -LiteralPath (Join-Path $fixture 'bin\64bit\obs64.exe') -Destination (Join-Path $fixture 'bin\64bit\valid.exe')
    Copy-Item "$env:WINDIR\System32\notepad.exe" (Join-Path $fixture 'bin\64bit\obs64.exe')
    Check ((Install 'wrong-version.log') -ne 0 -and -not (Test-Path $installedDll)) 'unsupported OBS executable rejected'
    Remove-Item -LiteralPath (Join-Path $fixture 'bin\64bit\obs64.exe')
    Move-Item -LiteralPath (Join-Path $fixture 'bin\64bit\valid.exe') -Destination (Join-Path $fixture 'bin\64bit\obs64.exe')
    Remove-Item -LiteralPath (Join-Path $fixture 'bin\64bit\Qt6Widgets.dll')
    Check ((Install 'missing-qt.log') -ne 0 -and -not (Test-Path $installedDll)) 'incomplete OBS installation rejected'
    Set-Content (Join-Path $fixture 'bin\64bit\Qt6Widgets.dll') 'fixture-presence-only'
    Set-Content (Join-Path $fixture 'portable_mode.txt') ''
    Check ((Install 'portable.log') -ne 0 -and -not (Test-Path $installedDll)) 'portable mode explicitly rejected'
    Remove-Item -LiteralPath (Join-Path $fixture 'portable_mode.txt')
    New-Item -ItemType Directory -Force (Join-Path $fixture 'obs-plugins\64bit') | Out-Null
    $duplicate=Join-Path $fixture 'obs-plugins\64bit\obs-screen-mapper.dll'
    Set-Content $duplicate 'manual copy'
    Check ((Install 'duplicate.log') -ne 0 -and (Get-Content $duplicate -Raw).Contains('manual copy')) 'manual duplicate rejected and preserved'
    Remove-Item -LiteralPath $duplicate
    Check ((Install 'fresh.log') -eq 0) 'fresh installation in Unicode path succeeds'
    $payload=Join-Path $root 'build\installer\payload'
    foreach($file in (Get-ChildItem $payload -File -Recurse)){
        $relative=[IO.Path]::GetRelativePath($payload,$file.FullName)
        Check ((Get-FileHash (Join-Path $target $relative)).Hash -eq (Get-FileHash $file.FullName).Hash) ('installed payload hash: '+$relative)
    }
    Check (@(Get-ChildItem $target -File -Recurse|Where-Object Extension -in '.pdb','.lib').Count -eq 0) 'linker artifacts excluded'
    $uninstaller=Join-Path $target 'uninstall\unins000.exe'
    Check ((Test-Path $uninstaller) -and (Test-Path $testKey)) 'isolated uninstaller and registration created'
    $unknown=Join-Path $target 'bin\64bit\unrelated.txt';Set-Content $unknown 'unrelated file, keep'
    $unknownHash=(Get-FileHash $unknown).Hash
    $preset=Join-Path $fixture 'config\obs-studio\plugin_config\obs-screen-mapper\presets\personal.json'
    New-Item -ItemType Directory -Force (Split-Path $preset) | Out-Null
    Set-Content $preset '{"personal":true}'
    $presetHash=(Get-FileHash $preset).Hash
    # A benign, owned process named obs64.exe tests the real WMI detection.
    $dummyCode=Join-Path $suite 'dummy.cs'
    Set-Content $dummyCode 'class Dummy { static void Main() { System.Threading.Thread.Sleep(300000); } }' -Encoding ascii
    $dummyExe=Join-Path $suite 'obs64.exe'
    & "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /target:winexe "/out:$dummyExe" $dummyCode
    if($LASTEXITCODE){throw 'Dummy test process compilation failed'}
    $dummy=Start-Process -FilePath $dummyExe -WindowStyle Hidden -PassThru
    Check ((Install 'running-obs.log') -ne 0) 'installer refuses while OBS-named process is running'
    $languageIndex=@('spanish','english','german','french','italian').IndexOf($Language)
    $expectedMessage=((Get-Content (Join-Path $root 'translations\installer.txt')|Where-Object{$_ -like 'CloseInstall|*'}).Split('|'))[$languageIndex+1]
    Check ((Get-Content (Join-Path $suite 'running-obs.log') -Raw).Contains($expectedMessage)) 'installer refusal message uses selected language'
    Check ((Run $uninstaller @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/LOG="'+(Join-Path $suite 'uninstall-running.log')+'"'))) -ne 0 -and (Test-Path $installedDll)) 'uninstaller refuses while OBS-named process is running'
    if([IO.Path]::GetFullPath($dummy.Path) -ne [IO.Path]::GetFullPath($dummyExe)){throw 'Unexpected test process path'}
    Stop-Process -Id $dummy.Id;$dummy.WaitForExit();$dummy=$null
    Set-Content $installedDll 'previous modified owned file' -Encoding ascii
    $oldHash=(Get-FileHash $installedDll).Hash
    Check ((Install 'replace-with-backup.log') -eq 0) 'reinstallation replaces changed owned payload'
    $backups=@(Get-ChildItem (Join-Path $target 'backups') -File -Recurse)
    Check (@($backups|Where-Object{(Get-FileHash $_.FullName).Hash -eq $oldHash}).Count -eq 1) 'previous changed DLL backed up byte-for-byte'
    $backupCount=$backups.Count
    Check ((Install 'idempotent.log') -eq 0) 'identical reinstall succeeds'
    Check (@(Get-ChildItem (Join-Path $target 'backups') -File -Recurse).Count -eq $backupCount) 'identical reinstall creates no redundant backup'
    Check ((Get-FileHash $scene).Hash -eq $sceneHash) 'invalid unrelated scene JSON never read or modified'
    Check ((Run $uninstaller @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/LOG="'+(Join-Path $suite 'uninstall.log')+'"'))) -eq 0) 'uninstall succeeds'
    for($attempt=0;$attempt -lt 40 -and ((Test-Path $testKey) -or (Test-Path $uninstaller));$attempt++){Start-Sleep -Milliseconds 250}
    Check (-not (Test-Path $installedDll) -and -not (Test-Path (Join-Path $target 'bin\64bit\obs-screen-mapper-ui.dll'))) 'both owned DLLs removed'
    Check ((Get-FileHash $unknown).Hash -eq $unknownHash) 'unrelated file inside plugin directory preserved'
    Check ((Get-FileHash $preset).Hash -eq $presetHash -and (Get-FileHash $scene).Hash -eq $sceneHash) 'presets and scenes preserved on uninstall'
    Check (@(Get-ChildItem (Join-Path $target 'backups') -File -Recurse).Count -eq $backupCount) 'backups preserved on uninstall'
    Check (-not (Test-Path $testKey)) 'isolated uninstall registry entry removed'
    Check (-not (Test-Path $uninstaller)) 'second uninstall has no registered package to remove'
    Check ((RegistrySnapshot $realKey) -eq $realBefore) 'production plugin registration untouched'
    $summary=@{passed=$true;checks=$results.Count;directory=$suite;language=$Language;installer_sha256=(Get-FileHash $setup).Hash;scope='isolated installer fixture; no real OBS installation or rendering'}
    $summary|ConvertTo-Json|Set-Content (Join-Path $root 'build\installer\test-results.json') -Encoding utf8
    $summary|ConvertTo-Json
} finally {
    if($dummy -and -not $dummy.HasExited -and [IO.Path]::GetFullPath($dummy.Path) -eq [IO.Path]::GetFullPath((Join-Path $suite 'obs64.exe'))){Stop-Process -Id $dummy.Id}
}
