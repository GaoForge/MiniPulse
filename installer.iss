#define AppVersion "1.6.3"

[Setup]
AppId={{38D2B470-89CA-4D96-A7B8-6E896B11579A}
AppName=MiniPulse
AppVersion={#AppVersion}
AppPublisher=MiniPulse contributors
DefaultDirName={autopf}\MiniPulse
DefaultGroupName=MiniPulse
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
DisableDirPage=no
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22000
OutputDir=dist
OutputBaseFilename=MiniPulse-Setup-{#AppVersion}-x64
SetupIconFile=assets\MiniPulse.ico
UninstallDisplayIcon={app}\MiniPulse.exe
AppMutex=Local\MiniPulseTraySingleton_1014
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile=LICENSE
InfoBeforeFile=INSTALL-NOTES.txt
UsePreviousTasks=yes

[Languages]
Name: "chinesesimp"; MessagesFile: "assets\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopIcon}"; GroupDescription: "{cm:ExtraIcons}"; Flags: unchecked
Name: "startup"; Description: "{cm:StartupTask}"; GroupDescription: "{cm:StartupGroup}"; Flags: unchecked
Name: "hardware"; Description: "{cm:HardwareTask}"; GroupDescription: "{cm:HardwareGroup}"; Check: IsAdminInstallMode

[Files]
Source: "MiniPulse.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "LICENSE"; DestDir: "{app}"
Source: "INSTALL-NOTES.txt"; DestDir: "{app}"
Source: "sensors\*"; DestDir: "{app}\sensors"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\MiniPulse"; Filename: "{app}\MiniPulse.exe"
Name: "{autodesktop}\MiniPulse"; Filename: "{app}\MiniPulse.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\sensors\PawnIO_setup.exe"; Description: "{cm:InstallDriver}"; Tasks: hardware; Check: NeedsPawnIO; Flags: postinstall skipifsilent
Filename: "{app}\MiniPulse.exe"; Parameters: "--enable-sensors"; Description: "{cm:LaunchApp}"; Tasks: hardware; Flags: nowait postinstall skipifsilent
Filename: "{app}\MiniPulse.exe"; Description: "{cm:LaunchApp}"; Check: WithoutHardware; Flags: nowait postinstall skipifsilent

[Code]
function NeedsPawnIO: Boolean;
var Version: String; Major, Minor, Dot: Integer;
begin
  Result := not RegQueryStringValue(HKLM64, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\PawnIO', 'DisplayVersion', Version);
  if not Result then begin
    Dot := Pos('.', Version);
    Major := StrToIntDef(Copy(Version, 1, Dot - 1), 0);
    Delete(Version, 1, Dot);
    Dot := Pos('.', Version);
    if Dot = 0 then Dot := Length(Version) + 1;
    Minor := StrToIntDef(Copy(Version, 1, Dot - 1), 0);
    Result := (Major < 2) or ((Major = 2) and (Minor < 2));
  end;
end;

function WithoutHardware: Boolean;
begin
  Result := not WizardIsTaskSelected('hardware');
end;

// Startup uses a highest-privilege scheduled task (logon trigger) instead of a
// PowerShell elevation shortcut, which heuristic AVs routinely quarantine.
procedure CreateStartupTask;
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{sys}\schtasks.exe'),
       '/Create /TN "MiniPulse" /TR "\"' + ExpandConstant('{app}') + '\MiniPulse.exe\" --enable-sensors" /SC ONLOGON /RL HIGHEST /F',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

procedure RemoveStartupTask;
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{sys}\schtasks.exe'),
       '/Delete /TN "MiniPulse" /F',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('startup') then
    CreateStartupTask;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RemoveStartupTask;
end;

[CustomMessages]
chinesesimp.DesktopIcon=创建桌面快捷方式（所有用户）
chinesesimp.ExtraIcons=快捷方式：
chinesesimp.StartupTask=登录 Windows 时启动 MiniPulse（所有用户）
chinesesimp.StartupGroup=启动选项：
chinesesimp.LaunchApp=立即启动 MiniPulse
chinesesimp.HardwareTask=启用独立硬件采温（LibreHardwareMonitor + PawnIO，需要管理员权限）
chinesesimp.HardwareGroup=硬件温度：
chinesesimp.InstallDriver=安装 PawnIO 驱动（请在随后向导中使用官方签名版本）
english.DesktopIcon=Create a desktop shortcut (all users)
english.ExtraIcons=Shortcuts:
english.StartupTask=Start MiniPulse when signing in to Windows (all users)
english.StartupGroup=Startup options:
english.LaunchApp=Launch MiniPulse now
english.HardwareTask=Enable hardware temperatures (LibreHardwareMonitor + PawnIO, administrator required)
english.HardwareGroup=Hardware temperatures:
english.InstallDriver=Install PawnIO driver (use the official signed edition in its wizard)
