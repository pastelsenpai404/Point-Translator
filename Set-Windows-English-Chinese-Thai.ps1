[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# Relaunch as administrator when needed.
if (-not (Test-IsAdministrator)) {
    Write-Host 'Administrator permission is required. Opening the UAC prompt...' -ForegroundColor Yellow
    $arguments = @(
        '-NoProfile'
        '-ExecutionPolicy', 'Bypass'
        '-File', ('"{0}"' -f $PSCommandPath)
    )
    Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $arguments -Wait
    exit $LASTEXITCODE
}

try {
    Write-Host 'Checking the English (United States) display language...' -ForegroundColor Cyan

    # Install the full UI language pack when Windows exposes the Windows 11
    # LanguagePackManagement cmdlets. Install-Language is safe to call only
    # when the language is not already present.
    $installLanguage = Get-Command -Name Install-Language -ErrorAction SilentlyContinue
    $getInstalledLanguage = Get-Command -Name Get-InstalledLanguage -ErrorAction SilentlyContinue

    if ($installLanguage -and $getInstalledLanguage) {
        $englishInstalled = Get-InstalledLanguage | Where-Object LanguageId -eq 'en-US'
        if (-not $englishInstalled) {
            Write-Host 'Downloading and installing the en-US language pack. This can take several minutes...'
            Install-Language -Language 'en-US'
        }
        else {
            Write-Host 'The en-US language pack is already installed.' -ForegroundColor Green
        }
    }
    else {
        Write-Warning 'This Windows version does not provide Install-Language. The script will use an existing en-US language pack.'
    }

    Write-Host 'Setting Windows display language to English (United States)...' -ForegroundColor Cyan
    Set-WinUILanguageOverride -Language 'en-US'
    Set-Culture -CultureInfo 'en-US'

    # Windows 11 also has a machine-wide preferred UI language setting.
    $setSystemUiLanguage = Get-Command -Name Set-SystemPreferredUILanguage -ErrorAction SilentlyContinue
    if ($setSystemUiLanguage) {
        Set-SystemPreferredUILanguage -Language 'en-US'
    }

    Write-Host 'Keeping only Chinese (Microsoft Pinyin) and Thai keyboards...' -ForegroundColor Cyan
    $languageList = New-WinUserLanguageList -Language 'zh-CN'
    $languageList.Add('th-TH')
    Set-WinUserLanguageList -LanguageList $languageList -Force

    Write-Host ''
    Write-Host 'Completed successfully.' -ForegroundColor Green
    Write-Host 'Display language: English (United States)'
    Write-Host 'Input languages: Chinese (Simplified, Microsoft Pinyin) and Thai'
    Write-Host ''
    Write-Host 'Sign out and sign back in (or restart Windows) to apply every change.' -ForegroundColor Yellow
}
catch {
    Write-Host ''
    Write-Host 'The setup could not be completed:' -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    Write-Host ''
    Write-Host 'Note: Windows Home Single Language editions may not allow changing the display language.' -ForegroundColor Yellow
    exit 1
}
finally {
    Write-Host ''
    Read-Host 'Press Enter to close this window'
}
