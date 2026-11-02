#define Version "1.0"
[Setup]
#ifdef TestBuild
AppId=OBSscreenMapperIsolatedInstallerTest
PrivilegesRequired=lowest
DefaultDirName={src}\fixture-target
OutputBaseFilename=OBS-Screen-Mapper-Engine-Test
OutputDir=..\build\installer
#else
AppId=OBSscreenMapper
PrivilegesRequired=admin
DefaultDirName={commonappdata}\obs-studio\plugins\obs-screen-mapper
OutputBaseFilename=OBS-Screen-Mapper-1.0-Engine
OutputDir=..\build\installer
#endif
AppName=OBS Screen Mapper
AppVersion={#Version}
AppVerName=OBS Screen Mapper {#Version}
AppPublisher=DjEmiMas
AppPublisherURL=https://discord.gg/dhcqZPDYh
DefaultGroupName=OBS Screen Mapper
DisableDirPage=yes
DisableProgramGroupPage=yes
UsePreviousAppDir=no
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
WizardSizePercent=120
DisableWelcomePage=no
DisableFinishedPage=no
WizardImageFile=assets\nexus-sidebar.bmp
WizardSmallImageFile=assets\nexus-header.bmp
WizardImageStretch=yes
WizardImageBackColor=$1A0F09
SetupIconFile=assets\nexus-installer.ico
UninstallDisplayIcon={uninstallexe}
VersionInfoVersion=1.0.0.0
VersionInfoTextVersion=1.0
VersionInfoProductVersion=1.0.0.0
VersionInfoProductTextVersion=1.0
VersionInfoDescription=OBS Screen Mapper 1.0 Setup
ShowLanguageDialog=no
LanguageDetectionMethod=uilanguage
UninstallDisplayName=OBS Screen Mapper {#Version}
UninstallFilesDir={app}\uninstall
CloseApplications=no
RestartApplications=no
SetupLogging=yes
LicenseFile=..\LICENSE.txt

AlwaysShowDirOnReadyPage=yes

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"; InfoBeforeFile: "info.txt"
Name: "english"; MessagesFile: "compiler:Default.isl"; InfoBeforeFile: "info-en.txt"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"; InfoBeforeFile: "info-de.txt"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"; InfoBeforeFile: "info-fr.txt"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"; InfoBeforeFile: "info-it.txt"

[CustomMessages]
#include "messages.iss"

[Dirs]
Name: "{app}\data"

[Files]
#include "payload-files.iss"

[Code]
var OBSPage: TInputDirWizardPage;

function GetBinaryType(FileName: String; var BinaryType: Cardinal): Boolean;
external 'GetBinaryTypeW@kernel32.dll stdcall';

function OBSRunning: Boolean;
var Locator, Service, Processes: Variant;
begin
  Locator := CreateOleObject('WbemScripting.SWbemLocator');
  Service := Locator.ConnectServer('', 'root\CIMV2');
  Processes := Service.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name = ''obs64.exe'' OR Name = ''obs32.exe''');
  Result := Processes.Count > 0;
end;

function DetectOBS: String;
begin
  Result := ExpandConstant('{autopf}\obs-studio');
  if RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', Result) then exit;
  if RegQueryStringValue(HKLM32, 'SOFTWARE\OBS Studio', '', Result) then exit;
end;

function ValidateOBS: String;
var Root, Executable: String; VersionMS, VersionLS, BinaryType: Cardinal;
begin
  Result := '';
  Root := RemoveBackslashUnlessRoot(OBSPage.Values[0]);
  Executable := Root + '\bin\64bit\obs64.exe';
  if not FileExists(Executable) then begin
    Result := CustomMessage('MissingOBS'); exit;
  end;
  if not GetBinaryType(Executable, BinaryType) or (BinaryType <> 6) then begin
    Result := CustomMessage('WrongArch'); exit;
  end;
  if not GetVersionNumbers(Executable, VersionMS, VersionLS) then begin
    Result := CustomMessage('WrongVersion'); exit;
  end;
  if not ((((VersionMS shr 16) = 32) and ((VersionMS and $FFFF) = 2)) or
          (((VersionMS shr 16) = 33) and ((VersionMS and $FFFF) = 0))) then begin
    Result := CustomMessage('WrongVersion'); exit;
  end;
  if not FileExists(Root + '\bin\64bit\obs.dll') or not FileExists(Root + '\bin\64bit\Qt6Widgets.dll') then begin
    Result := CustomMessage('MissingRuntime'); exit;
  end;
  if FileExists(Root + '\portable_mode.txt') or FileExists(Root + '\portable_mode') then begin
    Result := CustomMessage('Portable'); exit;
  end;
  // Prevent two copies of the same source being discovered by OBS.
  if FileExists(Root + '\obs-plugins\64bit\obs-screen-mapper.dll') or
     FileExists(Root + '\obs-plugins\64bit\obs-screen-mapper-ui.dll') then begin
    Result := CustomMessage('Duplicate'); exit;
  end;
end;

procedure InitializeWizard;
begin
  OBSPage := CreateInputDirPage(wpWelcome, CustomMessage('SelectOBS'),
    'OBS Studio 32.2.x / 33.0.x · Windows x64',
    CustomMessage('OBSHelp'), False, '');
  OBSPage.Add(CustomMessage('OBSFolder'));
  OBSPage.Values[0] := ExpandConstant('{param:OBSROOT|}') ;
  if OBSPage.Values[0] = '' then OBSPage.Values[0] := DetectOBS;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var Error: String;
begin
  Result := True;
  if (CurPageID = OBSPage.ID) and not WizardSilent then begin
    Error := ValidateOBS;
    if Error <> '' then begin MsgBox(Error, mbError, MB_OK); Result := False; end;
  end;
end;

function StageAndBackup(const FileName, RelativeName, ExpectedHash: String): Boolean;
var Existing, Backup, BackupBase: String; Suffix: Integer;
begin
  Result := False;
  ExtractTemporaryFile(FileName);
  if CompareText(GetSHA256OfFile(ExpandConstant('{tmp}\') + FileName), ExpectedHash) <> 0 then exit;
  Existing := ExpandConstant('{app}\') + RelativeName;
  if FileExists(Existing) and (CompareText(GetSHA256OfFile(Existing), ExpectedHash) <> 0) then begin
    BackupBase := ExpandConstant('{app}\backups\') + GetDateTimeString('yyyymmdd-hhnnss', '-', ':') + '\' + RelativeName;
    Backup := BackupBase; Suffix := 0;
    while FileExists(Backup) do begin Suffix := Suffix + 1; Backup := BackupBase + '.' + IntToStr(Suffix); end;
    if not ForceDirectories(ExtractFileDir(Backup)) then exit;
    if not CopyFile(Existing, Backup, True) then exit;
  end;
  Result := True;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  try
    if OBSRunning then begin Result := CustomMessage('CloseInstall'); exit; end;
    Result := ValidateOBS;
    if Result <> '' then exit;
    // Stage/hash-check all runtime payloads before deployment.
#include "payload-checks.iss"
  except
    Log(GetExceptionMessage);
    Result := CustomMessage('PrepareError');
  end;
end;

function InitializeUninstall: Boolean;
begin
  Result := False;
  try
    if OBSRunning then begin
      if not UninstallSilent then MsgBox(CustomMessage('CloseUninstall'), mbError, MB_OK);
      exit;
    end;
    Result := True;
  except
    if not UninstallSilent then MsgBox(CustomMessage('UninstallError'), mbError, MB_OK);
  end;
end;

[UninstallDelete]
; Remove only empty owned directories. Keep unknown files, backups and OBS configuration.
Type: dirifempty; Name: "{app}\bin\64bit"
Type: dirifempty; Name: "{app}\bin"
Type: dirifempty; Name: "{app}\data\examples"
Type: dirifempty; Name: "{app}\data"
Type: dirifempty; Name: "{app}\uninstall"
Type: dirifempty; Name: "{app}"




