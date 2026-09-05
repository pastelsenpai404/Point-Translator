[CmdletBinding()]
param(
    [switch]$Rebuild,
    [switch]$InstallBuildTools,
    [switch]$ConfigureApiKey,
    [switch]$UseLocalAI
)

$ErrorActionPreference = 'Stop'

function Find-CMakeExecutable {
    $command = Get-Command -Name 'cmake.exe' -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vsWhere) {
        $installationPath = & $vsWhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.CMake.Project `
            -property installationPath

        if ($installationPath) {
            $bundledCMake = Join-Path $installationPath.Trim() `
                'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (Test-Path -LiteralPath $bundledCMake) {
                return $bundledCMake
            }
        }
    }

    return $null
}

function Find-OllamaExecutable {
    $command = Get-Command -Name 'ollama.exe' -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $knownLocations = @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Ollama\ollama.exe'),
        (Join-Path $env:ProgramFiles 'Ollama\ollama.exe')
    )
    foreach ($location in $knownLocations) {
        if (Test-Path -LiteralPath $location) {
            return $location
        }
    }

    return $null
}

function Test-OllamaServer {
    try {
        Invoke-WebRequest -Uri 'http://127.0.0.1:11434/api/tags' `
            -UseBasicParsing -TimeoutSec 2 | Out-Null
        return $true
    }
    catch {
        return $false
    }
}

function Start-OllamaServer {
    param([Parameter(Mandatory)][string]$OllamaExecutable)

    if (Test-OllamaServer) {
        return
    }

    Start-Process -FilePath $OllamaExecutable -ArgumentList 'serve' -WindowStyle Hidden
    # A first launch immediately after installation may need extra time to
    # initialize Ollama's local database and background application.
    for ($attempt = 0; $attempt -lt 90; $attempt++) {
        Start-Sleep -Milliseconds 500
        if (Test-OllamaServer) {
            return
        }
    }

    throw 'เปิด Ollama local server ไม่สำเร็จ'
}

try {
    $projectDirectory = Split-Path -Parent $PSCommandPath
    $buildDirectory = Join-Path $projectDirectory 'build'
    $rebuildDirectory = Join-Path $projectDirectory 'build-rebuild'
    $executable = Join-Path $buildDirectory 'Release\ThaiKaraokeOverlay.exe'
    $exampleConfig = Join-Path $projectDirectory 'config.example.ini'
    $runtimeConfig = Join-Path (Split-Path -Parent $executable) 'config.ini'

    if ($UseLocalAI) {
        # Ensure the executable includes the local-provider compatibility path.
        $Rebuild = $true
        $ollama = Find-OllamaExecutable
        if (-not $ollama) {
            $winget = Get-Command -Name 'winget.exe' -ErrorAction SilentlyContinue
            if (-not $winget) {
                throw 'ไม่พบ winget จึงไม่สามารถติดตั้ง Ollama อัตโนมัติได้'
            }

            Write-Host 'กำลังติดตั้ง Ollama สำหรับรัน AI ภายในเครื่อง...' -ForegroundColor Cyan
            & $winget.Source install --id 'Ollama.Ollama' --exact --source 'winget' `
                --accept-package-agreements --accept-source-agreements
            if ($LASTEXITCODE -ne 0) {
                throw "ติดตั้ง Ollama ไม่สำเร็จ (exit code $LASTEXITCODE)"
            }

            $ollama = Find-OllamaExecutable
            if (-not $ollama) {
                throw 'ติดตั้ง Ollama แล้วแต่ยังไม่พบ ollama.exe กรุณาเปิด PowerShell ใหม่แล้วรันคำสั่งเดิมอีกครั้ง'
            }
        }

        Start-OllamaServer -OllamaExecutable $ollama

        Write-Host 'กำลังดาวน์โหลดโมเดล qwen3:4b-instruct (ประมาณ 2.5 GB)...' -ForegroundColor Cyan
        & $ollama pull 'qwen3:4b-instruct'
        if ($LASTEXITCODE -ne 0) {
            throw "ดาวน์โหลดโมเดล qwen3:4b-instruct ไม่สำเร็จ (exit code $LASTEXITCODE)"
        }
    }

    if ($ConfigureApiKey) {
        Write-Host 'กรอก OpenAI API key (ตัวอักษรจะไม่แสดงบนหน้าจอ)' -ForegroundColor Cyan
        $secureKey = Read-Host -Prompt 'API key' -AsSecureString
        $keyPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureKey)
        try {
            $plainKey = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($keyPointer)
            if ([string]::IsNullOrWhiteSpace($plainKey)) {
                throw 'ไม่ได้กรอก API key'
            }

            [Environment]::SetEnvironmentVariable(
                'THAI_OVERLAY_API_KEY',
                $plainKey,
                [EnvironmentVariableTarget]::User
            )
            $env:THAI_OVERLAY_API_KEY = $plainKey
            Write-Host 'บันทึก API key ใน User environment variable แล้ว' -ForegroundColor Green
        }
        finally {
            if ($keyPointer -ne [IntPtr]::Zero) {
                [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($keyPointer)
            }
            Remove-Variable -Name plainKey, secureKey -ErrorAction SilentlyContinue
        }
    }

    if ($Rebuild -or -not (Test-Path -LiteralPath $executable)) {
        $cmakeBuildDirectory = if ($Rebuild) { $rebuildDirectory } else { $buildDirectory }
        $cmake = Find-CMakeExecutable
        if (-not $cmake) {
            if (-not $InstallBuildTools) {
                throw @"
ยังไม่พบโปรแกรม ThaiKaraokeOverlay.exe และไม่พบ CMake

เครื่องนี้มี winget พร้อมใช้งาน ให้รันคำสั่งต่อไปนี้เพื่อติดตั้ง
Visual Studio 2022 Build Tools พร้อมเครื่องมือ C++ ที่จำเป็น:

  .\Run-Thai-Karaoke-Overlay.ps1 -InstallBuildTools

การติดตั้งมีขนาดค่อนข้างใหญ่และ Windows จะแสดงหน้าต่าง UAC
"@
            }

            $winget = Get-Command -Name 'winget.exe' -ErrorAction SilentlyContinue
            if (-not $winget) {
                throw 'ไม่พบ winget จึงไม่สามารถติดตั้ง Visual Studio Build Tools อัตโนมัติได้'
            }

            Write-Host 'กำลังติดตั้ง Visual Studio 2022 Build Tools และ C++ workload...' -ForegroundColor Cyan
            Write-Host 'ขั้นตอนนี้อาจใช้เวลาหลายนาทีและต้องอนุญาตหน้าต่าง UAC' -ForegroundColor Yellow

            & $winget.Source install --id 'Microsoft.VisualStudio.2022.BuildTools' `
                --exact --source 'winget' `
                --accept-package-agreements --accept-source-agreements `
                --override '--passive --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'

            if ($LASTEXITCODE -ne 0) {
                throw "การติดตั้ง Visual Studio Build Tools ไม่สำเร็จ (exit code $LASTEXITCODE)"
            }

            $cmake = Find-CMakeExecutable
            if (-not $cmake) {
                throw 'ติดตั้งเสร็จแล้วแต่ยังไม่พบ CMake กรุณารีสตาร์ต PowerShell แล้วรันสคริปต์อีกครั้ง'
            }
        }

        if ($Rebuild) {
            Write-Host 'กำลัง build รุ่นใหม่ โดยให้ Overlay ตัวเดิมทำงานต่อไปก่อน...' -ForegroundColor Cyan
        }
        else {
            Write-Host 'กำลังเตรียมโปรเจกต์ C++...' -ForegroundColor Cyan
        }
        & $cmake -S $projectDirectory -B $cmakeBuildDirectory `
            -G 'Visual Studio 17 2022' -A 'x64'
        if ($LASTEXITCODE -ne 0) {
            throw "CMake configure ไม่สำเร็จ (exit code $LASTEXITCODE)"
        }

        Write-Host 'กำลัง build โปรแกรม...' -ForegroundColor Cyan
        & $cmake --build $cmakeBuildDirectory --config 'Release'
        if ($LASTEXITCODE -ne 0) {
            throw "C++ build ไม่สำเร็จ (exit code $LASTEXITCODE)"
        }

        if ($Rebuild) {
            $rebuiltExecutable = Join-Path $rebuildDirectory 'Release\ThaiKaraokeOverlay.exe'
            if (-not (Test-Path -LiteralPath $rebuiltExecutable)) {
                throw "Build สำเร็จแต่ไม่พบโปรแกรมรุ่นใหม่: $rebuiltExecutable"
            }

            $overlayProcesses = @(Get-Process -Name 'ThaiKaraokeOverlay' -ErrorAction SilentlyContinue)
            foreach ($overlayProcess in $overlayProcesses) {
                if ($overlayProcess.Path -eq $executable) {
                    Write-Host 'Build สำเร็จแล้ว กำลังบังคับปิด Overlay ตัวเดิม...' -ForegroundColor Cyan
                    $overlayProcess | Stop-Process -Force
                    $overlayProcess | Wait-Process -ErrorAction SilentlyContinue
                }
            }

            $releaseDirectory = Split-Path -Parent $executable
            New-Item -ItemType Directory -Path $releaseDirectory -Force | Out-Null
            Copy-Item -LiteralPath $rebuiltExecutable -Destination $executable -Force
            Copy-Item -LiteralPath $exampleConfig `
                -Destination (Join-Path $releaseDirectory 'config.example.ini') -Force
            Write-Host 'นำโปรแกรมรุ่นใหม่มาแทนตัวเดิมเรียบร้อยแล้ว' -ForegroundColor Green
        }
    }

    if (-not (Test-Path -LiteralPath $executable)) {
        throw "ไม่พบไฟล์โปรแกรมหลัง build: $executable"
    }

    if (-not (Test-Path -LiteralPath $runtimeConfig)) {
        if (-not (Test-Path -LiteralPath $exampleConfig)) {
            throw "ไม่พบไฟล์ตั้งค่าตัวอย่าง: $exampleConfig"
        }

        Copy-Item -LiteralPath $exampleConfig -Destination $runtimeConfig
        Write-Host "สร้างไฟล์ตั้งค่าแล้ว: $runtimeConfig" -ForegroundColor Green
    }

    if ($UseLocalAI) {
        @(
            '# Thai Karaoke Overlay - local AI configuration'
            'api_base=http://127.0.0.1:11434'
            'model=qwen3:4b-instruct'
            'translate_hotkey=T'
            'ocr_hotkey=O'
            'quit_hotkey=Q'
            'ocr_language=auto'
            'show_original=true'
            'show_karaoke=true'
            'show_explanation=true'
            'show_word_breakdown=true'
            'overlay_opacity=96'
            'overlay_position=bottom'
            'auto_hide_seconds=0'
        ) | Set-Content -LiteralPath $runtimeConfig -Encoding Ascii
        Write-Host 'ตั้งค่า Overlay ให้ใช้ Ollama ภายในเครื่องแล้ว' -ForegroundColor Green
    }

    $localAIConfigured = Select-String -LiteralPath $runtimeConfig `
        -Pattern '^api_base=http://(localhost|127\.0\.0\.1):11434/?$' -Quiet

    if ($localAIConfigured -and -not $UseLocalAI) {
        $ollama = Find-OllamaExecutable
        if (-not $ollama) {
            throw 'config.ini ตั้งเป็น Local AI แต่ไม่พบ Ollama ให้รันสคริปต์พร้อม -UseLocalAI'
        }
        Start-OllamaServer -OllamaExecutable $ollama
    }

    if (-not $localAIConfigured -and -not $env:THAI_OVERLAY_API_KEY) {
        Write-Warning @"
ยังไม่ได้ตั้งค่า THAI_OVERLAY_API_KEY
หากใช้ OpenAI ให้ตั้งค่าด้วยคำสั่งนี้ แล้วเปิด PowerShell ใหม่:
  setx THAI_OVERLAY_API_KEY "your-api-key"

หากใช้เซิร์ฟเวอร์ local ที่ไม่ต้องใช้ API key สามารถข้ามข้อความนี้ได้
"@
    }

    $existingProcesses = @(Get-Process -Name 'ThaiKaraokeOverlay' -ErrorAction SilentlyContinue)
    foreach ($existingProcess in $existingProcesses) {
        if ($existingProcess.Path -eq $executable) {
            Write-Host 'พบ Overlay รุ่นเดิม กำลังบังคับปิดและเปิดใหม่...' -ForegroundColor Cyan
            $existingProcess | Stop-Process -Force
            $existingProcess | Wait-Process -ErrorAction SilentlyContinue
        }
        else {
            Write-Warning "พบโปรเซสชื่อ ThaiKaraokeOverlay จากตำแหน่งอื่น จึงไม่ปิด: $($existingProcess.Path)"
        }
    }

    Start-Process -FilePath $executable -WorkingDirectory (Split-Path -Parent $executable)

    Write-Host ''
    Write-Host 'เปิด Thai Karaoke Overlay เรียบร้อยแล้ว' -ForegroundColor Green
    Write-Host 'เลือกข้อความ แล้วกด Ctrl+Alt+T เพื่อแปล' -ForegroundColor Cyan
    Write-Host 'ข้อความในเกม/รูปภาพ กด Ctrl+Alt+O แล้วลากครอบข้อความ' -ForegroundColor Cyan
    Write-Host 'กด Esc เพื่อซ่อนหน้าต่าง และ Ctrl+Alt+Q เพื่อปิดโปรแกรม'
}
catch {
    Write-Host ''
    Write-Host 'ไม่สามารถเปิด Thai Karaoke Overlay ได้:' -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
