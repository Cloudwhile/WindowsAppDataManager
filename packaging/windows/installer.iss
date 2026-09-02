#ifndef WamVersion
  #error WamVersion must be supplied with /DWamVersion
#endif

#ifndef WamNumericVersion
  #error WamNumericVersion must be supplied with /DWamNumericVersion
#endif

#ifndef WamSourceDir
  #error WamSourceDir must be supplied with /DWamSourceDir
#endif

#ifndef WamOutputDir
  #error WamOutputDir must be supplied with /DWamOutputDir
#endif

#define WamAppName "Windows AppData Manager"
#define WamExeName "WindowsAppDataManager.exe"
#define WamPublisher "Cloudwhile"
#define WamProjectUrl "https://github.com/Cloudwhile/WindowsAppDataManager"
#define WamOutputName "WindowsAppDataManager-" + WamVersion + "-windows-x64-setup"

[Setup]
AppId={{9D80C2CC-5525-4D3F-AC1B-5204A1F86D98}
AppName={#WamAppName}
AppVersion={#WamVersion}
AppVerName={#WamAppName} {#WamVersion}
AppPublisher={#WamPublisher}
AppPublisherURL={#WamProjectUrl}
AppSupportURL={#WamProjectUrl}/issues
AppUpdatesURL={#WamProjectUrl}/releases
AppCopyright=Copyright (C) 2026 {#WamPublisher}
VersionInfoVersion={#WamNumericVersion}
VersionInfoProductName={#WamAppName}
VersionInfoProductVersion={#WamNumericVersion}
VersionInfoProductTextVersion={#WamVersion}
VersionInfoCompany={#WamPublisher}
VersionInfoDescription={#WamAppName} installer
DefaultDirName={autopf}\{#WamAppName}
DefaultGroupName={#WamAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
InfoBeforeFile={#WamSourceDir}\THIRD_PARTY_NOTICES.txt
OutputDir={#WamOutputDir}
OutputBaseFilename={#WamOutputName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
MinVersion=10.0.17763
SetupLogging=yes
CloseApplications=yes
RestartApplications=no
UninstallDisplayName={#WamAppName}
UninstallDisplayIcon={app}\{#WamExeName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
english.DesktopIcon=Create a &desktop shortcut

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#WamSourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#WamAppName}"; Filename: "{app}\{#WamExeName}"
Name: "{autodesktop}\{#WamAppName}"; Filename: "{app}\{#WamExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#WamExeName}"; Description: "{cm:LaunchProgram,{#WamAppName}}"; Flags: nowait postinstall skipifsilent runasoriginaluser
