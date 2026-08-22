#Requires -Version 5.1
#Requires -RunAsAdministrator

[CmdletBinding()]
param(
    [string]$UnrealEngineRoot = "",
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [switch]$SkipTwitchAuthorization,
    [switch]$ResetBridgeEnvironment
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ProjectRoot = $PSScriptRoot
$ProjectFile = Join-Path $ProjectRoot "Demo.uproject"
$BridgeRoot = Join-Path $ProjectRoot "Tools\TwitchBridge"
$BridgeEnvironmentExample = Join-Path $BridgeRoot ".env.example"
$BridgeEnvironment = Join-Path $BridgeRoot ".env"
$MinimumNodeVersion = [version]"20.6.0"
$RestartRequired = $false

$VisualStudioComponents = @(
    "Microsoft.VisualStudio.Workload.VCTools",
    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
    "Microsoft.VisualStudio.Component.VC.14.44.17.14.x86.x64",
    "Microsoft.VisualStudio.Component.Windows11SDK.22621",
    "Microsoft.Net.Component.4.8.SDK",
    "Microsoft.Net.Component.4.8.TargetingPack",
    "Microsoft.Net.ComponentGroup.4.8.DeveloperTools"
)

function Write-Step {
    param([string]$Message)

    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Message)

    Write-Host "[OK] $Message" -ForegroundColor Green
}

function Refresh-ProcessPath {
    $MachinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
    $UserPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $env:Path = "$MachinePath;$UserPath"
}

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$FailureMessage = "The command failed."
    )

    & $FilePath @Arguments
    $ExitCode = $LASTEXITCODE

    if ($null -ne $ExitCode -and $ExitCode -ne 0) {
        throw "$FailureMessage Exit code: $ExitCode"
    }
}

function Get-WinGetPath {
    $Command = Get-Command "winget.exe" -ErrorAction SilentlyContinue
    if ($null -eq $Command) {
        throw "Windows Package Manager (winget) was not found. Install or update 'App Installer' from Microsoft Store, then run setup.ps1 again."
    }

    return $Command.Source
}

function Install-WinGetPackage {
    param(
        [Parameter(Mandatory = $true)][string]$PackageId,
        [Parameter(Mandatory = $true)][string]$DisplayName,
        [switch]$Upgrade
    )

    $WinGet = Get-WinGetPath
    $Operation = "install"
    if ($Upgrade) {
        $Operation = "upgrade"
    }

    Write-Step "$Operation $DisplayName"

    & $WinGet $Operation `
        --id $PackageId `
        --exact `
        --source winget `
        --silent `
        --accept-source-agreements `
        --accept-package-agreements `
        --disable-interactivity

    $ExitCode = $LASTEXITCODE
    if ($ExitCode -ne 0 -and $Upgrade) {
        Write-Warning "winget upgrade did not complete successfully. Trying winget install for $DisplayName."

        & $WinGet install `
            --id $PackageId `
            --exact `
            --source winget `
            --silent `
            --accept-source-agreements `
            --accept-package-agreements `
            --disable-interactivity

        $ExitCode = $LASTEXITCODE
    }

    if ($ExitCode -ne 0) {
        throw "winget could not install $DisplayName. Exit code: $ExitCode"
    }

    Refresh-ProcessPath
}

function Ensure-Git {
    Write-Step "Checking Git"

    $Git = Get-Command "git.exe" -ErrorAction SilentlyContinue
    if ($null -eq $Git) {
        Install-WinGetPackage -PackageId "Git.Git" -DisplayName "Git for Windows"
        $Git = Get-Command "git.exe" -ErrorAction SilentlyContinue
    }

    if ($null -eq $Git) {
        throw "Git was installed but git.exe is not available in PATH. Restart Windows and run setup.ps1 again."
    }

    $GitVersion = (& $Git.Source --version).Trim()
    Write-Success $GitVersion
}

function Get-InstalledNodeVersion {
    $Node = Get-Command "node.exe" -ErrorAction SilentlyContinue
    if ($null -eq $Node) {
        return $null
    }

    $RawVersion = (& $Node.Source --version).Trim() -replace "^v", ""
    try {
        return [version]$RawVersion
    }
    catch {
        return $null
    }
}

