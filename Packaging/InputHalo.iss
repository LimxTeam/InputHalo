#ifndef SourceRoot
  #define SourceRoot ".."
#endif
#ifndef BuildRoot
  #define BuildRoot SourceRoot + "\Build\windows-msvc\Release"
#endif
#define ProductVersion GetVersionNumbersString(BuildRoot + "\InputHalo.exe")
#define CompanyName "佛山市壹贰冉冉科技有限公司"

[Setup]
AppId={{6C195AF8-C4E3-4C28-B594-356895034CE7}
AppName=InputHalo
AppVersion={#ProductVersion}
AppVerName=InputHalo {#ProductVersion} Preview
AppPublisher={#CompanyName}
AppCopyright=Copyright (c) 2026 {#CompanyName}. All rights reserved.
VersionInfoVersion={#ProductVersion}
VersionInfoCompany={#CompanyName}
VersionInfoDescription=InputHalo Setup - LimxTeam
VersionInfoProductName=InputHalo
VersionInfoProductVersion={#ProductVersion}
VersionInfoProductTextVersion={#ProductVersion} Preview
DefaultDirName={localappdata}\Programs\LimxTeam\InputHalo
DefaultGroupName=LimxTeam
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0.17763
OutputDir={#SourceRoot}\Build\Installer
OutputBaseFilename=InputHalo-{#ProductVersion}-Preview-windows-x64-setup
SetupIconFile={#SourceRoot}\Assets\Icons\InputHalo.ico
UninstallDisplayIcon={app}\InputHalo.exe
UninstallDisplayName=InputHalo
LicenseFile={#SourceRoot}\LICENSE
InfoBeforeFile={#SourceRoot}\Packaging\InstallNotes.txt
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
CloseApplications=no
RestartApplications=no
SetupMutex=LimxTeam.InputHalo.Setup
ChangesAssociations=no
UsePreviousAppDir=yes
UsePreviousTasks=yes

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
chinesesimplified.DesktopShortcut=创建桌面快捷方式
english.DesktopShortcut=Create a desktop shortcut
chinesesimplified.LaunchProgram=启动 InputHalo
english.LaunchProgram=Launch InputHalo
chinesesimplified.ExitBeforeSetup=InputHalo 正在运行。请右键单击系统托盘中的 InputHalo 图标，选择“退出”后继续。关闭设置窗口只会收起到托盘。
english.ExitBeforeSetup=InputHalo is running. Right-click its system tray icon and choose Exit, then continue. Closing the settings window only hides it to the tray.

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopShortcut}"; Flags: unchecked

[Files]
; Explicit allowlist: never ship test executables, debug libraries, logs or user settings.
Source: "{#BuildRoot}\InputHalo.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildRoot}\Shaders\Renderer2D.vert.spv"; DestDir: "{app}\Shaders"; Flags: ignoreversion
Source: "{#BuildRoot}\Shaders\Renderer2D.frag.spv"; DestDir: "{app}\Shaders"; Flags: ignoreversion
Source: "{#BuildRoot}\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildRoot}\PRIVACY.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildRoot}\THIRD_PARTY_NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildRoot}\Licenses\*"; DestDir: "{app}\Licenses"; Flags: ignoreversion
Source: "{#SourceRoot}\Packaging\InstallNotes.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\InputHalo"; Filename: "{app}\InputHalo.exe"; WorkingDir: "{app}"; AppUserModelID: "LimxTeam.InputHalo"
Name: "{autodesktop}\InputHalo"; Filename: "{app}\InputHalo.exe"; WorkingDir: "{app}"; AppUserModelID: "LimxTeam.InputHalo"; Tasks: desktopicon

[Run]
Filename: "{app}\InputHalo.exe"; Description: "{cm:LaunchProgram}"; Flags: nowait postinstall skipifsilent

[Code]
function AppIsRunning(): Boolean;
begin
  Result := FindWindowByClassName('InputHalo.Settings') <> 0;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if AppIsRunning() then Result := CustomMessage('ExitBeforeSetup');
end;

function InitializeUninstall(): Boolean;
begin
  Result := not AppIsRunning();
  if not Result then MsgBox(CustomMessage('ExitBeforeSetup'), mbInformation, MB_OK);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var Existing: String;
begin
  { Keep an existing startup opt-in pointing at the installed copy. Never enable it by default. }
  if CurStep = ssPostInstall then
    if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'InputHalo', Existing) then
      if Existing <> '' then
        RegWriteStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'InputHalo', '"' + ExpandConstant('{app}\InputHalo.exe') + '"');
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var Existing, Installed: String;
begin
  if CurUninstallStep = usUninstall then begin
    Installed := ExpandConstant('{app}\InputHalo.exe');
    if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'InputHalo', Existing) then
      if (CompareText(Existing, Installed) = 0) or (CompareText(Existing, '"' + Installed + '"') = 0) then
        RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'InputHalo');
  end;
  { User settings, profiles and logs in LocalAppData\InputHalo are intentionally preserved. }
end;
