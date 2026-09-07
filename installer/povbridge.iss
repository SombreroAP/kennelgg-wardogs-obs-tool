; POVBridge for OBS - Windows installer (Inno Setup 6)
; Installs the portable-plugin layout into OBS's shared plugin folder, which OBS 30+ scans on start:
;   C:\ProgramData\obs-studio\plugins\povbridge\bin\64bit\povbridge.dll  +  data\

#ifndef VERSION
  #define VERSION "0.0.0"
#endif
#ifndef SRC
  #define SRC "..\release\RelWithDebInfo\povbridge"
#endif
#ifndef OUTDIR
  #define OUTDIR "..\release"
#endif

[Setup]
AppId={{7C1E6B0A-4F5D-4C7B-9C0E-POVBRIDGE0001}
AppName=POVBridge for OBS
AppVersion={#VERSION}
AppVerName=POVBridge for OBS {#VERSION}
AppPublisher=Sombrero / The Kennel
AppPublisherURL=https://kennel.gg
DefaultDirName={commonappdata}\obs-studio\plugins\povbridge
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir={#OUTDIR}
OutputBaseFilename=povbridge-{#VERSION}-windows-x64-installer
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName=POVBridge for OBS
WizardStyle=modern
SetupLogging=yes

[Files]
Source: "{#SRC}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Messages]
WelcomeLabel2=This installs POVBridge into OBS Studio's plugin folder.%n%nClose OBS before continuing. After installing, start OBS and open View > Docks > POVBridge.

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
