; Plan 33b Task 5: per-user installer (Inno Setup 6.3 or later). No admin
; prompt: it installs to %LOCALAPPDATA%\Programs\Road Rash Native, adds a
; Start-menu shortcut, an optional desktop shortcut and an uninstaller that
; keeps saves and the imported disc unless the user asks to remove them.
; Built by tools/build_windows_package.py, which passes these defines:
;   AppVersion        display version, e.g. 0.1.0 or 0.0.0-g1a2b3c4
;   NumericVersion    X.Y.Z.0 for the setup's version resource
;   Arch              x64 or arm64
;   SourceDir         staged "Road Rash Native" folder (no portable.txt used)
;   OutputDir, OutputBaseFilename, IconFile
;   SignTool          optional; set only when signing is configured
#if !Defined(AppVersion) || !Defined(NumericVersion) || !Defined(Arch) || !Defined(SourceDir) || !Defined(OutputDir) || !Defined(OutputBaseFilename) || !Defined(IconFile)
  #error Run tools/build_windows_package.py, which supplies the required defines
#endif

[Setup]
AppId={{456A11B4-D2B9-4328-847F-E35539EDE7F1}
AppName=Road Rash Native
AppVersion={#AppVersion}
AppVerName=Road Rash Native {#AppVersion}
AppPublisher=Road Rash Native port
VersionInfoVersion={#NumericVersion}
VersionInfoProductName=Road Rash Native
VersionInfoDescription=Road Rash Native setup
DefaultDirName={autopf}\Road Rash Native
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
#if Arch == "arm64"
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#else
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#endif
; The UTF-8 active code page in the game's manifest needs 1903 (build 18362).
MinVersion=10.0.18362
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
SetupIconFile={#IconFile}
UninstallDisplayIcon={app}\rr-game.exe
UninstallDisplayName=Road Rash Native
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
#ifdef SignTool
SignTool={#SignTool}
SignedUninstaller=yes
#endif

[Tasks]
Name: desktopicon; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; An installed copy is never portable, even if someone copied the marker in.
Type: files; Name: "{app}\portable.txt"

[Files]
Source: "{#SourceDir}\rr-game.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\README.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Road Rash Native"; Filename: "{app}\rr-game.exe"
Name: "{autodesktop}\Road Rash Native"; Filename: "{app}\rr-game.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\rr-game.exe"; Description: "{cm:LaunchProgram,Road Rash Native}"; Flags: nowait postinstall skipifsilent

[Code]
// Saves and settings (%APPDATA%) and the imported disc and logs
// (%LOCALAPPDATA%) outlive the program files unless the user asks. A silent
// uninstall keeps them; `unins000.exe /VERYSILENT /PURGE=1` removes them.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Roaming, Local: String;
  Remove: Boolean;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;
  Roaming := ExpandConstant('{userappdata}\RoadRashNative');
  Local := ExpandConstant('{localappdata}\RoadRashNative');
  if not (DirExists(Roaming) or DirExists(Local)) then
    Exit;
  if UninstallSilent then
    Remove := ExpandConstant('{param:PURGE|0}') = '1'
  else
    Remove := MsgBox('Also remove your Road Rash Native game data?' + #13#10#13#10 +
      'This deletes saves, settings, the imported disc and logs in:' + #13#10 +
      Roaming + #13#10 + Local + #13#10#13#10 +
      'Choose No to keep them for a later install.',
      mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
  if Remove then
  begin
    DelTree(Roaming, True, True, True);
    DelTree(Local, True, True, True);
  end;
end;
