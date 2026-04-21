param(
    [string]$ConfigurePreset = "release",
    [string]$BuildPreset = "release",
    [string]$BuildDir = "",
    [string]$OutputRoot = "",
    [switch]$SkipBuild,
    [switch]$BuildOnly,
    [string]$PythonEmbedVersion = "",
    [string]$PythonEmbedZip = "",
    [string]$PortableZipName = "NisojeStudio-portable.zip",
    [string]$PanelName = "Nisoje Studio",
    [switch]$RequireRemoteAuth,
    [string]$BaseConfigPath = "",
    [string]$ExternalTargetUser = "",
    [switch]$ClearExternalTargetUser,
    [switch]$AllowBlankExternalTargetUser
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-ProjectRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

function Resolve-VsDeveloperCommand {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        return ""
    }

    $installationPath = (& $vswhere `
        -latest `
        -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath | Select-Object -First 1).Trim()
    if ([string]::IsNullOrWhiteSpace($installationPath)) {
        return ""
    }

    $candidates = @(
        (Join-Path $installationPath "Common7\Tools\VsDevCmd.bat"),
        (Join-Path $installationPath "VC\Auxiliary\Build\vcvars64.bat")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    return ""
}

function Import-EnvironmentFromBatchFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BatchPath,

        [string[]]$Arguments = @()
    )

    $commandLine = ('call "{0}" {1} >nul && set' -f $BatchPath, ($Arguments -join ' ')).Trim()
    $envDump = cmd.exe /s /c $commandLine
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to import environment from $BatchPath"
    }

    foreach ($line in $envDump) {
        $separator = $line.IndexOf('=')
        if ($separator -lt 1) {
            continue
        }

        $name = $line.Substring(0, $separator)
        $value = $line.Substring($separator + 1)
        Set-Item -Path ("Env:{0}" -f $name) -Value $value
    }
}

function Ensure-MsvcBuildEnvironment {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) {
        return
    }

    $developerCommand = Resolve-VsDeveloperCommand
    if ([string]::IsNullOrWhiteSpace($developerCommand)) {
        throw "MSVC build tools were not found. Install Visual Studio Build Tools with the C++ toolchain."
    }

    $commandName = [System.IO.Path]::GetFileName($developerCommand)
    $arguments = @()
    if ($commandName -ieq "VsDevCmd.bat") {
        $arguments = @("-no_logo", "-arch=x64", "-host_arch=x64")
    }

    Import-EnvironmentFromBatchFile -BatchPath $developerCommand -Arguments $arguments

    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw "MSVC compiler was still unavailable after loading $developerCommand"
    }
}

function Remove-IfExists {
    param([string]$PathValue)
    if (Test-Path $PathValue) {
        Remove-Item $PathValue -Recurse -Force
    }
}

function Set-ObjectProperty {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Target,

        [Parameter(Mandatory = $true)]
        [string]$Name,

        [AllowNull()]
        [object]$Value
    )

    if ($null -eq $Target.PSObject.Properties[$Name]) {
        $Target | Add-Member -NotePropertyName $Name -NotePropertyValue $Value
        return
    }

    $Target.$Name = $Value
}

function Ensure-ObjectProperty {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Target,

        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    $existing = $Target.PSObject.Properties[$Name]
    if ($null -ne $existing -and $null -ne $Target.$Name) {
        return $Target.$Name
    }

    $child = [pscustomobject]@{}
    Set-ObjectProperty -Target $Target -Name $Name -Value $child
    return $child
}

function Resolve-BasePanelConfigPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot,

        [string]$RequestedPath = ""
    )

    $candidate = if ([string]::IsNullOrWhiteSpace($RequestedPath)) {
        Join-Path $ProjectRoot "panel_config.json"
    } elseif ([System.IO.Path]::IsPathRooted($RequestedPath)) {
        $RequestedPath
    } else {
        Join-Path $ProjectRoot $RequestedPath
    }

    if (-not (Test-Path $candidate)) {
        throw "Base panel config not found at $candidate"
    }

    return (Resolve-Path $candidate).Path
}

