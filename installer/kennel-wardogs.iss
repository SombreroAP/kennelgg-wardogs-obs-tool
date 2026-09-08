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

[Tasks]
Name: "distroav"; Description: "Download and install DistroAV 6.2.1 (NDI for OBS, GPL-2) so squad mates on the same network can share feeds. DistroAV will ask you to fetch the NDI Runtime from Vizrt."; Flags: unchecked

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
var
  DlPage: TDownloadWizardPage;

function OnDownloadProgress(const Url, FileName: String; const Progress, ProgressMax: Int64): Boolean;
begin
  Result := True;
end;

procedure InitializeWizard;
begin
  DlPage := CreateDownloadPage(SetupMessage(msgWizardPreparing), SetupMessage(msgPreparingDesc), @OnDownloadProgress);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  ResultCode: Integer;
begin
  Result := True;
  if (CurPageID = wpReady) and WizardIsTaskSelected('distroav') then
  begin
    DlPage.Clear;
    DlPage.Add('https://github.com/DistroAV/DistroAV/releases/download/6.2.1/distroav-6.2.1-windows-x64-Installer.exe', 'distroav-installer.exe', '');
    DlPage.Show;
    try
      try
        DlPage.Download;
        if not Exec(ExpandConstant('{tmp}\distroav-installer.exe'), '', '', SW_SHOWNORMAL, ewWaitUntilTerminated, ResultCode) then
          MsgBox('DistroAV installer could not be started. Get it from https://distroav.org later.', mbInformation, MB_OK);
      except
        MsgBox('DistroAV download failed: ' + GetExceptionMessage + #13#10 + 'Get it from https://distroav.org later.', mbInformation, MB_OK);
      end;
    finally
      DlPage.Hide;
    end;
  end;
end;

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
