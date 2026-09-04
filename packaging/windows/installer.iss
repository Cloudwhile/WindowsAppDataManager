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

#ifndef WamSetupIconFile
  #error WamSetupIconFile must be supplied with /DWamSetupIconFile
#endif

#ifndef WamWizardImageFile
  #error WamWizardImageFile must be supplied with /DWamWizardImageFile
#endif

#ifndef WamUninstallIconFile
  #error WamUninstallIconFile must be supplied with /DWamUninstallIconFile
#endif

#ifndef WamThirdPartyLicenseFile
  #error WamThirdPartyLicenseFile must be supplied with /DWamThirdPartyLicenseFile
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
LicenseFile={#WamSourceDir}\LICENSE
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
UninstallDisplayIcon={app}\WindowsAppDataManagerUninstall.ico
SetupIconFile={#WamSetupIconFile}
WizardSmallImageFile={#WamWizardImageFile}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
english.DesktopIcon=Create a &desktop shortcut

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#WamThirdPartyLicenseFile}"; DestDir: "{tmp}"; DestName: "THIRD_PARTY_LICENSES.txt"; Flags: dontcopy noencryption
Source: "{#WamSourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#WamUninstallIconFile}"; DestDir: "{app}"; DestName: "WindowsAppDataManagerUninstall.ico"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#WamAppName}"; Filename: "{app}\{#WamExeName}"
Name: "{autodesktop}\{#WamAppName}"; Filename: "{app}\{#WamExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#WamExeName}"; Description: "{cm:LaunchProgram,{#WamAppName}}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[Code]
var
  ThirdPartyPage: TWizardPage;
  ThirdPartyMemo: TNewMemo;
  ThirdPartyAgreement: TNewCheckBox;

procedure InitializeWizard;
begin
  ExtractTemporaryFile('THIRD_PARTY_LICENSES.txt');
  ThirdPartyPage := CreateCustomPage(wpLicense,
    'Third-party license notices',
    'Please review the third-party license notices before continuing.');

  ThirdPartyMemo := TNewMemo.Create(ThirdPartyPage);
  ThirdPartyMemo.Parent := ThirdPartyPage.Surface;
  ThirdPartyMemo.Left := 0;
  ThirdPartyMemo.Top := 0;
  ThirdPartyMemo.Width := ThirdPartyPage.SurfaceWidth;
  ThirdPartyMemo.Height := ThirdPartyPage.SurfaceHeight - ScaleY(28);
  ThirdPartyMemo.ReadOnly := True;
  ThirdPartyMemo.ScrollBars := ssVertical;
  ThirdPartyMemo.Lines.LoadFromFile(ExpandConstant('{tmp}\THIRD_PARTY_LICENSES.txt'));

  ThirdPartyAgreement := TNewCheckBox.Create(ThirdPartyPage);
  ThirdPartyAgreement.Parent := ThirdPartyPage.Surface;
  ThirdPartyAgreement.Left := 0;
  ThirdPartyAgreement.Top := ThirdPartyMemo.Top + ThirdPartyMemo.Height + ScaleY(8);
  ThirdPartyAgreement.Width := ThirdPartyPage.SurfaceWidth;
  ThirdPartyAgreement.Caption :=
    'I have read and agree to the third-party license notices.';
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = ThirdPartyPage.ID) and not ThirdPartyAgreement.Checked then
  begin
    MsgBox('You must accept the third-party license notices to continue.',
      mbError, MB_OK);
    Result := False;
  end;
end;