function New-ReleasePanelConfig {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BaseConfigPath,

        [Parameter(Mandatory = $true)]
        [string]$PanelName,

        [Parameter(Mandatory = $true)]
        [bool]$RequireRemoteAuth,

        [string]$ExternalTargetUser = "",

        [Parameter(Mandatory = $true)]
        [bool]$ClearExternalTargetUser,

        [Parameter(Mandatory = $true)]
        [bool]$AllowBlankExternalTargetUser
    )

    $baseConfigRaw = Get-Content -LiteralPath $BaseConfigPath -Raw
    $baseConfig = $baseConfigRaw | ConvertFrom-Json
    if ($null -eq $baseConfig) {
        throw "Base panel config at $BaseConfigPath could not be parsed."
    }

    Set-ObjectProperty -Target $baseConfig -Name "panel_name" -Value $PanelName
    Set-ObjectProperty -Target $baseConfig -Name "bridge_mode" -Value "external"

    $resolvedTargetUser = if ($ClearExternalTargetUser) {
        ""
    } elseif (-not [string]::IsNullOrWhiteSpace($ExternalTargetUser)) {
        $ExternalTargetUser.Trim()
    } elseif ($null -ne $baseConfig.PSObject.Properties["external_target_user"]) {
        [string]$baseConfig.external_target_user
    } else {
        ""
    }

    if ([string]::IsNullOrWhiteSpace($resolvedTargetUser) -and -not $AllowBlankExternalTargetUser) {
        throw "Release packaging requires external_target_user. Update panel_config.json or pass -ExternalTargetUser."
    }

    Set-ObjectProperty -Target $baseConfig -Name "external_target_user" -Value $resolvedTargetUser

    $resolvedExternalWsPort = 8765
    if ($null -ne $baseConfig.PSObject.Properties["external_ws_port"]) {
        try {
            $parsedPort = [int]$baseConfig.external_ws_port
            if ($parsedPort -gt 0 -and $parsedPort -le 65535) {
                $resolvedExternalWsPort = $parsedPort
            }
        } catch {
        }
    }
    Set-ObjectProperty -Target $baseConfig -Name "external_ws_port" -Value $resolvedExternalWsPort

    $defaultGameId = if ($null -ne $baseConfig.PSObject.Properties["default_game_id"]) {
        [string]$baseConfig.default_game_id
    } else {
        ""
    }
    if ([string]::IsNullOrWhiteSpace($defaultGameId)) {
        Set-ObjectProperty -Target $baseConfig -Name "default_game_id" -Value "event-counter"
    }

    Set-ObjectProperty -Target $baseConfig -Name "embedded_ui_enabled" -Value $true
    Set-ObjectProperty -Target $baseConfig -Name "embedded_ui_fallback_to_browser" -Value $true
    Set-ObjectProperty -Target $baseConfig -Name "embedded_ui_devtools" -Value $false
    Set-ObjectProperty -Target $baseConfig -Name "embedded_ui_url" -Value "http://127.0.0.1:18913/"
    Set-ObjectProperty -Target $baseConfig -Name "embedded_ui_startup_timeout_ms" -Value 8000

    $authConfig = Ensure-ObjectProperty -Target $baseConfig -Name "auth"
    Set-ObjectProperty -Target $authConfig -Name "required" -Value $RequireRemoteAuth

    $bridgeConfig = Ensure-ObjectProperty -Target $baseConfig -Name "bridge"
    Set-ObjectProperty -Target $bridgeConfig -Name "enabled" -Value $true
    Set-ObjectProperty -Target $bridgeConfig -Name "stub_mode" -Value $false
    Set-ObjectProperty -Target $bridgeConfig -Name "source_name" -Value "tiktok-external"

    return $baseConfig
}

function Copy-IfExists {
    param(
        [string]$SourcePath,
        [string]$DestinationPath
    )

    if (Test-Path $SourcePath) {
        $destinationDir = Split-Path -Parent $DestinationPath
        if (-not [string]::IsNullOrWhiteSpace($destinationDir) -and -not (Test-Path $destinationDir)) {
            New-Item -ItemType Directory -Path $destinationDir -Force | Out-Null
        }
        Copy-Item $SourcePath $DestinationPath -Force
    }
}