function Ensure-Node {
    Write-Step "Checking Node.js and npm"

    $NodeVersion = Get-InstalledNodeVersion
    if ($null -eq $NodeVersion) {
        Install-WinGetPackage -PackageId "OpenJS.NodeJS.LTS" -DisplayName "Node.js LTS"
    }
    elseif ($NodeVersion -lt $MinimumNodeVersion) {
        Install-WinGetPackage -PackageId "OpenJS.NodeJS.LTS" -DisplayName "Node.js LTS" -Upgrade
    }

    $NodeVersion = Get-InstalledNodeVersion
    $Npm = Get-Command "npm.cmd" -ErrorAction SilentlyContinue

    if ($null -eq $NodeVersion -or $NodeVersion -lt $MinimumNodeVersion -or $null -eq $Npm) {
        throw "Node.js $MinimumNodeVersion or newer and npm are required. Restart Windows and run setup.ps1 again."
    }

    $NpmVersion = (& $Npm.Source --version).Trim()
    Write-Success "Node.js $NodeVersion; npm $NpmVersion"
}

function Get-VsWherePath {
    $Path = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $Path) {
        return $Path
    }

    return $null
}

function Get-VisualStudioInstallation {
    param([string[]]$RequiredComponents = @())

    $VsWhere = Get-VsWherePath
    if ($null -eq $VsWhere) {
        return $null
    }

    $Arguments = @(
        "-products", "*",
        "-version", "[17.0,18.0)",
        "-latest"
    )

    if ($RequiredComponents.Count -gt 0) {
        $Arguments += "-requires"
        $Arguments += $RequiredComponents
    }

    $Arguments += @("-property", "installationPath")
    $InstallationPath = (& $VsWhere @Arguments | Select-Object -First 1)

    if ([string]::IsNullOrWhiteSpace($InstallationPath)) {
        return $null
    }

    return $InstallationPath.Trim()
}

