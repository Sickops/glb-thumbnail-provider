; Inno Setup 6.3+ - https://jrsoftware.org/isinfo.php
; Prerequis : build\GlbThumbnail.dll (scripts\build.bat). Compiler : ISCC installer\installer.iss
#ifndef MyAppVersion
  #define MyAppVersion "0.0.0-dev"
#endif

[Setup]
AppName=GLB Thumbnail Provider
AppVersion={#MyAppVersion}
AppPublisher=Sickops
AppPublisherURL=https://github.com/Sickops/glb-thumbnail-provider
DefaultDirName={commonpf64}\GlbThumbnail
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\LICENSE
OutputDir=..\dist
OutputBaseFilename=GlbThumbnail_Setup_{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=GLB Thumbnail Provider

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; "regserver" : DllRegisterServer a l'installation, DllUnregisterServer a la desinstallation.
Source: "..\build\GlbThumbnail.dll"; DestDir: "{app}"; Flags: regserver 64bit restartreplace uninsrestartdelete
