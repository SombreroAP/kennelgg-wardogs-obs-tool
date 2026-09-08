; Kennel.gg WARDOGS OBS Tools - Windows installer (Inno Setup 6)
; Installs the portable-plugin layout into OBS's shared plugin folder, which OBS 30+ scans on start:
;   C:\ProgramData\obs-studio\plugins\kennel-wardogs\bin\64bit\kennel-wardogs.dll  +  data\

#ifndef VERSION
  #define VERSION "0.0.0"
#endif
#ifndef SRC
  #define SRC "..\release\RelWithDebInfo\kennel-wardogs"
#endif
#ifndef OUTDIR
  #define OUTDIR "..\release"
#endif
#ifndef APPSRC
  #define APPSRC "..\release\app\ClipHound"
#endif

[Setup]
AppId={{7C1E6B0A-4F5D-4C7B-9C0E-KENNELWD0001}
AppName=Kennel.gg WARDOGS OBS Tools
AppVersion={#VERSION}
AppVerName=Kennel.gg WARDOGS OBS Tools {#VERSION}
AppPublisher=Sombrero / The Kennel
AppPublisherURL=https://kennel.gg
DefaultDirName={commonappdata}\obs-studio\plugins\kennel-wardogs
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir={#OUTDIR}
OutputBaseFilename=kennel-wardogs-{#VERSION}-windows-x64-installer
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName=Kennel.gg WARDOGS OBS Tools
WizardStyle=modern
SetupLogging=yes

[Types]
Name: "full"; Description: "OBS plugin + ClipHound app (recommended)"
Name: "plugin"; Description: "OBS plugin only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "plugin"; Description: "Kennel WARDOGS OBS plugin (POV swap, clips)"; Types: full plugin custom; Flags: fixed
Name: "app"; Description: "ClipHound - kill-feed OCR clipping app (auto-started by the plugin)"; Types: full

[Dirs]
Name: "{commonappdata}\Kennel WARDOGS\ClipHound"; Permissions: users-modify; Components: app

[Files]
Source: "{#SRC}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: plugin
Source: "{#APPSRC}\*"; DestDir: "{commonappdata}\Kennel WARDOGS\ClipHound"; Excludes: "config.yaml"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: app
Source: "{#APPSRC}\config.default.yaml"; DestDir: "{commonappdata}\Kennel WARDOGS\ClipHound"; DestName: "config.yaml"; Flags: onlyifdoesntexist uninsneveruninstall; Components: app

[Icons]
Name: "{commonprograms}\Kennel WARDOGS\ClipHound"; Filename: "{commonappdata}\Kennel WARDOGS\ClipHound\ClipHound.exe"; WorkingDir: "{commonappdata}\Kennel WARDOGS\ClipHound"; Components: app
Name: "{commonprograms}\Kennel WARDOGS\ClipHound setup"; Filename: "{commonappdata}\Kennel WARDOGS\ClipHound\ClipHound.exe"; Parameters: "--setup"; WorkingDir: "{commonappdata}\Kennel WARDOGS\ClipHound"; Components: app

[Run]
Filename: "{commonappdata}\Kennel WARDOGS\ClipHound\ClipHound.exe"; Parameters: "--setup"; WorkingDir: "{commonappdata}\Kennel WARDOGS\ClipHound"; Description: "Run ClipHound setup now (your in-game name, Twitch login)"; Flags: postinstall nowait skipifsilent unchecked; Components: app

[Messages]
WelcomeLabel2=This installs the Kennel.gg WARDOGS OBS plugin into OBS Studio's plugin folder and, optionally, the ClipHound clipping app (C:\ProgramData\Kennel WARDOGS\ClipHound), which the plugin starts with OBS.%n%nClose OBS before continuing. After installing, start OBS and open View > Docks > Kennel WARDOGS.

[Code]
function IsOBSRunning(): Boolean;
var
  ResultCode: Integer;
begin
  Result := False;
  if Exec('cmd.exe', '/c tasklist /FI "IMAGENAME eq obs64.exe" | find /I "obs64.exe" >nul', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    Result := (ResultCode = 0);
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
  while IsOBSRunning() do
  begin
    if MsgBox('OBS Studio is running. Close it, then press Retry.', mbError, MB_RETRYCANCEL) = IDCANCEL then
    begin
      Result := False;
      Exit;
    end;
  end;
end;
