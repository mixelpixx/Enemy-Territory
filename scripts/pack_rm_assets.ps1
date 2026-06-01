# pack_rm_assets.ps1 - Build the RM UI override pak (zz_rm_ui.pk3) from rm/ sources.
#
# ET loads etmain/*.pk3 and gives precedence to alphabetically-later names, so a
# pak named "zz_rm_ui.pk3" wins over the retail "pak0.pk3" without touching the
# user's retail data directory. We place it in fs_homepath's etmain so the retail
# install stays clean. scripts/play.bat sets fs_homepath to build/bin.
#
# Usage:  pwsh -File scripts\pack_rm_assets.ps1
#         (run from the repo root, or it resolves paths relative to itself)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$srcRoot  = Join-Path $repoRoot 'rm'                       # rm/ui/*.menu lives here
$outDir   = Join-Path $repoRoot 'build\bin\etmain'         # fs_homepath/etmain
$pakName  = 'zz_rm_ui.pk3'
$outPak   = Join-Path $outDir $pakName

if (-not (Test-Path $srcRoot)) { throw "Source dir not found: $srcRoot" }

# Stage the files so the zip's internal paths are exactly ui/<file>.menu (no 'rm/' prefix).
$stage = Join-Path $env:TEMP ('rm_pak_stage_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $stage | Out-Null
try {
    Copy-Item -Recurse -Force (Join-Path $srcRoot '*') $stage

    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    if (Test-Path $outPak) { Remove-Item -Force $outPak }

    $tmpZip = Join-Path $env:TEMP ('zz_rm_ui_' + [guid]::NewGuid().ToString('N') + '.zip')
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $tmpZip -Force
    Move-Item -Force $tmpZip $outPak

    Write-Host "Built $outPak"
    Write-Host "Contents:"
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::OpenRead($outPak).Entries |
        ForEach-Object { Write-Host "  $($_.FullName)" }
}
finally {
    Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
}
