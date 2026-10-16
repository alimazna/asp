; Inno Setup script for the ASTRA Sender.
;
; Builds a normal Windows setup program (AstraSenderSetup.exe) that
;   * requests elevation (UAC), so setup writes to protected locations;
;   * installs the single-file app under Program Files;
;   * keeps the config + SQLite ledger under %PROGRAMDATA%\AstraSender, which
;     the installer grants write access to, so every user on the machine can
;     run the sender without seeing "access denied";
;   * creates Start Menu and optional desktop shortcuts and an uninstaller.
;
; Provisioning the token: if a file named ``config.json`` sits next to this
; setup program, setup copies it into %PROGRAMDATA%\AstraSender\config.json
; (only when none exists). This lets a pre-configured setup be produced
; without embedding the credential in the setup binary itself.

#define AppName "ASTRA Sender"
#define AppVersion "0.4.6"
#define AppPublisher "ASTRA"
#define AppExeName "AstraSender.exe"

[Setup]
AppId={{7E4C1D2A-9B31-4F6E-8A2D-5C0F1B7A3D91}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\AstraSender
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
; Elevation: "requests permissions from the system".
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=dist
OutputBaseFilename=AstraSenderSetup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; \
    GroupDescription: "Additional shortcuts:"; Flags: checkedonce

[Files]
Source: "..\dist\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion
; Config provisioning. Two modes:
;   default          -> read config.json from beside the setup program at run
;                       time (external), so the setup binary holds no secret.
;   /DEmbedConfig=1  -> compile a config.json that sits in the build directory
;                       directly into the setup, producing a single
;                       self-contained installer. Build such a setup privately:
;                       it contains the ingestion credential.
#ifdef EmbedConfig
Source: "config.json"; DestDir: "{commonappdata}\AstraSender"; \
    DestName: "config.json"; Flags: onlyifdoesntexist uninsneveruninstall
#else
Source: "{src}\config.json"; DestDir: "{commonappdata}\AstraSender"; \
    DestName: "config.json"; \
    Flags: external skipifsourcedoesntexist onlyifdoesntexist uninsneveruninstall
#endif

[Dirs]
; Let any user run the sender and write its ledger/state here.
Name: "{commonappdata}\AstraSender"; Permissions: users-modify

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; \
    Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; \
    Flags: nowait postinstall skipifsilent
