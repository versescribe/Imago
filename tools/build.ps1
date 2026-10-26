param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [switch]$RunTests,
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$version = (Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim()
if ($version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') { throw 'Invalid VERSION file.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio 2022 with Desktop development with C++ and a Windows SDK.' }
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'Visual Studio C++ tools were not found.' }
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmake = if ($cmakeCommand) { $cmakeCommand.Source } else { Join-Path $visualStudio 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' }
if (-not (Test-Path -LiteralPath $cmake)) { throw 'Install the C++ CMake tools component in Visual Studio.' }
$buildDir = Join-Path $projectRoot 'build'
$testOption = if ($RunTests -and -not $SkipTests) { '-DBUILD_TESTING=ON' } else { '-DBUILD_TESTING=OFF' }
& $cmake -S $projectRoot -B $buildDir -G 'Visual Studio 17 2022' -A x64 $testOption
if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
& $cmake --build $buildDir --config $Configuration --parallel
if ($LASTEXITCODE) { throw 'Build failed.' }
if ($RunTests -and -not $SkipTests) {
    $ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
    & $ctest --test-dir $buildDir -C $Configuration --output-on-failure
    if ($LASTEXITCODE) { throw 'Tests failed.' }
}
$dist = Join-Path $projectRoot 'dist'
New-Item -ItemType Directory -Path $dist -Force | Out-Null
$packageName = "Imago-$version-windows-x64"
$packageDirectory = Join-Path $dist $packageName
New-Item -ItemType Directory -Path $packageDirectory -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $buildDir "$Configuration\Imago.exe") -Destination (Join-Path $packageDirectory 'Imago.exe') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE'),(Join-Path $projectRoot 'README.md'),(Join-Path $projectRoot 'CHANGELOG.md') -Destination $packageDirectory -Force
try {
    Copy-Item -LiteralPath (Join-Path $packageDirectory 'Imago.exe') -Destination (Join-Path $dist 'Imago.exe') -Force
} catch {
    Write-Warning "dist/Imago.exe could not be replaced (it may be running). The new build is in $packageDirectory."
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE'),(Join-Path $projectRoot 'README.md') -Destination $dist -Force
$executableVersion = (Get-Item -LiteralPath (Join-Path $packageDirectory 'Imago.exe')).VersionInfo.ProductVersion
if ($executableVersion -ne $version) { throw "EXE version $executableVersion does not match VERSION $version." }
Compress-Archive -LiteralPath (Join-Path $packageDirectory 'Imago.exe'),(Join-Path $packageDirectory 'LICENSE'),(Join-Path $packageDirectory 'README.md'),(Join-Path $packageDirectory 'CHANGELOG.md') -DestinationPath (Join-Path $dist "$packageName.zip") -Force
$hash = (Get-FileHash -LiteralPath (Join-Path $dist "$packageName.zip") -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $dist "$packageName.zip.sha256"), "$hash  $packageName.zip`n", [Text.UTF8Encoding]::new($false))
Write-Host "Ready: $(Join-Path $packageDirectory 'Imago.exe')"
