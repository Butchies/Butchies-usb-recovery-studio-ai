#define MyAppName "Butchies USB Recovery Studio AI"
#ifndef MyAppVersion
  #define MyAppVersion "0.2.0"
#endif
#define MyAppExeName "Butchies-USB-Recovery-Studio-AI.exe"
[Setup]
AppId={{4E2B8F31-7C1D-4A9E-9B6F-3D5A2C8E1F07}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=Butchies
DefaultDirName={autopf}\Butchies USB Recovery Studio AI
DefaultGroupName={#MyAppName}
OutputDir=..\out
OutputBaseFilename=ButchiesUsbRecoveryStudioAI_Setup_{#MyAppVersion}_x64
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
SetupIconFile=..\native\app\app.ico
WizardImageFile=wizard.bmp
WizardSmallImageFile=wizard-small.bmp
UninstallDisplayIcon={app}\{#MyAppExeName}
[Languages]
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
[Tasks]
Name: "desktopicon"; Description: "Desktop-Verknüpfung erstellen"; GroupDescription: "Zusätzliche Aufgaben:"
[Files]
Source: "..\dist\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{#MyAppName} deinstallieren"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Butchies USB Recovery Studio AI starten"; Flags: nowait postinstall skipifsilent shellexec
