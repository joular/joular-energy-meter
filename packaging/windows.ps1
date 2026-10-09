# Builds Joular Energy Meter on Windows (x64) and makes, in dist\:
#   - a zip with the program, SDL2.dll, the README and the licences: unzip and run
#   - an MSI installer: Program Files, Start menu shortcut, removed from Settings > Apps
#
# Needs Alire, which installs MSYS2 and SDL2 for the build, and for the MSI the .NET SDK
# (https://dotnet.microsoft.com/download), with which WiX is installed as a dotnet tool.
# The SDL2.dll shipped is SDL's own build, which needs only Windows' DLLs.
#
# Run from PowerShell, anywhere:
#   powershell -ExecutionPolicy Bypass -File packaging\windows.ps1
# With $env:SKIP_BUILD = '1', packages bin\joularenergymeter.exe as it is.

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$Name = 'joularenergymeter'
$Version = ([regex]'(?m)^version *= *"(.*)"').Match((Get-Content -Raw alire.toml)).Groups[1].Value
$Sdl2Version = '2.32.10'
$WixVersion = '5.0.2'
$Work = Join-Path $Root 'obj\packaging'
$Dist = Join-Path $Root 'dist'
$Exe = Join-Path $Root "bin\$Name.exe"

Remove-Item -Recurse -Force $Work -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $Work, $Dist | Out-Null

if ($env:SKIP_BUILD -ne '1') {
    alr --non-interactive build
    if ($LASTEXITCODE) { throw 'alr build failed' }
}

# SDL's SDL2.dll beside the program, which won't start without it
Invoke-WebRequest -UseBasicParsing -OutFile "$Work\SDL2.zip" `
    -Uri "https://github.com/libsdl-org/SDL/releases/download/release-$Sdl2Version/SDL2-$Sdl2Version-win32-x64.zip"
Expand-Archive "$Work\SDL2.zip" -DestinationPath "$Work\SDL2"
Copy-Item "$Work\SDL2\SDL2.dll", "$Work\SDL2\README-SDL.txt" bin

# The program is a GUI app: PowerShell would neither wait for it nor capture what it prints
function Invoke-Program([string] $Arguments) {
    $Info = New-Object System.Diagnostics.ProcessStartInfo $Exe, $Arguments
    $Info.UseShellExecute = $false
    $Info.RedirectStandardOutput = $true
    $Process = [System.Diagnostics.Process]::Start($Info)
    $Output = $Process.StandardOutput.ReadToEnd()
    $Process.WaitForExit()
    if ($Process.ExitCode) { throw "$Exe $Arguments failed with exit code $($Process.ExitCode)" }
    $Output -split '\r?\n' | Where-Object { $_ }
}

# --version and --help print before any window opens. The version must be alire.toml's.
Invoke-Program '--help' | Out-Null
$Lines = Invoke-Program '--version'
$Lines
$Built = ($Lines | Select-Object -First 1).Trim().Split(' ')[-1]
if ($Built -ne $Version) { throw "alire.toml says $Version but $Exe says $Built" }

# Only Windows' own DLLs, for the program and for SDL2.dll. objdump comes with GNAT.
foreach ($File in "bin\$Name.exe", 'bin\SDL2.dll') {
    $Dlls = alr --non-interactive exec '--' objdump -p $File | Select-String 'DLL Name' |
        ForEach-Object { $_.Line.Trim() }
    "${File}: $($Dlls -join ', ')"
    if ($Dlls -match 'gnat|gnarl|libgcc|libstdc|libwinpthread|vcruntime|msvcp') {
        throw "$File needs a DLL of a toolchain"
    }
}

# zip
$Package = Join-Path $Work "$Name-$Version-windows-x64"
New-Item -ItemType Directory -Force $Package | Out-Null
Copy-Item "bin\$Name.exe", 'bin\SDL2.dll', 'bin\README-SDL.txt', 'README.md', 'LICENSE' $Package
Copy-Item 'lvgl\LICENCE.txt' "$Package\LICENCE-LVGL.txt"
Copy-Item 'src\fonts\OFL.txt' "$Package\LICENCE-Barlow.txt"
Compress-Archive -Force -Path $Package -DestinationPath "$Dist\$Name-$Version-windows-x64.zip"

# MSI, from the same files
if (-not (Get-Command wix -ErrorAction SilentlyContinue)) {
    dotnet tool install --global wix --version $WixVersion
    if ($LASTEXITCODE) { throw 'WiX could not be installed: is the .NET SDK installed?' }
    $env:PATH += ";$env:USERPROFILE\.dotnet\tools"
}
wix build -arch x64 -d "Version=$Version" -d "Files=$Package" -d "Icon=$Root\assets\icon.ico" `
    -o "$Dist\$Name-$Version-windows-x64.msi" "$PSScriptRoot\$Name.wxs"
if ($LASTEXITCODE) { throw 'wix build failed' }
Remove-Item "$Dist\*.wixpdb"

Get-ChildItem $Dist