function Copy-DirectoryFiltered {
    param(
        [string]$SourceDir,
        [string]$DestinationDir,
        [string[]]$ExcludedDirNames = @(),
        [string[]]$ExcludedFileNames = @(),
        [string[]]$ExcludedTopLevelPatterns = @()
    )

    if (-not (Test-Path $SourceDir)) {
        return
    }

    Get-ChildItem $SourceDir -Recurse -File | Where-Object {
        $relative = $_.FullName.Substring($SourceDir.Length).TrimStart('\')
        $parts = $relative.Split('\')
        foreach ($part in $parts) {
            if ($ExcludedDirNames -contains $part) {
                return $false
            }
        }

        if ($ExcludedFileNames -contains $_.Name) {
            return $false
        }

        if ($parts.Length -gt 0) {
            foreach ($pattern in $ExcludedTopLevelPatterns) {
                if ($parts[0] -like $pattern) {
                    return $false
                }
            }
        }

        return $true
    } | ForEach-Object {
        $relative = $_.FullName.Substring($SourceDir.Length).TrimStart('\')
        $target = Join-Path $DestinationDir $relative
        $targetDir = Split-Path -Parent $target
        if (-not (Test-Path $targetDir)) {
            New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
        }
        Copy-Item $_.FullName $target -Force
    }
}

function Invoke-PythonCapture {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PythonExe,

        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    $output = & $PythonExe @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Python command failed: $PythonExe $($Arguments -join ' ')`n$output"
    }

    return ($output | Out-String).Trim()
}

function Read-CMakeCacheValue {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CachePath,

        [Parameter(Mandatory = $true)]
        [string]$Key
    )

    if (-not (Test-Path $CachePath)) {
        return ""
    }

    $pattern = "^" + [regex]::Escape($Key) + ":[^=]*=(.*)$"
    $match = Select-String -Path $CachePath -Pattern $pattern | Select-Object -First 1
    if ($null -eq $match) {
        return ""
    }

    return $match.Matches[0].Groups[1].Value.Trim()
}

function Resolve-ConfigurePresetBinaryDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot,

        [string]$PresetName
    )

    if ([string]::IsNullOrWhiteSpace($PresetName)) {
        return ""
    }

    $presetPath = Join-Path $ProjectRoot "CMakePresets.json"
    if (-not (Test-Path $presetPath)) {
        return ""
    }

    $presetJson = Get-Content -LiteralPath $presetPath -Raw | ConvertFrom-Json
    $configurePreset = @($presetJson.configurePresets | Where-Object { $_.name -eq $PresetName } | Select-Object -First 1)
    if ($configurePreset.Count -eq 0) {
        return ""
    }

    $binaryDir = [string]$configurePreset[0].binaryDir
    if ([string]::IsNullOrWhiteSpace($binaryDir)) {
        return ""
    }

    $binaryDir = $binaryDir.Replace('${sourceDir}', $ProjectRoot)
    return [System.IO.Path]::GetFullPath($binaryDir)
}

function Resolve-ToolchainFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot
    )

    $cacheCandidates = @(
        (Join-Path $ProjectRoot "build\CMakeCache.txt"),
        (Join-Path $ProjectRoot "build\release\CMakeCache.txt")
    )

    foreach ($cachePath in $cacheCandidates) {
        $toolchain = Read-CMakeCacheValue -CachePath $cachePath -Key "CMAKE_TOOLCHAIN_FILE"
        if (-not [string]::IsNullOrWhiteSpace($toolchain) -and (Test-Path $toolchain)) {
            return (Resolve-Path $toolchain).Path
        }
    }

    if (-not [string]::IsNullOrWhiteSpace($env:CMAKE_TOOLCHAIN_FILE) -and (Test-Path $env:CMAKE_TOOLCHAIN_FILE)) {
        return (Resolve-Path $env:CMAKE_TOOLCHAIN_FILE).Path
    }

    if (-not [string]::IsNullOrWhiteSpace($env:VCPKG_ROOT)) {
        $candidate = Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    return ""
}

function Resolve-ReleaseBuildDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot,

        [string]$RequestedBuildDir
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedBuildDir)) {
        return [System.IO.Path]::GetFullPath($RequestedBuildDir)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot "build\release"))
}

function Ensure-ReleaseBuild {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot,

        [Parameter(Mandatory = $true)]
        [string]$BuildDirectory,

        [string]$RequestedConfigurePreset,

        [string]$RequestedBuildPreset
    )

    Ensure-MsvcBuildEnvironment

    $toolchainFile = Resolve-ToolchainFile -ProjectRoot $ProjectRoot
    Remove-IfExists $BuildDirectory
    New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null

    $useConfigurePreset = -not [string]::IsNullOrWhiteSpace($RequestedConfigurePreset)
    if ($useConfigurePreset) {
        $presetBuildDirectory = Resolve-ConfigurePresetBinaryDirectory `
            -ProjectRoot $ProjectRoot `
            -PresetName $RequestedConfigurePreset

        if (-not [string]::IsNullOrWhiteSpace($presetBuildDirectory)) {
            $normalizedRequestedBuildDir = [System.IO.Path]::GetFullPath($BuildDirectory).TrimEnd('\')
            $normalizedPresetBuildDir = [System.IO.Path]::GetFullPath($presetBuildDirectory).TrimEnd('\')
            if ($normalizedRequestedBuildDir -ine $normalizedPresetBuildDir) {
                Write-Host "[package] BuildDir overrides preset binaryDir; configuring explicit Release build at $normalizedRequestedBuildDir"
                $useConfigurePreset = $false
            }
        }
    }

    if ($useConfigurePreset) {
        $configureArgs = @("--preset", $RequestedConfigurePreset)
        if (-not [string]::IsNullOrWhiteSpace($toolchainFile)) {
            $configureArgs += "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile"
        }
        cmake @configureArgs
        cmake --build --preset $RequestedBuildPreset
        return
    }

    $configureArgs = @(
        "-S", $ProjectRoot,
        "-B", $BuildDirectory,
        "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release"
    )
    if (-not [string]::IsNullOrWhiteSpace($toolchainFile)) {
        $configureArgs += "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile"
    }

    cmake @configureArgs
    cmake --build $BuildDirectory --config Release
}

function Assert-NoDebugCrtDependencies {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BinaryPath
    )

    if (-not (Test-Path $BinaryPath)) {
        throw "Binary not found at $BinaryPath"
    }

    $forbiddenDlls = @(
        "MSVCP140D.dll",
        "VCRUNTIME140D.dll",
        "VCRUNTIME140_1D.dll",
        "ucrtbased.dll"
    )

    $binaryText = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($BinaryPath))
    $matches = @()
    foreach ($dllName in $forbiddenDlls) {
        if ($binaryText.IndexOf($dllName, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
            $matches += $dllName
        }
    }

    if ($matches.Count -gt 0) {
        throw ("Debug CRT dependencies detected in {0}: {1}" -f $BinaryPath, ($matches -join ", "))
    }
}

function Remove-PythonArtifacts {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RootPath
    )

    if (-not (Test-Path $RootPath)) {
        return
    }

    Get-ChildItem -Path $RootPath -Recurse -Directory -Filter "__pycache__" -ErrorAction SilentlyContinue |
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
    Get-ChildItem -Path $RootPath -Recurse -File -Include "*.pyc" -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
}

function Write-PortableRequirementsFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PackageRoot,

        [Parameter(Mandatory = $true)]
        [System.Text.Encoding]$Encoding
    )

    $requirements = @'
Nisoje Studio portable prerequisites (Windows x64)

This portable package already includes:
- NisojeStudio.exe built in Release mode
- tools\bridge_py\python_runtime for the bundled TikTok bridge
- tools\game_bridge_py for launching external local games

This portable package does not require:
- a system Python installation

This portable package still requires on the target machine:
1. Microsoft Visual C++ Redistributable x64 (2015-2022 or newer)
   Download: https://aka.ms/vs/17/release/vc_redist.x64.exe
2. Microsoft Edge WebView2 Runtime for the embedded desktop window
   Download: https://developer.microsoft.com/microsoft-edge/webview2/

Operational notes:
- Launch Nisoje Studio.bat checks for the VC++ runtime before starting the host.
- NisojeStudio.exe can still be started directly if you already know the machine has the required runtime.
- External local games are discovered under %USERPROFILE%\Desktop\Juegos unless NLP3_LOCAL_GAMES_ROOT overrides it.
- The app needs local loopback access and free ports for the panel HTTP server and the external bridge.
'@

    [System.IO.File]::WriteAllText((Join-Path $PackageRoot "PORTABLE_REQUIREMENTS.txt"), $requirements, $Encoding)
}

function Write-PackageManifestFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PackageRoot,

        [Parameter(Mandatory = $true)]
        [string]$ManifestPath,

        [Parameter(Mandatory = $true)]
        [System.Text.Encoding]$Encoding
    )

    $resolvedRoot = (Resolve-Path $PackageRoot).Path.TrimEnd('\')
    $entries = New-Object System.Collections.Generic.List[string]

    Get-ChildItem -LiteralPath $PackageRoot -Recurse |
        Sort-Object FullName |
        ForEach-Object {
            $relativePath = $_.FullName.Substring($resolvedRoot.Length).TrimStart('\')
            if ([string]::IsNullOrWhiteSpace($relativePath)) {
                return
            }

            if ($_.PSIsContainer) {
                $entries.Add("[DIR]  $relativePath")
                return
            }

            $entries.Add(("{0}`t{1}`t{2:yyyy-MM-ddTHH:mm:ss}" -f $relativePath, $_.Length, $_.LastWriteTime))
        }

    [System.IO.File]::WriteAllLines($ManifestPath, $entries, $Encoding)
}

function Write-Sha256SumsFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$OutputRoot,

        [Parameter(Mandatory = $true)]
        [string[]]$FilePaths,

        [Parameter(Mandatory = $true)]
        [System.Text.Encoding]$Encoding
    )

    $resolvedOutputRoot = [System.IO.Path]::GetFullPath($OutputRoot).TrimEnd('\')
    $lines = foreach ($filePath in $FilePaths) {
        if (-not (Test-Path $filePath)) {
            continue
        }

        $resolvedFilePath = [System.IO.Path]::GetFullPath($filePath)
        $relativePath = $resolvedFilePath.Substring($resolvedOutputRoot.Length).TrimStart('\').Replace('/', '\')
        $hash = (Get-FileHash -LiteralPath $resolvedFilePath -Algorithm SHA256).Hash.ToUpperInvariant()
        "{0}  {1}" -f $hash, $relativePath
    }

    [System.IO.File]::WriteAllLines((Join-Path $OutputRoot "SHA256SUMS.txt"), $lines, $Encoding)
}

function Resolve-BridgeVenvPythonExe {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot
    )

    $pythonExe = Join-Path $ProjectRoot "tools\bridge_py\.venv\Scripts\python.exe"
    if (-not (Test-Path $pythonExe)) {
        throw "Portable bridge packaging now requires tools\\bridge_py\\.venv as a build-time source. Run tools\\bridge_py\\setup_windows_bridge_env.ps1 first."
    }

    return $pythonExe
}

function Resolve-PythonEmbedArchive {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot,

        [Parameter(Mandatory = $true)]
        [string]$BridgeVenvPython,

        [string]$RequestedVersion,

        [string]$RequestedArchive
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedArchive)) {
        if (-not (Test-Path $RequestedArchive)) {
            throw "Python embeddable archive not found at $RequestedArchive"
        }

        return [pscustomobject]@{
            Version = $RequestedVersion
            ArchivePath = (Resolve-Path $RequestedArchive).Path
        }
    }

    $version = $RequestedVersion
    if ([string]::IsNullOrWhiteSpace($version)) {
        $version = Invoke-PythonCapture `
            -PythonExe $BridgeVenvPython `
            -Arguments @("-c", "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}')")
    }

    $archiveName = "python-$version-embed-amd64.zip"
    $cacheDir = Join-Path $ProjectRoot "build\package_cache"
    New-Item -ItemType Directory -Path $cacheDir -Force | Out-Null
    $archivePath = Join-Path $cacheDir $archiveName

    if (-not (Test-Path $archivePath)) {
        $downloadUrl = "https://www.python.org/ftp/python/$version/$archiveName"
        Write-Host "[package] downloading python embeddable runtime: $downloadUrl"
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri $downloadUrl -OutFile $archivePath
    }

    return [pscustomobject]@{
        Version = $version
        ArchivePath = $archivePath
    }
}

function Initialize-PortablePythonRuntime {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot,

        [Parameter(Mandatory = $true)]
        [string]$PackageBridgeRoot,

        [Parameter(Mandatory = $true)]
        [string]$BridgeVenvPython,

        [Parameter(Mandatory = $true)]
        [string]$PythonEmbedArchive
    )

    $runtimeRoot = Join-Path $PackageBridgeRoot "python_runtime"
    Remove-IfExists $runtimeRoot
    New-Item -ItemType Directory -Path $runtimeRoot -Force | Out-Null

    Expand-Archive -LiteralPath $PythonEmbedArchive -DestinationPath $runtimeRoot -Force

    $pthFile = Get-ChildItem -Path $runtimeRoot -Filter "python*._pth" | Select-Object -First 1
    if ($null -eq $pthFile) {
        throw "Embedded Python runtime does not contain a python*._pth file."
    }

    $stdlibZip = Get-ChildItem -Path $runtimeRoot -Filter "python*.zip" | Select-Object -First 1
    if ($null -eq $stdlibZip) {
        throw "Embedded Python runtime does not contain the standard library zip."
    }

    $sitePackagesSource = Join-Path $ProjectRoot "tools\bridge_py\.venv\Lib\site-packages"
    if (-not (Test-Path $sitePackagesSource)) {
        throw "site-packages source not found at $sitePackagesSource"
    }

    $sitePackagesTarget = Join-Path $runtimeRoot "Lib\site-packages"
    New-Item -ItemType Directory -Path $sitePackagesTarget -Force | Out-Null
    Copy-DirectoryFiltered `
        -SourceDir $sitePackagesSource `
        -DestinationDir $sitePackagesTarget `
        -ExcludedDirNames @("__pycache__", ".pytest_cache", ".mypy_cache", ".ruff_cache", "tests") `
        -ExcludedFileNames @("easy-install.pth", "distutils-precedence.pth") `
        -ExcludedTopLevelPatterns @(
            "pip",
            "pip-*",
            "setuptools",
            "setuptools-*",
            "wheel",
            "wheel-*",
            "pkg_resources",
            "_distutils_hack"
        )

    $pthContent = @(
        $stdlibZip.Name,
        ".",
        "..",
        "Lib",
        "Lib/site-packages",
        "import site"
    )
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllLines($pthFile.FullName, $pthContent, $utf8)

    $runtimePython = Join-Path $runtimeRoot "python.exe"
    if (-not (Test-Path $runtimePython)) {
        throw "Embedded Python runtime did not produce python.exe at $runtimePython"
    }

    $runtimeProbe = Invoke-PythonCapture `
        -PythonExe $runtimePython `
        -Arguments @(
            "-c",
            "import sys, asyncio, json, websockets, yaml, TikTokLive; print(sys.executable); print(sys.prefix); print(sys.base_prefix)"
        )

    Write-Host "[package] bridge runtime: embedded Python ready"
    Write-Host $runtimeProbe
    Remove-PythonArtifacts -RootPath $runtimeRoot
}

$projectRoot = Resolve-ProjectRoot
Push-Location $projectRoot
try {
    $buildDir = Resolve-ReleaseBuildDirectory -ProjectRoot $projectRoot -RequestedBuildDir $BuildDir

    if (-not $SkipBuild) {
        Write-Host "[package] configuring release build..."
        Ensure-ReleaseBuild `
            -ProjectRoot $projectRoot `
            -BuildDirectory $buildDir `
            -RequestedConfigurePreset $ConfigurePreset `
            -RequestedBuildPreset $BuildPreset
    }

    $cachePath = Join-Path $buildDir "CMakeCache.txt"
    if (-not (Test-Path $cachePath)) {
        throw "Release build cache not found at $cachePath. Run the package script without -SkipBuild or provide -BuildDir pointing to a configured Release build."
    }

    $buildType = Read-CMakeCacheValue -CachePath $cachePath -Key "CMAKE_BUILD_TYPE"
    if (-not [string]::IsNullOrWhiteSpace($buildType) -and $buildType -ne "Release") {
        throw "Portable packaging requires a Release host build. Found CMAKE_BUILD_TYPE=$buildType in $cachePath"
    }

    $exePath = Join-Path $buildDir "src\platform\NisojeStudio.exe"
    $compatExePath = Join-Path $buildDir "src\platform\nlp3_app.exe"
    $loaderPath = Join-Path $buildDir "src\platform\WebView2Loader.dll"

    if (-not (Test-Path $exePath)) {
        throw "Built executable not found at $exePath"
    }

    Assert-NoDebugCrtDependencies -BinaryPath $exePath
    if (Test-Path $compatExePath) {
        Assert-NoDebugCrtDependencies -BinaryPath $compatExePath
    }

    if ($BuildOnly) {
        Write-Host "[package] build-only mode complete"
        Write-Host "[package] host exe:         $exePath"
        if (Test-Path $compatExePath) {
            Write-Host "[package] compat host exe:  $compatExePath"
        }
        if (Test-Path $loaderPath) {
            Write-Host "[package] WebView2 loader:  $loaderPath"
        }
        return
    }

    if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
        $OutputRoot = Join-Path $projectRoot "dist"
    }

    $packageRoot = Join-Path $OutputRoot "NisojeStudio"
    $toolsRoot = Join-Path $packageRoot "tools"
    $bridgeRoot = Join-Path $toolsRoot "bridge_py"
    $gameBridgeRoot = Join-Path $toolsRoot "game_bridge_py"

    Remove-IfExists $packageRoot
    New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $toolsRoot -Force | Out-Null

    Copy-IfExists $exePath (Join-Path $packageRoot "NisojeStudio.exe")
    Copy-IfExists $compatExePath (Join-Path $packageRoot "nlp3_app.exe")
    Copy-IfExists $loaderPath (Join-Path $packageRoot "WebView2Loader.dll")
    Copy-IfExists (Join-Path $projectRoot "README.md") (Join-Path $packageRoot "README.md")
    Copy-IfExists (Join-Path $projectRoot "README_embedded_ui.md") (Join-Path $packageRoot "README_embedded_ui.md")
    Copy-IfExists (Join-Path $projectRoot "README_panel_ui.md") (Join-Path $packageRoot "README_panel_ui.md")
    Copy-IfExists (Join-Path $projectRoot "README_tts.md") (Join-Path $packageRoot "README_tts.md")
    Copy-IfExists (Join-Path $projectRoot "tools\install_tts_voices.ps1") (Join-Path $toolsRoot "install_tts_voices.ps1")

    Copy-DirectoryFiltered `
        -SourceDir (Join-Path $projectRoot "tools\bridge_py") `
        -DestinationDir $bridgeRoot `
        -ExcludedDirNames @(".venv", "python_runtime", "__pycache__", ".pytest_cache", ".mypy_cache", ".ruff_cache", "tests", "logs", "live_inbox") `
        -ExcludedFileNames @(
            ".gitignore",
            "Dockerfile",
            "README.md",
            "README_bridge.md",
            "REAL_LOCAL_RUNBOOK.md",
            "requirements.txt",
            "run_bridge.sh",
            "sample_events.py",
            "sample_session.jsonl",
            "setup_windows_bridge_env.ps1"
        )

    Copy-DirectoryFiltered `
        -SourceDir (Join-Path $projectRoot "tools\game_bridge_py") `
        -DestinationDir $gameBridgeRoot `
        -ExcludedDirNames @("__pycache__", ".pytest_cache", ".mypy_cache", ".ruff_cache", "tests") `
        -ExcludedFileNames @(
            ".gitignore",
            "README.md"
        )

    $bridgeVenvPython = Resolve-BridgeVenvPythonExe -ProjectRoot $projectRoot
    $embedRuntime = Resolve-PythonEmbedArchive `
        -ProjectRoot $projectRoot `
        -BridgeVenvPython $bridgeVenvPython `
        -RequestedVersion $PythonEmbedVersion `
        -RequestedArchive $PythonEmbedZip

    Write-Host "[package] bridge runtime source: tools\\bridge_py\\.venv"
    Write-Host "[package] python embeddable version: $($embedRuntime.Version)"
    Initialize-PortablePythonRuntime `
        -ProjectRoot $projectRoot `
        -PackageBridgeRoot $bridgeRoot `
        -BridgeVenvPython $bridgeVenvPython `
        -PythonEmbedArchive $embedRuntime.ArchivePath
    Remove-PythonArtifacts -RootPath $bridgeRoot
    Remove-PythonArtifacts -RootPath $gameBridgeRoot

    $resolvedBaseConfigPath = Resolve-BasePanelConfigPath -ProjectRoot $projectRoot -RequestedPath $BaseConfigPath
    $releaseConfig = New-ReleasePanelConfig `
        -BaseConfigPath $resolvedBaseConfigPath `
        -PanelName $PanelName `
        -RequireRemoteAuth ([bool]$RequireRemoteAuth) `
        -ExternalTargetUser $ExternalTargetUser `
        -ClearExternalTargetUser ([bool]$ClearExternalTargetUser) `
        -AllowBlankExternalTargetUser ([bool]$AllowBlankExternalTargetUser)
    $configJson = $releaseConfig | ConvertTo-Json -Depth 20
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText((Join-Path $packageRoot "panel_config.json"), $configJson + [Environment]::NewLine, $utf8)
    Write-Host "[package] panel config source: $resolvedBaseConfigPath"
    Write-Host "[package] target user: $(if ([string]::IsNullOrWhiteSpace([string]$releaseConfig.external_target_user)) { '<blank>' } else { [string]$releaseConfig.external_target_user })"
    Write-Host "[package] auth required: $([bool]$releaseConfig.auth.required)"
    Write-PortableRequirementsFile -PackageRoot $packageRoot -Encoding $utf8

    $launchScript = @'
@echo off
setlocal
cd /d "%~dp0"
reg query "HKLM\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X64" /v Installed 2>nul | find "0x1" >nul
if errorlevel 1 (
echo.
echo Nisoje Studio portable requires Microsoft Visual C++ Redistributable x64.
echo Download: https://aka.ms/vs/17/release/vc_redist.x64.exe
echo See PORTABLE_REQUIREMENTS.txt in this folder for the full prerequisites.
pause
exit /b 1
)
start "" "%~dp0NisojeStudio.exe"
endlocal
'@
    [System.IO.File]::WriteAllText((Join-Path $packageRoot "Launch Nisoje Studio.bat"), $launchScript, $utf8)

    Assert-NoDebugCrtDependencies -BinaryPath (Join-Path $packageRoot "NisojeStudio.exe")
    if (Test-Path (Join-Path $packageRoot "nlp3_app.exe")) {
        Assert-NoDebugCrtDependencies -BinaryPath (Join-Path $packageRoot "nlp3_app.exe")
    }

    if ([string]::IsNullOrWhiteSpace($PortableZipName)) {
        $PortableZipName = "NisojeStudio-portable.zip"
    }

    $zipPath = Join-Path $OutputRoot $PortableZipName
    Remove-IfExists $zipPath
    Compress-Archive -Path (Join-Path $packageRoot "*") -DestinationPath $zipPath

    $manifestPath = Join-Path $OutputRoot "NisojeStudio-manifest.txt"
    Write-PackageManifestFile -PackageRoot $packageRoot -ManifestPath $manifestPath -Encoding $utf8
    Write-Sha256SumsFile `
        -OutputRoot $OutputRoot `
        -FilePaths @(
            (Join-Path $packageRoot "NisojeStudio.exe"),
            $zipPath
        ) `
        -Encoding $utf8

    Write-Host "[package] portable folder: $packageRoot"
    Write-Host "[package] portable zip:    $zipPath"
    Write-Host "[package] manifest:        $manifestPath"
    Write-Host "[package] host prerequisites: Microsoft Visual C++ Redistributable x64 + Microsoft Edge WebView2 Runtime"
} finally {
    Pop-Location
}
