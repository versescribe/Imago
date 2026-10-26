param(
    [Parameter(Mandatory)][string]$Tag,
    [Parameter(Mandatory)][string]$OutputPath
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$version = (Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim()
if ($Tag -cne "v$version") { throw "Tag $Tag must match VERSION v$version." }
if ($version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') { throw 'Invalid stable version.' }
$changelog = Get-Content -LiteralPath (Join-Path $projectRoot 'CHANGELOG.md') -Raw
$section = [regex]::Match($changelog, '(?ms)^## ' + [regex]::Escape($version) + ' [^\r\n]*\r?\n(.*?)(?=^## |\z)')
if (-not $section.Success -or -not $section.Groups[1].Value.Trim()) { throw "Missing CHANGELOG section for $version." }
$notes = $section.Groups[1].Value.Trim() + "`n`n下载 Windows x64 ZIP，解压运行 Imago.exe。更新前先从托盘退出旧版。`n`n附带 SHA-256 校验文件。此发行包尚未使用 Authenticode 签名。`n"
[IO.File]::WriteAllText($OutputPath, $notes, [Text.UTF8Encoding]::new($false))
