# Powtarzalne budowanie oraz testy; skrypt nie edytuje plików źródłowych.
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Nie znaleziono vswhere.' }
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'Nie znaleziono Visual Studio z narzędziami C++.' }
$msbuild = Join-Path $installation 'MSBuild\Current\Bin\MSBuild.exe'
$solution = Join-Path $PSScriptRoot 'MandelbrotBenchmark.sln'
$toolsRoot = Join-Path $installation 'VC\Tools\MSVC'
$toolset = Get-ChildItem -LiteralPath $toolsRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1
$dumpbin = Join-Path $toolset.FullName 'bin\Hostx64\x64\dumpbin.exe'

foreach ($configuration in @('Debug', 'Release')) {
    Write-Output "BUDOWANIE: $configuration x64"
    & $msbuild $solution '/m' '/nologo' '/v:minimal' '/t:Build' "/p:Configuration=$configuration" '/p:Platform=x64'
    if ($LASTEXITCODE -ne 0) { throw "Niepowodzenie budowania $configuration." }
    $binaryDirectory = Join-Path $PSScriptRoot "bin\x64\$configuration"
    $dll = Join-Path $binaryDirectory 'MandelbrotAsm.dll'
    $application = Join-Path $binaryDirectory 'MandelbrotApp.exe'
    if (-not (Test-Path -LiteralPath $application) -or -not (Test-Path -LiteralPath $dll)) {
        throw 'Brak oczekiwanych plików EXE lub DLL.'
    }
    Write-Output "EKSPORTY DLL: $configuration"
    & $dumpbin '/nologo' '/exports' $dll
    if ($LASTEXITCODE -ne 0) { throw 'Niepowodzenie odczytu eksportów DLL.' }
    Write-Output "TESTY OBLICZENIOWE: $configuration"
    $testArguments = @('--self-test')
    if ($configuration -eq 'Release') { $testArguments += '--benchmark' }
    & $application @testArguments | ForEach-Object { Write-Output $_ }
    if ($LASTEXITCODE -ne 0) { throw "Niepowodzenie testów obliczeniowych $configuration." }
    Write-Output "TESTY GUI: $configuration"
    & $application '--gui-smoke' | ForEach-Object { Write-Output $_ }
    if ($LASTEXITCODE -ne 0) { throw "Niepowodzenie testów GUI $configuration." }
}
Write-Output 'PASS: Debug, Release, DLL, zgodność, GUI i benchmark.'
