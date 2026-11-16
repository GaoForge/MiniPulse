#define AppVersion "1.6.4"
#define AppGuid "38D2B470-89CA-4D96-A7B8-6E896B11579A"

[Setup]
AppId={{{#AppGuid}}
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
Name: "logonstartup"; Description: "{cm:StartupTask}"; GroupDescription: "{cm:StartupGroup}"; Flags: unchecked; Check: OfferLogonStartup
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
Filename: "{app}\MiniPulse.exe"; Description: "{cm:LaunchApp}"; Flags: nowait postinstall skipifsilent

[Code]
var Upgrading: Boolean;

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

function ExistingAdminInstall: Boolean;
begin
  Result := RegKeyExists(HKLM64, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{{#AppGuid}}_is1');
end;

function OfferLogonStartup: Boolean;
begin
  Result := IsAdminInstallMode and not ExistingAdminInstall;
end;

procedure ConfigureLogonStartup(const Command, ErrorMessage: String);
var ResultCode: Integer; Detail, MessageText: String;
begin
  if Exec(ExpandConstant('{app}\MiniPulse.exe'), Command, ExpandConstant('{app}'),
          SW_HIDE, ewWaitUntilTerminated, ResultCode) and (ResultCode = 0) then
    exit;
  case ResultCode of
    -2147220991: Detail := CustomMessage('StartupPathError');
    -2147220990: Detail := CustomMessage('StartupAccountError');
    -2147220989: Detail := CustomMessage('StartupConflictError');
  else
    Detail := '';
  end;
  Log(ErrorMessage + ' (' + IntToStr(ResultCode) + ') ' + Detail);
  MessageText := ErrorMessage;
  if Detail <> '' then
    MessageText := MessageText + #13#10 + Detail;
  SuppressibleMsgBox(MessageText, mbError, MB_OK, IDOK);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
    Upgrading := ExistingAdminInstall;
  if (CurStep <> ssPostInstall) or not IsAdminInstallMode then
    exit;
  if WizardIsTaskSelected('logonstartup') then
    ConfigureLogonStartup('--autostart-enable', CustomMessage('StartupConfigureError'))
  else if Upgrading then
    ConfigureLogonStartup('--autostart-maintain', CustomMessage('StartupConfigureError'));
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usUninstall) and IsAdminInstallMode and
     FileExists(ExpandConstant('{app}\MiniPulse.exe')) then
    ConfigureLogonStartup('--autostart-uninstall', CustomMessage('StartupCleanupError'));
end;

[CustomMessages]
chinesesimp.DesktopIcon=创建桌面快捷方式（所有用户）
chinesesimp.ExtraIcons=快捷方式：
chinesesimp.StartupTask=登录 Windows 时启动 MiniPulse（当前账户）
chinesesimp.StartupGroup=启动选项：
chinesesimp.StartupConfigureError=未能配置登录自启。可以在安装后通过托盘菜单检查设置。
chinesesimp.StartupCleanupError=未能清理登录自启任务。请在 Windows 任务计划程序中检查指向此安装位置的 MiniPulse 任务。
chinesesimp.StartupPathError=请选择受管理员保护的本地目录，例如默认安装目录。
chinesesimp.StartupAccountError=请使用当前登录的 Windows 管理员账户设置自启。
chinesesimp.StartupConflictError=当前账户的自启任务已被其他位置的 MiniPulse 使用，或任务已被修改。
chinesesimp.LaunchApp=立即启动 MiniPulse
chinesesimp.HardwareTask=启用独立硬件采温（LibreHardwareMonitor + PawnIO，需要管理员权限）
chinesesimp.HardwareGroup=硬件温度：
chinesesimp.InstallDriver=安装 PawnIO 驱动（请在随后向导中使用官方签名版本）
english.DesktopIcon=Create a desktop shortcut (all users)
english.ExtraIcons=Shortcuts:
english.StartupTask=Start MiniPulse when signing in to Windows (current account)
english.StartupGroup=Startup options:
english.StartupConfigureError=Could not configure startup at sign-in. Check the tray menu after installation.
english.StartupCleanupError=Could not remove the startup task. Check Task Scheduler for MiniPulse tasks pointing to this installation.
english.StartupPathError=Use a protected local folder such as the default installation folder.
english.StartupAccountError=Configure startup from the administrator account signed in to Windows.
english.StartupConflictError=Another MiniPulse location uses the startup task for this account, or the task was modified.
english.LaunchApp=Launch MiniPulse now
english.HardwareTask=Enable hardware temperatures (LibreHardwareMonitor + PawnIO, administrator required)
english.HardwareGroup=Hardware temperatures:
english.InstallDriver=Install PawnIO driver (use the official signed edition in its wizard)
