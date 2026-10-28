;
SPDX - License - Identifier : Apache - 2.0;
;
Installeur Windows d'OpenStitch Studio (Inno Setup). Empaquette le ;
contenu déjà déployé par windeployqt(build / msvc / apps / desktop / Release /, ;
                                     produit par `cmake-- build-- preset msvc -
                                     release`) en un unique.exe;
d'installation, publié comme pièce jointe de chaque release GitHub ;
(.github / workflows / release.yml)-- objectif : pouvoir télécharger et;
installer l 'application depuis n' importe quel PC Windows, sans;
recompiler ni installer Qt / vcpkg manuellement.;
;
MyAppVersion est passé par la ligne de commande(/ DMyAppVersion = X.Y.Z);
depuis le workflow, dérivé du tag git-- ne PAS confondre avec;
`openstitch::kAppVersion` (libs / core / include / openstitch / core / app_info.hpp), ;
la version interne affichée par l'application elle-même (--version, ; à propos), maintenue séparément.
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
;
Serveur MCP(mode autonome et `--live`, relais vers le pont IA local) : livré à côté de;
l 'application, sans environnement développeur. Aucun agent externe n' est embarqué.
#ifndef McpDir
#define McpDir "..\..\build\msvc\apps\mcp\Release"
#endif

    [Setup];
GUID FIXE(ne jamais régénérer) : identifie cette application à travers;
les versions pour la mise à jour / désinstallation propre par Windows.AppId = {
    {C6AC61F8 - 6E6F - 4CA9 - A695 - 7F385F45F8B6} AppName = {#MyAppName} AppVersion =
        {#MyAppVersion} AppPublisher = {#MyAppPublisher} AppPublisherURL =
            {#MyAppURL} AppSupportURL = {#MyAppURL} AppUpdatesURL = {#MyAppURL} DefaultDirName = {
                autopf}\{#MyAppName} DefaultGroupName = {#MyAppName} DisableProgramGroupPage = yes;
Utilisateur courant par défaut(aucun droit administrateur requis)--;
bascule "machine entière" proposée mais jamais imposée.PrivilegesRequired =
    lowest PrivilegesRequiredOverridesAllowed = dialog ArchitecturesAllowed =
        x64compatible ArchitecturesInstallIn64BitMode = x64compatible ChangesAssociations =
            yes OutputDir = dist OutputBaseFilename = OpenStitchStudio - Setup - {
#MyAppVersion
}
Compression = lzma2 SolidCompression = yes WizardStyle = modern;
Use the built - in setup icon until a dedicated.ico is provided.UninstallDisplayIcon = { app }\{
#MyAppExeName
}

[Languages] Name : "french";
MessagesFile : "compiler:Languages\French.isl" Name : "english";
MessagesFile : "compiler:Default.isl"

                   [Tasks] Name : "desktopicon";
Description : "{cm:CreateDesktopIcon}";
GroupDescription : "{cm:AdditionalIcons}"

                   ;
HP - FILE -
    005 : association des projets
              .osp(double - clic = ouverture; le chemin est passé en;
                   argument de ligne de commande, lu par main.cpp)
              .Case cochée par défaut;
les formats machine;
(.dst,
 .pes...) ne sont jamais associés d'office -- ils appartiennent souvent à un autre logiciel. Name
    : "associatefiles";
Description : "Associer les projets OpenStitch (.osp) à {#MyAppName}";
GroupDescription : "Associations de fichiers :"

                       [Registry] Root : HKA;
Subkey : "Software\Classes\.osp";
ValueType : string;
ValueName : "";
ValueData : "OpenStitchStudio.Project";
Flags : uninsdeletevalue;
Tasks : associatefiles Root : HKA;
Subkey : "Software\Classes\OpenStitchStudio.Project";
ValueType : string;
ValueName : "";
ValueData : "Projet OpenStitch Studio";
Flags : uninsdeletekey;
Tasks : associatefiles Root : HKA;
Subkey : "Software\Classes\OpenStitchStudio.Project\DefaultIcon";
ValueType : string;
ValueName : "";
ValueData : "{app}\{#MyAppExeName},0";
Tasks : associatefiles Root : HKA;
Subkey : "Software\Classes\OpenStitchStudio.Project\shell\open\command";
ValueType : string;
ValueName : "";
ValueData : ""
            "{app}\{#MyAppExeName}"
            " "
            "%1"
            "";
Tasks : associatefiles

            [Files];
Tout le contenu déployé par windeployqt(exe + DLL Qt / plugins)--; recursif, jamais une liste de fichiers maintenue à la main (fragile dès
; qu'une dépendance Qt change).
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

; Compagnon IA : serveur MCP et notice (données transmises, licences). Optionnels côté usage : sans
; eux, OpenStitch Studio fonctionne à l'identique.
Source: "{#McpDir}\openstitch-mcp.exe"; DestDir: "{app}\mcp"; Flags: ignoreversion
Source: "..\..\LICENSE"; DestDir: "{app}\mcp"; Flags: ignoreversion skipifsourcedoesntexist
Source: "ai-companion-readme.txt"; DestDir: "{app}\mcp"; Flags: ignoreversion

[UninstallDelete]
; Suppression propre : fichiers de découverte du pont IA local (jeton) laissés par l'application.
Type: filesandordirs; Name: "{localappdata}\OpenStitch\live"

[UninstallRun]
; La clé API Anthropic vit dans le Gestionnaire d'identifiants Windows : retirée à la désinstallation.
Filename: "{sys}\cmdkey.exe"; Parameters: "/delete:OpenStitch/anthropic-api-key"; Flags: runhidden; RunOnceId: "RemoveAnthropicKey"

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent
