; ============================================================
;  SystemInfoPro - Script de instalacao (Inno Setup 6+)
;  Gera SystemInfoPro_Setup.exe para distribuir na sua pagina.
;
;  Como usar:
;   1. Compile o projeto em Release / x64 no Visual Studio.
;   2. Instale o Inno Setup (https://jrsoftware.org/isdl.php).
;   3. Abra este arquivo no Inno Setup e clique em "Compile" (F9).
;   4. O instalador sai na pasta "Output\".
;
;  Ajuste os caminhos em [Files] se sua pasta de build for diferente.
; ============================================================

#define MyAppName "SystemInfoPro"
#define MyAppVersion "2.2"
#define MyAppPublisher "SystemInfoPro (Open Source)"
#define MyAppURL "https://github.com/SEU-USUARIO/SystemInfoPro"
#define MyAppExeName "SystemInfoPro.exe"

[Setup]
; AppId identifica o programa para atualizacoes/desinstalacao. NAO mude depois de publicar.
AppId={{B9E7C2A4-5D31-4F86-A0C7-1E2D3F4A5B6C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
; O app precisa de Administrador para ler SMART/sensores -> instala para todos os usuarios.
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=SystemInfoPro_Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile=LICENSE
; Icone do desinstalador (usa o icone embutido no proprio exe)
UninstallDisplayIcon={app}\{#MyAppExeName}
; Se voce gerou app.ico (menu Ajuda -> Gerar arquivo de icone), descomente:
; SetupIconFile=app.ico

[Languages]
Name: "brazilian"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "startupicon"; Description: "Iniciar o SystemInfoPro junto com o Windows"; GroupDescription: "Opcoes de inicializacao:"; Flags: unchecked

[Files]
; --- Executavel principal (ajuste o caminho conforme seu build) ---
Source: "build\x64\Release\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
; --- Documentacao ---
Source: "LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "LEIA-ME.md"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Desinstalar {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; Inicia com o Windows (se o usuario marcar a tarefa)
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; \
    ValueName: "SystemInfoPro"; ValueData: """{app}\{#MyAppExeName}"""; \
    Flags: uninsdeletevalue; Tasks: startupicon

[Run]
; Oferece abrir o programa ao final da instalacao
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; \
    Flags: nowait postinstall skipifsilent runascurrentuser