function Invoke-VisualStudioInstaller {
    param(
        [Parameter(Mandatory = $true)][string]$InstallerPath,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    $Process = Start-Process `
        -FilePath $InstallerPath `
        -ArgumentList $Arguments `
        -Wait `
        -PassThru

    if ($Process.ExitCode -eq 3010 -or $Process.ExitCode -eq 1641) {
        $script:RestartRequired = $true
        return
    }

    if ($Process.ExitCode -ne 0) {
        throw "Visual Studio Installer failed. Exit code: $($Process.ExitCode)"
    }
}

function Ensure-VisualStudioBuildTools {
    Write-Step "Checking Visual Studio 2022 C++ Build Tools"

    $CompatibleInstallation = Get-VisualStudioInstallation -RequiredComponents $VisualStudioComponents
    if ($null -ne $CompatibleInstallation) {
        Write-Success "Required Visual Studio components are installed at $CompatibleInstallation"
        return $CompatibleInstallation
    }

    $ExistingInstallation = Get-VisualStudioInstallation
    if ($null -ne $ExistingInstallation) {
        Write-Host "Adding required components to $ExistingInstallation"
        $Installer = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\setup.exe"

        if (-not (Test-Path -LiteralPath $Installer)) {
            throw "Visual Studio Installer setup.exe was not found."
        }

        $Arguments = @(
            "modify",
            "--installPath", ('"{0}"' -f $ExistingInstallation),
            "--passive",
            "--norestart"
        )

        foreach ($Component in $VisualStudioComponents) {
            $Arguments += @("--add", $Component)
        }

        Invoke-VisualStudioInstaller -InstallerPath $Installer -Arguments $Arguments
    }
    else {
        Write-Host "Installing Visual Studio Build Tools 2022. This can take several minutes."
        $Bootstrapper = Join-Path $env:TEMP "vs_BuildTools.exe"

        Invoke-WebRequest `
            -Uri "https://aka.ms/vs/17/release/vs_BuildTools.exe" `
            -OutFile $Bootstrapper `
            -UseBasicParsing

        $Arguments = @(
            "--passive",
            "--wait",
            "--norestart"
        )

        foreach ($Component in $VisualStudioComponents) {
            $Arguments += @("--add", $Component)
        }

        Invoke-VisualStudioInstaller -InstallerPath $Bootstrapper -Arguments $Arguments
    }

    if ($RestartRequired) {
        return $null
    }

    $CompatibleInstallation = Get-VisualStudioInstallation -RequiredComponents $VisualStudioComponents
    if ($null -eq $CompatibleInstallation) {
        throw "Visual Studio Installer completed, but one or more required UE 5.8 components are missing. See how_to_install_toolchain.md."
    }

    Write-Success "Required Visual Studio components are installed at $CompatibleInstallation"
    return $CompatibleInstallation
}

function Test-CppToolchain {
    param([Parameter(Mandatory = $true)][string]$VisualStudioRoot)

    Write-Step "Validating the C++ toolchain"

    $Compiler = Get-ChildItem `
        -Path (Join-Path $VisualStudioRoot "VC\Tools\MSVC\14.44.*\bin\Hostx64\x64\cl.exe") `
        -ErrorAction SilentlyContinue |
        Select-Object -First 1

    $WindowsResourceCompiler = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin\10.0.22621.0\x64\rc.exe"
    $NetFxSdk = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\NETFXSDK\4.8"
    $NetFxReferences = Join-Path ${env:ProgramFiles(x86)} "Reference Assemblies\Microsoft\Framework\.NETFramework\v4.8"

    if ($null -eq $Compiler) {
        throw "MSVC 14.44 x64 compiler was not found under $VisualStudioRoot."
    }

    if (-not (Test-Path -LiteralPath $WindowsResourceCompiler)) {
        throw "Windows SDK 10.0.22621.0 resource compiler was not found."
    }

    if (-not (Test-Path -LiteralPath $NetFxSdk) -or -not (Test-Path -LiteralPath $NetFxReferences)) {
        throw ".NET Framework 4.8 SDK or targeting pack was not found."
    }

    Write-Success "MSVC 14.44, Windows SDK 10.0.22621.0, and .NET Framework 4.8 are available"
}

function Test-UnrealRoot {
    param([string]$Candidate)

    if ([string]::IsNullOrWhiteSpace($Candidate)) {
        return $false
    }

    $BuildScript = Join-Path $Candidate "Engine\Build\BatchFiles\Build.bat"
    $Editor = Join-Path $Candidate "Engine\Binaries\Win64\UnrealEditor.exe"
    return (Test-Path -LiteralPath $BuildScript) -and (Test-Path -LiteralPath $Editor)
}

function Resolve-UnrealEngineRoot {
    param([string]$RequestedRoot)

    Write-Step "Locating Unreal Engine 5.8"

    $Candidates = New-Object System.Collections.Generic.List[string]
    if (-not [string]::IsNullOrWhiteSpace($RequestedRoot)) {
        $Candidates.Add($RequestedRoot)
    }

    if (-not [string]::IsNullOrWhiteSpace($env:UE_5_8_ROOT)) {
        $Candidates.Add($env:UE_5_8_ROOT)
    }

    $Candidates.Add((Join-Path $env:ProgramFiles "Epic Games\UE_5.8"))

    $LauncherManifest = Join-Path $env:ProgramData "Epic\UnrealEngineLauncher\LauncherInstalled.dat"
    if (Test-Path -LiteralPath $LauncherManifest) {
        try {
            $LauncherData = Get-Content -LiteralPath $LauncherManifest -Raw | ConvertFrom-Json
            foreach ($Installation in $LauncherData.InstallationList) {
                if ($Installation.AppName -like "UE_5.8*" -or $Installation.ArtifactId -like "UE_5.8*") {
                    $Candidates.Add([string]$Installation.InstallLocation)
                }
            }
        }
        catch {
            Write-Warning "Could not read Epic Games Launcher installation data: $($_.Exception.Message)"
        }
    }

    $EpicGamesRoot = Join-Path $env:ProgramFiles "Epic Games"
    if (Test-Path -LiteralPath $EpicGamesRoot) {
        Get-ChildItem -LiteralPath $EpicGamesRoot -Directory -Filter "UE_5.8*" -ErrorAction SilentlyContinue |
            ForEach-Object { $Candidates.Add($_.FullName) }
    }

    foreach ($Candidate in $Candidates) {
        if (Test-UnrealRoot -Candidate $Candidate) {
            $ResolvedRoot = (Resolve-Path -LiteralPath $Candidate).Path
            Write-Success "Unreal Engine found at $ResolvedRoot"
            return $ResolvedRoot
        }
    }

    throw @"
Unreal Engine 5.8 was not found. Its installation requires an interactive Epic Games account and cannot be completed safely by this script.

Install Unreal Engine 5.8.1 through Epic Games Launcher, then run:
  powershell -ExecutionPolicy Bypass -File .\setup.ps1

For a custom location, run:
  powershell -ExecutionPolicy Bypass -File .\setup.ps1 -UnrealEngineRoot "D:\Epic Games\UE_5.8"
"@
}

function Set-DotEnvValue {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Value
    )

    $Content = Get-Content -LiteralPath $Path -Raw
    if ($null -eq $Content) {
        $Content = ""
    }

    $Pattern = "(?m)^" + [regex]::Escape($Name) + "=.*$"
    $Replacement = "$Name=$Value"

    if ([regex]::IsMatch($Content, $Pattern)) {
        $Content = [regex]::Replace($Content, $Pattern, $Replacement)
    }
    else {
        if (-not $Content.EndsWith("`n")) {
            $Content += "`r`n"
        }
        $Content += "$Replacement`r`n"
    }

    $Utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Content, $Utf8WithoutBom)
}

