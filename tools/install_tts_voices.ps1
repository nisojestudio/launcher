param(
    [string[]]$Locales = @('es-ES', 'es-MX', 'es-US'),
    [switch]$IncludeSpeechRecognition
)

$ErrorActionPreference = 'Stop'

function Test-IsAdmin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Write-Section($text) {
    Write-Host ""
    Write-Host "== $text ==" -ForegroundColor Cyan
}

function Ensure-Capability {
    param(
        [Parameter(Mandatory = $true)][string]$CapabilityName
    )

    $cap = Get-WindowsCapability -Online -Name $CapabilityName
    if (-not $cap) {
        Write-Warning "Capability no encontrada: $CapabilityName"
        return
    }

    if ($cap.State -eq 'Installed') {
        Write-Host "Ya instalado: $CapabilityName" -ForegroundColor DarkGreen
        return
    }

    Write-Host "Instalando: $CapabilityName" -ForegroundColor Yellow
    Add-WindowsCapability -Online -Name $CapabilityName | Out-Null
}

if (-not (Test-IsAdmin)) {
    throw "Este script debe ejecutarse como Administrador."
}

Write-Section "Sistema"
Get-ComputerInfo | Select-Object WindowsProductName, WindowsVersion, OsVersion | Format-List

Write-Section "Instalando voces"
foreach ($locale in $Locales) {
    Write-Host "Locale: $locale" -ForegroundColor Cyan
    Ensure-Capability -CapabilityName ("Language.Basic~~~{0}~0.0.1.0" -f $locale)
    Ensure-Capability -CapabilityName ("Language.TextToSpeech~~~{0}~0.0.1.0" -f $locale)
    if ($IncludeSpeechRecognition) {
        Ensure-Capability -CapabilityName ("Language.Speech~~~{0}~0.0.1.0" -f $locale)
    }
}

Write-Section "Idiomas del usuario"
Get-WinUserLanguageList | Format-List

Write-Section "Voces Desktop"
$desktopVoices = Get-ChildItem 'HKLM:\SOFTWARE\Microsoft\Speech\Voices\Tokens' -ErrorAction SilentlyContinue |
    ForEach-Object {
        [PSCustomObject]@{
            Source = 'Desktop'
            Name = $_.PSChildName
            Description = $_.GetValue('')
        }
    }
$desktopVoices | Format-Table -AutoSize

Write-Section "Voces OneCore"
$oneCoreVoices = Get-ChildItem 'HKLM:\SOFTWARE\Microsoft\Speech_OneCore\Voices\Tokens' -ErrorAction SilentlyContinue |
    ForEach-Object {
        [PSCustomObject]@{
            Source = 'OneCore'
            Name = $_.PSChildName
            Description = $_.GetValue('')
        }
    }
$oneCoreVoices | Format-Table -AutoSize

Write-Section ".NET SpeechSynthesizer"
Add-Type -AssemblyName System.Speech
$synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
$synth.GetInstalledVoices() |
    ForEach-Object {
        $info = $_.VoiceInfo
        [PSCustomObject]@{
            Name = $info.Name
            Culture = $info.Culture
            Gender = $info.Gender
            Description = $info.Description
        }
    } | Format-Table -AutoSize

Write-Section "Fin"
Write-Host "Reinicia Nisoje Studio despues de instalar voces nuevas." -ForegroundColor Green
