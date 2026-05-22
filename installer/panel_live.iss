#define MyAppName "Panel Live"
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef SetupBaseName
  #define SetupBaseName "panel-live-" + AppVersion + "-win-x64"
#endif
#ifndef VersionInfoVersionValue
  #define VersionInfoVersionValue AppVersion + ".0"
#endif
#define MyAppVersion AppVersion
#define MyAppPublisher "Nisoje Studio"
#define MyAppExeName "NisojeStudio.exe"
#define VCREDIST_FILE "vc_redist.x64.exe"
#define WEBVIEW2_FILE "MicrosoftEdgeWebView2RuntimeInstallerX64.exe"
#define BRIDGE_CHECK_FILE "bridge_env_check.py"

#ifndef PackageRoot
  #define PackageRoot "..\\dist\\NisojeStudio"
#endif
#ifndef DependencyRoot
  #define DependencyRoot "..\\build\\installer_cache"
#endif
#ifndef OutputRoot
  #define OutputRoot "..\\dist\\installer"
#endif
#ifndef SetupIcon
  #define SetupIcon "..\\assets\\branding\\panel_live.ico"
#endif

[Setup]
AppId={{5F84E58E-6199-4FB0-8D88-9A9B7785A3F0}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\Panel Live
DefaultGroupName={#MyAppName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2/ultra64
SolidCompression=yes
OutputDir={#OutputRoot}
OutputBaseFilename={#SetupBaseName}
WizardStyle=modern
; WizardBackColor=$09111d
SetupIconFile={#SetupIcon}
UninstallDisplayIcon={app}\{#MyAppExeName}
PrivilegesRequired=admin
DisableProgramGroupPage=yes
CloseApplications=yes
SetupLogging=yes
VersionInfoVersion={#VersionInfoVersionValue}

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked

[Files]
Source: "{#PackageRoot}\NisojeStudio.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageRoot}\WebView2Loader.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageRoot}\panel_config.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageRoot}\tools\*"; DestDir: "{app}\tools"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#DependencyRoot}\{#VCREDIST_FILE}"; Flags: dontcopy
Source: "{#DependencyRoot}\{#WEBVIEW2_FILE}"; Flags: dontcopy

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Iniciar {#MyAppName}"; Flags: nowait postinstall skipifsilent

[Code]
var
  NeedsVCRedist: Boolean;
  NeedsWebView2: Boolean;
  RestartRequiredByPrereq: Boolean;

function IsNonEmptyVersion(const Value: string): Boolean;
begin
  Result := (Trim(Value) <> '') and (Trim(Value) <> '0.0.0.0');
end;

function IsVCRedistInstalled(): Boolean;
var
  Installed: Cardinal;
begin
  Result := RegQueryDWordValue(
    HKLM64,
    'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X64',
    'Installed',
    Installed) and (Installed = 1);
end;

function IsWebView2Installed(): Boolean;
var
  VersionValue: string;
begin
  Result :=
    (RegQueryStringValue(
      HKLM64,
      'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}',
      'pv',
      VersionValue) and IsNonEmptyVersion(VersionValue))
    or
    (RegQueryStringValue(
      HKCU,
      'Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}',
      'pv',
      VersionValue) and IsNonEmptyVersion(VersionValue));
end;

procedure SetInstallStatusText(const Message: string);
begin
  WizardForm.StatusLabel.Caption := Message;
  Log(Message);
end;

procedure RunPrerequisite(
  const TempFileName: string;
  const Parameters: string;
  const FriendlyName: string);
var
  ResultCode: Integer;
begin
  ExtractTemporaryFile(TempFileName);
  SetInstallStatusText('Instalando ' + FriendlyName + '...');

  if not Exec(
    ExpandConstant('{tmp}\' + TempFileName),
    Parameters,
    '',
    SW_SHOWNORMAL,
    ewWaitUntilTerminated,
    ResultCode) then begin
    RaiseException('No se pudo iniciar ' + FriendlyName + '.');
  end;

  Log(FriendlyName + ' finalizo con codigo ' + IntToStr(ResultCode));
  if ResultCode = 3010 then begin
    RestartRequiredByPrereq := True;
    Exit;
  end;

  if ResultCode <> 0 then begin
    RaiseException(FriendlyName + ' devolvio el codigo ' + IntToStr(ResultCode) + '.');
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  NeedsVCRedist := not IsVCRedistInstalled();
  NeedsWebView2 := not IsWebView2Installed();
  RestartRequiredByPrereq := False;
  Result := '';
end;

function ValidateInstalledPrerequisites(): String;
begin
  Result := '';

  if not IsVCRedistInstalled() then begin
    Result := Result + 'Microsoft Visual C++ Redistributable x64 no quedo disponible despues de la instalacion.' + #13#10;
  end;

  if not IsWebView2Installed() then begin
    if RestartRequiredByPrereq then begin
      Result := Result + 'Microsoft Edge WebView2 Runtime solicita reinicio para terminar de quedar listo.' + #13#10;
    end else begin
      Result := Result + 'Microsoft Edge WebView2 Runtime no quedo disponible despues de la instalacion.' + #13#10;
    end;
  end;
end;

function ValidateBundledTikTokBridge(): String;
var
  PythonExe: string;
  CheckScript: string;
  ReportPath: string;
  ReportTextAnsi: AnsiString;
  ReportText: string;
  ReportLogText: string;
  Parameters: string;
  ResultCode: Integer;
begin
  Result := '';
  PythonExe := ExpandConstant('{app}\tools\bridge_py\python_runtime\python.exe');
  CheckScript := ExpandConstant('{app}\tools\bridge_py\{#BRIDGE_CHECK_FILE}');
  ReportPath := ExpandConstant('{tmp}\nisoje-bridge-check.txt');

  if not FileExists(PythonExe) then begin
    Result := 'No se instalo python_runtime\python.exe del bridge TikTok.';
    Exit;
  end;

  if not FileExists(CheckScript) then begin
    Result := 'No se instalo {#BRIDGE_CHECK_FILE} dentro del bridge TikTok.';
    Exit;
  end;

  if FileExists(ReportPath) then begin
    DeleteFile(ReportPath);
  end;

  Parameters :=
    '"' + CheckScript + '"' +
    ' --format text' +
    ' --expect-auth-required' +
    ' --config-path "' + ExpandConstant('{app}\panel_config.json') + '"' +
    ' --report-path "' + ReportPath + '"';

  SetInstallStatusText('Verificando runtime TikTok...');
  if not Exec(
    PythonExe,
    Parameters,
    ExpandConstant('{app}\tools\bridge_py'),
    SW_HIDE,
    ewWaitUntilTerminated,
    ResultCode) then begin
    Result := 'No se pudo iniciar la verificacion del bridge TikTok.';
    Exit;
  end;

  if FileExists(ReportPath) then begin
    LoadStringFromFile(ReportPath, ReportTextAnsi);
    ReportText := Trim(String(ReportTextAnsi));
  end else begin
    ReportText := '';
  end;

  if ResultCode <> 0 then begin
    if ReportText <> '' then begin
      Result := ReportText;
    end else begin
      Result := 'El runtime TikTok no quedo instalado correctamente.';
    end;
    Exit;
  end;

  if ReportText <> '' then begin
    ReportLogText := ReportText;
    StringChangeEx(ReportLogText, #13#10, ' | ', True);
    Log('TikTok bridge validation: ' + ReportLogText);
  end else begin
    Log('TikTok bridge validation ok.');
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  PrereqValidation: string;
  BridgeValidation: string;
begin
  if CurStep = ssInstall then begin
    if NeedsVCRedist then begin
      RunPrerequisite(
        '{#VCREDIST_FILE}',
        '/install /quiet /norestart',
        'Microsoft Visual C++ Redistributable x64');
    end else begin
      Log('Microsoft Visual C++ Redistributable x64 ya estaba instalado.');
    end;

    if NeedsWebView2 then begin
      RunPrerequisite(
        '{#WEBVIEW2_FILE}',
        '/silent /install',
        'Microsoft Edge WebView2 Runtime');
    end else begin
      Log('Microsoft Edge WebView2 Runtime ya estaba instalado.');
    end;

    if RestartRequiredByPrereq then begin
      WizardForm.StatusLabel.Caption :=
        'Al menos un prerequisito solicita reinicio. La instalacion principal continuara.';
    end;
    Exit;
  end;

  if CurStep = ssPostInstall then begin
    PrereqValidation := Trim(ValidateInstalledPrerequisites());
    if PrereqValidation <> '' then begin
      MsgBox(
        'La instalacion termino, pero faltan prerrequisitos por confirmar:' + #13#10 + #13#10 + PrereqValidation,
        mbCriticalError,
        MB_OK);
      if Pos('reinicio', Lowercase(PrereqValidation)) = 0 then begin
        RaiseException('La verificacion de prerrequisitos fallo.');
      end;
    end;

    BridgeValidation := Trim(ValidateBundledTikTokBridge());
    if BridgeValidation <> '' then begin
      MsgBox(
        'TikTok no quedo listo despues de instalar Panel Live:' + #13#10 + #13#10 + BridgeValidation,
        mbCriticalError,
        MB_OK);
      RaiseException('La verificacion del bridge TikTok fallo.');
    end;
  end;
end;