function Configure-TwitchBridge {
    Write-Step "Configuring Twitch Bridge for one computer"

    if (-not (Test-Path -LiteralPath $BridgeEnvironmentExample)) {
        throw "Twitch Bridge environment template was not found at $BridgeEnvironmentExample"
    }

    if ($ResetBridgeEnvironment -or -not (Test-Path -LiteralPath $BridgeEnvironment)) {
        Copy-Item -LiteralPath $BridgeEnvironmentExample -Destination $BridgeEnvironment -Force
        Write-Host "Created local Tools\TwitchBridge\.env from .env.example"
    }
    else {
        Write-Host "Preserving existing local Tools\TwitchBridge\.env values"
    }

    Set-DotEnvValue -Path $BridgeEnvironment -Name "ARENA_BRIDGE_HOST" -Value "127.0.0.1"
    Set-DotEnvValue -Path $BridgeEnvironment -Name "ARENA_BRIDGE_PORT" -Value "8080"
    Set-DotEnvValue -Path $BridgeEnvironment -Name "ARENA_BRIDGE_ALLOW_PRIVATE_NETWORK_CONNECTIONS" -Value "false"

    Write-Success "Twitch Bridge will listen on ws://127.0.0.1:8080"
}

function Configure-UnrealLoopback {
    Write-Step "Configuring Unreal Engine WebSocket for one computer"

    $LocalConfigDirectory = Join-Path $ProjectRoot "Saved\Config\WindowsEditor"
    $LocalGameConfig = Join-Path $LocalConfigDirectory "Game.ini"
    $SectionHeader = "[/Script/Demo.ArenaWebSocketSettings]"
    $SectionText = @"
$SectionHeader
ServerUrl="ws://127.0.0.1:8080"
bAllowPrivateNetworkConnections=False
"@

    if (-not (Test-Path -LiteralPath $LocalConfigDirectory)) {
        New-Item -ItemType Directory -Path $LocalConfigDirectory -Force | Out-Null
    }

    $Content = ""
    if (Test-Path -LiteralPath $LocalGameConfig) {
        $Content = Get-Content -LiteralPath $LocalGameConfig -Raw
        if ($null -eq $Content) {
            $Content = ""
        }
    }

    $SectionPattern = "(?ms)^\[/Script/Demo\.ArenaWebSocketSettings\]\r?\n.*?(?=^\[|\z)"
    if ([regex]::IsMatch($Content, $SectionPattern)) {
        $Content = [regex]::Replace($Content, $SectionPattern, "$SectionText`r`n")
    }
    else {
        if (-not [string]::IsNullOrEmpty($Content) -and -not $Content.EndsWith("`n")) {
            $Content += "`r`n"
        }
        if (-not [string]::IsNullOrEmpty($Content)) {
            $Content += "`r`n"
        }
        $Content += "$SectionText`r`n"
    }

    $Utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($LocalGameConfig, $Content, $Utf8WithoutBom)

    Write-Success "Unreal Engine will connect to ws://127.0.0.1:8080"
}

function Install-TwitchBridge {
    Write-Step "Installing and building Twitch Bridge"

    $Npm = (Get-Command "npm.cmd" -ErrorAction Stop).Source
    Push-Location $BridgeRoot
    try {
        Invoke-CheckedCommand -FilePath $Npm -Arguments @("ci") -FailureMessage "npm ci failed."
        Invoke-CheckedCommand -FilePath $Npm -Arguments @("run", "build") -FailureMessage "Twitch Bridge build failed."

        if (-not $SkipTests) {
            Invoke-CheckedCommand -FilePath $Npm -Arguments @("test") -FailureMessage "Twitch Bridge tests failed."
        }
    }
    finally {
        Pop-Location
    }

    Write-Success "Twitch Bridge dependencies and build are ready"
}

