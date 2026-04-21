<#
.SYNOPSIS
Genera catalog/latest.json para R2 a partir de los paquetes ZIP creados por package_panel_game.ps1.
#>

param(
    [string]$PackagesRoot = "",
    [string]$OutputPath = "",
    [string]$R2Prefix = "catalog/games"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.IO.Compression.FileSystem

function Resolve-NormalizedPath {
    param([Parameter(Mandatory = $true)][string]$PathValue)
    return [System.IO.Path]::GetFullPath($PathValue)
}

function Ensure-Directory {
    param([Parameter(Mandatory = $true)][string]$PathValue)
    if (-not (Test-Path -LiteralPath $PathValue)) {
        New-Item -ItemType Directory -Path $PathValue -Force | Out-Null
    }
}

function Trim-Text {
    param($Value)
    if ($null -eq $Value) {
        return ""
    }
    return ([string]$Value).Trim()
}

function Get-NestedValue {
    param(
        $Object,
        [Parameter(Mandatory = $true)][string[]]$Path
    )

    $current = $Object
    foreach ($segment in $Path) {
        if ($null -eq $current) {
            return $null
        }

        $property = $current.PSObject.Properties[$segment]
        if ($null -eq $property) {
            return $null
        }

        $current = $property.Value
    }

    return $current
}

function Read-ZipEntryText {
    param(
        [Parameter(Mandatory = $true)][string]$ZipPath,
        [Parameter(Mandatory = $true)][string]$EntryName
    )

    $archive = [System.IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        $entry = $archive.Entries | Where-Object { $_.FullName -eq $EntryName } | Select-Object -First 1
        if ($null -eq $entry) {
            return ""
        }

        $reader = New-Object System.IO.StreamReader($entry.Open(), [System.Text.UTF8Encoding]::new($false))
        try {
            return $reader.ReadToEnd()
        } finally {
            $reader.Dispose()
        }
    } finally {
        $archive.Dispose()
    }
}

function Read-JsonFromZip {
    param(
        [Parameter(Mandatory = $true)][string]$ZipPath,
        [Parameter(Mandatory = $true)][string]$EntryName
    )

    $raw = Read-ZipEntryText -ZipPath $ZipPath -EntryName $EntryName
    if ([string]::IsNullOrWhiteSpace($raw)) {
        return $null
    }

    try {
        return $raw | ConvertFrom-Json
    } catch {
        return $null
    }
}

function Resolve-PackageHash {
    param([Parameter(Mandatory = $true)][string]$ZipPath)

    $shaPath = [System.IO.Path]::ChangeExtension($ZipPath, ".sha256.txt")
    if (Test-Path -LiteralPath $shaPath) {
        $line = (Get-Content -LiteralPath $shaPath -TotalCount 1).Trim()
        if ($line -match "^\s*([0-9a-fA-F]{64})\b") {
            return $matches[1].ToUpperInvariant()
        }
    }

    return (Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Resolve-PackageVersion {
    param(
        [Parameter(Mandatory = $true)][string]$ZipBaseName,
        [Parameter(Mandatory = $true)][string]$GameId,
        $PackageMetadata
    )

    $fromMetadata = Trim-Text (Get-NestedValue -Object $PackageMetadata -Path @("package_version"))
    if (-not [string]::IsNullOrWhiteSpace($fromMetadata)) {
        return $fromMetadata
    }

    $prefix = "$GameId-"
    if ($ZipBaseName.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $ZipBaseName.Substring($prefix.Length)
    }

    return $ZipBaseName
}

function Build-ManifestPayload {
    param(
        [Parameter(Mandatory = $true)][string]$GameId,
        [Parameter(Mandatory = $true)][string]$DisplayName,
        [Parameter(Mandatory = $true)][string]$Version,
        $ModuleManifest
    )

    $description = Trim-Text (Get-NestedValue -Object $ModuleManifest -Path @("description"))
    if ([string]::IsNullOrWhiteSpace($description)) {
        $description = "Juego live distribuido por Nisoje Studio"
    }

    return [pscustomobject]@{
        gameId = $GameId
        displayName = $DisplayName
        version = $Version
        description = $description
        author = "Nisoje Studio"
        capabilities = @()
    }
}

if ([string]::IsNullOrWhiteSpace($PackagesRoot)) {
    $PackagesRoot = Join-Path $env:USERPROFILE "Desktop\\Juegos\\_packages"
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path (Get-Location).Path "latest.json"
}

$packagesRootResolved = Resolve-NormalizedPath $PackagesRoot
$outputPathResolved = Resolve-NormalizedPath $OutputPath

if (-not (Test-Path -LiteralPath $packagesRootResolved)) {
    throw "PackagesRoot no existe: $packagesRootResolved"
}

Ensure-Directory -PathValue (Split-Path -Parent $outputPathResolved)

$zipFiles = Get-ChildItem -LiteralPath $packagesRootResolved -Directory | ForEach-Object {
    Get-ChildItem -LiteralPath $_.FullName -File -Filter *.zip
} | Sort-Object FullName

if ($zipFiles.Count -eq 0) {
    throw "No se encontraron paquetes ZIP en $packagesRootResolved"
}

$games = New-Object System.Collections.Generic.List[object]
foreach ($zipFile in $zipFiles) {
    $zipPath = $zipFile.FullName
    $zipBaseName = [System.IO.Path]::GetFileNameWithoutExtension($zipFile.Name)
    $packageMetadata = Read-JsonFromZip -ZipPath $zipPath -EntryName "package_metadata.json"
    $moduleManifest = Read-JsonFromZip -ZipPath $zipPath -EntryName "module_manifest.json"

    $gameId = Trim-Text (Get-NestedValue -Object $packageMetadata -Path @("game", "id"))
    if ([string]::IsNullOrWhiteSpace($gameId)) {
        $gameId = Trim-Text (Get-NestedValue -Object $moduleManifest -Path @("id"))
    }
    if ([string]::IsNullOrWhiteSpace($gameId)) {
        $gameId = Trim-Text (Split-Path -Leaf $zipFile.DirectoryName)
    }
    if ([string]::IsNullOrWhiteSpace($gameId)) {
        throw "No se pudo resolver game_id para $zipPath"
    }

    $version = Resolve-PackageVersion -ZipBaseName $zipBaseName -GameId $gameId -PackageMetadata $packageMetadata
    $displayName = Trim-Text (Get-NestedValue -Object $packageMetadata -Path @("game", "display_name"))
    if ([string]::IsNullOrWhiteSpace($displayName)) {
        $displayName = Trim-Text (Get-NestedValue -Object $moduleManifest -Path @("displayName"))
    }
    if ([string]::IsNullOrWhiteSpace($displayName)) {
        $displayName = $gameId
    }

    $sha256 = Resolve-PackageHash -ZipPath $zipPath
    $shaFileName = [System.IO.Path]::ChangeExtension($zipFile.Name, ".sha256.txt")
    $normalizedPrefix = (Trim-Text $R2Prefix).Trim("/").Replace("\", "/")

    $games.Add([pscustomobject]@{
        game_id = $gameId
        display_name = $displayName
        version = $version
        package_path = "$normalizedPrefix/$gameId/$version/$($zipFile.Name)"
        sha256_path = "$normalizedPrefix/$gameId/$version/$shaFileName"
        sha256 = $sha256
        manifest = (Build-ManifestPayload -GameId $gameId -DisplayName $displayName -Version $version -ModuleManifest $moduleManifest)
    })
}

$latestGames = @(
    $games.ToArray() |
        Group-Object game_id |
        ForEach-Object {
            $_.Group | Sort-Object version -Descending | Select-Object -First 1
        } |
        Sort-Object game_id
)

$catalog = [ordered]@{
    generated_at = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
    games = $latestGames
}

$json = $catalog | ConvertTo-Json -Depth 8
$utf8 = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($outputPathResolved, ($json + [Environment]::NewLine), $utf8)

Write-Host "Catalogo generado: $outputPathResolved"
