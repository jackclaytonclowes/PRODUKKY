; Inno Setup 6 script for BOTH plugins on Windows. make-package.ps1 runs it:
;   iscc /DAppVersion=0.2.0 /DStage=<staged folder> /DOutDir=<dist> installer.iss
;
; The VST3s go to the system VST3 folder (C:\Program Files\Common Files\VST3),
; the one folder every Windows host scans: Ableton Live 12 with "Use VST3 Plug-In
; System Folders" on, Reaper, Cubase, Bitwig, FL Studio. The standalone apps go
; to Program Files with a Start menu entry each. Unsigned, so SmartScreen warns.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef Stage
  #define Stage "..\..\dist\windows-stage"
#endif
#ifndef OutDir
  #define OutDir "..\..\dist"
#endif

[Setup]
; the id ties upgrades and the uninstaller together: never change it
AppId={{2FCE44DF-9DDB-4960-B4A0-EB4A190747E7}
AppName=FRACTURE and CRATE
AppVersion={#AppVersion}
AppVerName=FRACTURE and CRATE {#AppVersion}
AppPublisher=Fracture
DefaultDirName={autopf}\Fracture and Crate
DefaultGroupName=Fracture and Crate
DisableProgramGroupPage=yes
InfoBeforeFile={#Stage}\READ ME FIRST.txt
OutputDir={#OutDir}
OutputBaseFilename=Fracture-and-Crate-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
; both plugins are 64-bit; Live 12 is 64-bit only
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName=FRACTURE and CRATE

[Components]
Name: "fracture"; Description: "FRACTURE (multi-band distortion), VST3"; Types: full compact custom
Name: "crate"; Description: "CRATE (twelve-bit drum processor), VST3"; Types: full compact custom
Name: "apps"; Description: "Standalone apps"; Types: full

[Files]
Source: "{#Stage}\VST3\FRACTURE.vst3\*"; DestDir: "{commoncf64}\VST3\FRACTURE.vst3"; Components: fracture; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Stage}\VST3\CRATE.vst3\*"; DestDir: "{commoncf64}\VST3\CRATE.vst3"; Components: crate; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Stage}\Standalone apps\FRACTURE.exe"; DestDir: "{app}"; Components: apps and fracture; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#Stage}\Standalone apps\CRATE.exe"; DestDir: "{app}"; Components: apps and crate; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#Stage}\READ ME FIRST.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\FRACTURE"; Filename: "{app}\FRACTURE.exe"; Components: apps and fracture
Name: "{group}\CRATE"; Filename: "{app}\CRATE.exe"; Components: apps and crate
Name: "{group}\Read me"; Filename: "{app}\READ ME FIRST.txt"
Name: "{group}\Uninstall FRACTURE and CRATE"; Filename: "{uninstallexe}"

[UninstallDelete]
; the bundles are folders: take them whole, but never the presets in Documents
Type: filesandordirs; Name: "{commoncf64}\VST3\FRACTURE.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\CRATE.vst3"