function Generate-And-BuildUnrealProject {
    param([Parameter(Mandatory = $true)][string]$EngineRoot)

    $GenerateProjectFiles = Join-Path $EngineRoot "Engine\Build\BatchFiles\GenerateProjectFiles.bat"
    $BuildScript = Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat"

    Write-Step "Generating Unreal Engine project files"
    if (-not (Test-Path -LiteralPath $GenerateProjectFiles)) {
        throw "GenerateProjectFiles.bat was not found at $GenerateProjectFiles"
    }

    Invoke-CheckedCommand `
        -FilePath $GenerateProjectFiles `
        -Arguments @("-project=$ProjectFile", "-game", "-engine") `
        -FailureMessage "Unreal project file generation failed."

    if ($SkipBuild) {
        Write-Warning "Unreal project build was skipped by -SkipBuild."
        return
    }

    if ($null -ne (Get-Process -Name "UnrealEditor" -ErrorAction SilentlyContinue)) {
        throw "Unreal Editor is running. Close it and run setup.ps1 again, or use -SkipBuild."
    }

    Write-Step "Building DemoEditor"
    Invoke-CheckedCommand `
        -FilePath $BuildScript `
        -Arguments @("DemoEditor", "Win64", "Development", "-Project=$ProjectFile", "-WaitMutex") `
        -FailureMessage "DemoEditor build failed."

    Write-Success "DemoEditor build completed"
}

function Ensure-TwitchAuthorization {
    if ($SkipTwitchAuthorization) {
        Write-Warning "Twitch authorization was skipped. Run 'npm run auth' in Tools\TwitchBridge before using Twitch mode."
        return
    }

    Write-Step "Checking Twitch authorization"

    $Npm = (Get-Command "npm.cmd" -ErrorAction Stop).Source
    Push-Location $BridgeRoot
    try {
        & $Npm run auth:status
        if ($LASTEXITCODE -ne 0) {
            Write-Host "Twitch authorization is required. Follow the URL and device code shown below."
            Invoke-CheckedCommand -FilePath $Npm -Arguments @("run", "auth") -FailureMessage "Twitch authorization failed."
        }
    }
    finally {
        Pop-Location
    }

    Write-Success "Twitch authorization is ready"
}

if ($env:OS -ne "Windows_NT") {
    throw "setup.ps1 supports Windows only."
}

if (-not [Environment]::Is64BitOperatingSystem) {
    throw "Unreal Engine 5.8 Editor requires 64-bit Windows."
}

if (-not (Test-Path -LiteralPath $ProjectFile)) {
    throw "Run setup.ps1 from the repository root containing Demo.uproject."
}

Write-Host "Demo project Windows setup" -ForegroundColor White
Write-Host "Project: $ProjectRoot"

Ensure-Git
Ensure-Node
$VisualStudioRoot = Ensure-VisualStudioBuildTools

if ($RestartRequired) {
    Write-Warning "A Windows restart is required to finish installing the toolchain. Restart Windows, then run setup.ps1 again."
    exit 3010
}

Test-CppToolchain -VisualStudioRoot $VisualStudioRoot
$ResolvedEngineRoot = Resolve-UnrealEngineRoot -RequestedRoot $UnrealEngineRoot

Configure-TwitchBridge
Configure-UnrealLoopback
Install-TwitchBridge
Generate-And-BuildUnrealProject -EngineRoot $ResolvedEngineRoot
Ensure-TwitchAuthorization

$EditorPath = Join-Path $ResolvedEngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"

Write-Host ""
Write-Host "Setup completed successfully." -ForegroundColor Green
Write-Host ""
Write-Host "Start Unreal Editor:"
Write-Host ('  & "{0}" "{1}"' -f $EditorPath, $ProjectFile)
Write-Host ""
Write-Host "Start Twitch Bridge in another PowerShell window:"
Write-Host ('  Set-Location "{0}"' -f $BridgeRoot)
Write-Host "  npm run twitch"
Write-Host ""
Write-Host "The project and bridge are configured for ws://127.0.0.1:8080."
