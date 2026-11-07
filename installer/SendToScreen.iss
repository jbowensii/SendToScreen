#define AppName "SendToScreen"
#define AppVersion "2.0.3"
#define AppExe "SendToScreen.exe"

[Setup]
AppId={{B7E5C2F4-6D1A-4C0B-9E3F-2A8D5F7C1E90}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=John B Owens II
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\release
OutputBaseFilename=SendToScreen_Setup_v{#AppVersion}
SetupIconFile=..\icons\icon.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Tasks]
Name: "startup"; Description: "Start {#AppName} when I sign in"; GroupDescription: "Startup:"

[Files]
Source: "..\build\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"

; Older installers used a Startup shortcut, which Windows will not launch for a requireAdministrator exe.
[InstallDelete]
Type: files; Name: "{userstartup}\{#AppName}.lnk"

[Run]
; A logon task at highest privileges starts elevated with no UAC prompt. Register-ScheduledTask, not schtasks: schtasks tasks stop after 72 hours and skip battery power.
; ponytail: {username} is the account running Setup; add a user picker if another admin ever installs for you.
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -Command ""$u='{username}'; Register-ScheduledTask -TaskName '{#AppName}' -Force -Action (New-ScheduledTaskAction -Execute '{app}\{#AppExe}') -Trigger (New-ScheduledTaskTrigger -AtLogOn -User $u) -Principal (New-ScheduledTaskPrincipal -UserId $u -LogonType Interactive -RunLevel Highest) -Settings (New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit ([TimeSpan]::Zero))"""; Flags: runhidden waituntilterminated; Tasks: startup; StatusMsg: "Setting up sign-in startup..."
Filename: "schtasks.exe"; Parameters: "/Delete /F /TN ""{#AppName}"""; Flags: runhidden waituntilterminated; Tasks: not startup
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName} now"; Flags: nowait postinstall skipifsilent shellexec

[UninstallRun]
Filename: "taskkill.exe"; Parameters: "/F /IM {#AppExe}"; Flags: runhidden; RunOnceId: "KillApp"
Filename: "schtasks.exe"; Parameters: "/Delete /F /TN ""{#AppName}"""; Flags: runhidden; RunOnceId: "RemoveTask"

[UninstallDelete]
Type: files; Name: "{localappdata}\SendToScreen.log"

[Code]
// The tray app holds no files open but must not keep running across an upgrade or uninstall.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  R: Integer;
begin
  Exec('taskkill.exe', '/F /IM {#AppExe}', '', SW_HIDE, ewWaitUntilTerminated, R);
  Result := '';
end;
