; Inno Setup 6 script for the Windows installer.
;
; Built by .github/workflows/release.yml. To build it locally after a Release build:
;   iscc /DAppVersion=0.1.0 packaging\windows\Floorplan.iss
; Relative paths are resolved from this folder.

#ifndef AppVersion
  #error Pass the version: iscc /DAppVersion=x.y.z Floorplan.iss
#endif
#ifndef BuildDir
  #define BuildDir "..\..\build\Floorplan_artefacts\Release"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

[Setup]
; Never change the AppId: Windows uses it to find an existing installation to upgrade.
AppId={{D138F8F3-7A69-4992-8696-3709C99CAEFC}
AppName=Floorplan
AppVersion={#AppVersion}
AppVerName=Floorplan {#AppVersion}
AppPublisher=Floorplan Contributors
AppPublisherURL=https://github.com/Kuzuw/Floorplan
AppSupportURL=https://github.com/Kuzuw/Floorplan/issues
AppUpdatesURL=https://github.com/Kuzuw/Floorplan/releases
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\Floorplan
DefaultGroupName=Floorplan
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename=Floorplan-{#AppVersion}-Windows-x64-Setup
Compression=lzma2/max
SolidCompression=yes
; 64-bit only. The VST3 goes to the system-wide Common Files folder, so setup needs admin rights.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
MinVersion=10.0
WizardStyle=modern
UninstallDisplayName=Floorplan {#AppVersion}
UninstallDisplayIcon={app}\Floorplan.exe

[Types]
Name: "full"; Description: "VST3 plugin and Standalone app"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (Common Files\VST3)"; Types: full custom
Name: "standalone"; Description: "Standalone app"; Types: full

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Components: standalone; Flags: unchecked

[Files]
Source: "{#BuildDir}\VST3\Floorplan.vst3\*"; DestDir: "{commoncf64}\VST3\Floorplan.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\Standalone\Floorplan.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "..\..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\..\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Floorplan"; Filename: "{app}\Floorplan.exe"; Components: standalone
Name: "{autodesktop}\Floorplan"; Filename: "{app}\Floorplan.exe"; Components: standalone; Tasks: desktopicon

[Run]
Filename: "{app}\Floorplan.exe"; Description: "{cm:LaunchProgram,Floorplan}"; Components: standalone; Flags: nowait postinstall skipifsilent
