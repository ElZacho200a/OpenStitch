; SPDX-License-Identifier: Apache-2.0
;
; Installeur Windows d'OpenStitch Studio (Inno Setup). Empaquette le
; contenu déjà déployé par windeployqt (build/msvc/apps/desktop/Release/,
; produit par `cmake --build --preset msvc-release`) en un unique .exe
; d'installation, publié comme pièce jointe de chaque release GitHub
; (.github/workflows/release.yml) -- objectif : pouvoir télécharger et
; installer l'application depuis n'importe quel PC Windows, sans
; recompiler ni installer Qt/vcpkg manuellement.
;
; MyAppVersion est passé par la ligne de commande (/DMyAppVersion=X.Y.Z)
; depuis le workflow, dérivé du tag git -- ne PAS confondre avec
; `openstitch::kAppVersion` (libs/core/include/openstitch/core/app_info.hpp),
; la version interne affichée par l'application elle-même (--version,
; à propos), maintenue séparément.
#ifndef MyAppVersion
  #define MyAppVersion "0.0.0-dev"
#endif
#define MyAppName "OpenStitch Studio"
#define MyAppExeName "openstitch.exe"
#define MyAppPublisher "OpenStitch"
#define MyAppURL "https://github.com/ElZacho200a/OpenStitch"
#ifndef SourceDir
  #define SourceDir "..\..\build\msvc\apps\desktop\Release"
#endif

[Setup]
; GUID FIXE (ne jamais régénérer) : identifie cette application à travers
; les versions pour la mise à jour/désinstallation propre par Windows.
AppId={{C6AC61F8-6E6F-4CA9-A695-7F385F45F8B6}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
; Utilisateur courant par défaut (aucun droit administrateur requis) --
; bascule "machine entière" proposée mais jamais imposée.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=dist
OutputBaseFilename=OpenStitchStudio-Setup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile={#SourceDir}\{#MyAppExeName}
UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
; Tout le contenu déployé par windeployqt (exe + DLL Qt/plugins) --
; recursif, jamais une liste de fichiers maintenue à la main (fragile dès
; qu'une dépendance Qt change).
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent
