; ASTRA — end-to-end Windows installer (Inno Setup).
;
; Bundles the Qt desktop frontend, the C++ backend host, and the frozen Python
; MT5 bridge into one setup that installs to Program Files. The user
; double-clicks ASTRA-Setup.exe; there is no CMD window and no separate Python
; install.
;
; Build (from the repo root, after the three build steps in the CI workflow):
;     iscc packaging\windows\astra-setup.iss
; Output: packaging\windows\Output\ASTRA-Setup.exe
;
; Expects this staging layout (relative to the repo root):
;     frontend/qt/build/bin/Release/   astra_desktop.exe + Qt DLLs (windeployqt)
;     build/Release/                   aura_backend_host.exe
;     dist/                            bridge.exe
;     frontend/qt/src/resources/icons/astra.ico

#define AppName "ASTRA"
#define AppVersion "1.0.0"
#define AppPublisher "AURA"
#define AppExeName "astra_desktop.exe"
#define BackendExeName "aura_backend_host.exe"

[Setup]
AppId={{7C4A9E12-3B6D-4F1A-9E2B-A5C7D8E9F012}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#AppExeName}
SetupIconFile=..\..\frontend\qt\src\resources\icons\astra.ico
OutputDir=Output
OutputBaseFilename=ASTRA-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Qt frontend + deployed Qt runtime (windeployqt output).
Source: "..\..\frontend\qt\build\bin\Release\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; C++ backend host.
Source: "..\..\build\Release\aura_backend_host.exe"; DestDir: "{app}"; Flags: ignoreversion
; Frozen Python bridge — the installer's market-data service. Placed where the
; backend expects it: <app>/resources/bridge/mt5_python/bridge.exe.
Source: "..\..\dist\bridge.exe"; DestDir: "{app}\resources\bridge\mt5_python"; Flags: ignoreversion
; Bridge sources travel alongside for transparency/debuggability.
Source: "..\..\bridge\mt5_python\*.py"; DestDir: "{app}\resources\bridge\mt5_python"; Flags: ignoreversion
Source: "..\..\bridge\mt5_python\requirements.txt"; DestDir: "{app}\resources\bridge\mt5_python"; Flags: ignoreversion
Source: "..\..\packaging\bundle_manifest.json"; DestDir: "{app}"; Flags: ignoreversion

[Dirs]
Name: "{app}\config"
Name: "{app}\data"
Name: "{app}\logs"

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}\logs"
