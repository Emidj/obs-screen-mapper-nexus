param([Parameter(Mandatory)][string]$EnginePath,[Parameter(Mandatory)][string]$OutputExe,[switch]$TestBuild)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$build=Join-Path $root 'build/installer'
$hash=(Get-FileHash -LiteralPath $EnginePath).Hash.ToLowerInvariant()
$metadata='internal static class LauncherPayload {internal const string EngineHash="'+$hash+'";}'
$metaPath=Join-Path $build 'LauncherPayload.cs'
Set-Content $metaPath $metadata -Encoding utf8
$manifestPath=Join-Path $build 'Launcher.manifest'
$level=if($TestBuild){'asInvoker'}else{'requireAdministrator'}
$manifest='<?xml version="1.0" encoding="UTF-8"?><assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0"><assemblyIdentity version="1.0.0.0" name="NEXUS.ScreenMapper.Setup"/><trustInfo xmlns="urn:schemas-microsoft-com:asm.v3"><security><requestedPrivileges><requestedExecutionLevel level="'+$level+'" uiAccess="false"/></requestedPrivileges></security></trustInfo></assembly>'
Set-Content $manifestPath $manifest -Encoding utf8
$argsList=@('/nologo','/target:winexe','/platform:x64','/optimize+',('/out:'+$OutputExe),('/win32icon:'+(Join-Path $PSScriptRoot 'assets/nexus-installer.ico')),('/win32manifest:'+$manifestPath),'/reference:System.Windows.Forms.dll','/reference:System.Drawing.dll','/reference:System.Web.Extensions.dll','/reference:System.Core.dll',('/resource:'+$EnginePath+',engine'),('/resource:'+(Join-Path $PSScriptRoot 'assets/sidebar-original.png')+',branding'),('/resource:'+(Join-Path $PSScriptRoot 'assets/nexus-installer.ico')+',icon'),('/resource:'+(Join-Path $PSScriptRoot 'launcher-strings.json')+',strings'),(Join-Path $PSScriptRoot 'Launcher.cs'),$metaPath)
if($TestBuild){$argsList+='/define:INSTALLER_TEST';$argsList+=(Join-Path $PSScriptRoot 'UiChecks.cs')}
& "$env:WINDIR/Microsoft.NET/Framework64/v4.0.30319/csc.exe" @argsList
if($LASTEXITCODE){throw 'NEXUS launcher compilation failed'}
Write-Output $OutputExe
