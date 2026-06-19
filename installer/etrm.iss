; =============================================================================
;  ET-RM installer  (Inno Setup 6)
;
;  Produces a single, self-contained, no-patching Windows installer: the player
;  runs setup.exe and plays -- the Release engine, the RM content paks, the
;  bundled retail Wolfenstein: ET data, and the app-local VC++ runtime are all
;  included. No separate downloads, no admin (per-user install).
;
;  Build it with:
;     ISCC.exe installer\etrm.iss
;  (the committed file here is the SOURCE; setup.exe lands in dist\, gitignored)
;
;  Prerequisites at compile time (local build outputs + your retail data):
;    - a Release build in build-rel\bin\   (cmake -DCMAKE_BUILD_TYPE=Release)
;    - retail ET paks present at SrcRetail  (pak0-2.pk3, mp_bin.pk3)
;
;  Runtime: etrm.exe imports VCRUNTIME140{,_1}.dll; the UCRT (api-ms-win-crt-*)
;  ships with Windows 10/11. We deploy the VC runtime app-local (next to the exe)
;  so no VC++ redistributable install step is required.
; =============================================================================

#define AppName     "Wolfenstein: Enemy Territory RM"
#define AppShort    "ET-RM"
#define AppVer      "0.1.0"
#define AppPublisher "ET-RM project"

#define SrcBin      "C:\repo\et-rm\build-rel\bin"
#define SrcEtmain   "C:\repo\et-rm\build-rel\bin\etmain"
#define SrcRetail   "C:\repo\enemy-territory-RM\etmain"
; x64 VC++ runtime redist DLLs (app-local source; NOT the WOW64-redirected System32)
#define SrcCRT      "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT"

[Setup]
AppName={#AppName}
AppVersion={#AppVer}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppShort}
DefaultGroupName={#AppShort}
UninstallDisplayIcon={app}\etrm.exe
OutputDir=C:\repo\et-rm\dist
OutputBaseFilename=etrm-setup-{#AppVer}
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
WizardStyle=modern
DisableProgramGroupPage=yes

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Files]
; --- Release engine ---
Source: "{#SrcBin}\etrm.exe";                 DestDir: "{app}"
Source: "{#SrcBin}\etrmded.exe";              DestDir: "{app}"
Source: "{#SrcBin}\etrm_renderer2.dll";       DestDir: "{app}"
Source: "{#SrcBin}\SDL2.dll";                 DestDir: "{app}"
; game modules: qagame (server) + the client cgame/ui the engine loads loose at
; startup (rm_bin.pk3 carries them too, but only for the pure-server match check)
Source: "{#SrcBin}\qagame_mp_x86_64.dll";     DestDir: "{app}"
Source: "{#SrcBin}\cgame_mp_x86_64.dll";      DestDir: "{app}"
Source: "{#SrcBin}\ui_mp_x86_64.dll";         DestDir: "{app}"
; --- VC++ runtime (app-local; UCRT is part of Win10/11) ---
Source: "{#SrcCRT}\vcruntime140.dll";         DestDir: "{app}"
Source: "{#SrcCRT}\vcruntime140_1.dll";       DestDir: "{app}"
Source: "{#SrcCRT}\msvcp140.dll";             DestDir: "{app}"
; --- RM content ---
Source: "{#SrcEtmain}\rm_bin.pk3";            DestDir: "{app}\etmain"
Source: "{#SrcEtmain}\zz_rm_ui.pk3";          DestDir: "{app}\etmain"
Source: "{#SrcEtmain}\rm_showcase.pk3";       DestDir: "{app}\etmain"
; --- retail Wolfenstein: ET data (free download; bundled for offline play) ---
Source: "{#SrcRetail}\pak0.pk3";              DestDir: "{app}\etmain"
Source: "{#SrcRetail}\pak1.pk3";              DestDir: "{app}\etmain"
Source: "{#SrcRetail}\pak2.pk3";              DestDir: "{app}\etmain"
Source: "{#SrcRetail}\mp_bin.pk3";            DestDir: "{app}\etmain"

[Icons]
Name: "{group}\{#AppShort}";                       Filename: "{app}\etrm.exe"
Name: "{group}\{#AppShort} (modern renderer)";     Filename: "{app}\etrm.exe"; Parameters: "+set cl_renderer gl2 +set r_normalMapping 1 +set cg_shadows 5 +set r_softShadows 4"
Name: "{group}\Uninstall {#AppShort}";             Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppShort}";                 Filename: "{app}\etrm.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\etrm.exe"; Description: "Launch {#AppShort} now"; Flags: nowait postinstall skipifsilent
