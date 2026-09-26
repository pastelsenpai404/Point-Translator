[CmdletBinding()]
param([switch]$Install, [string]$Python = '')
$ErrorActionPreference = 'Stop'
$venv = Join-Path $PSScriptRoot '.laya-venv'
$runtime = Join-Path $venv 'Scripts/python.exe'
$bridge = Join-Path $PSScriptRoot 'laya/bridge.py'

function Test-LayaService {
    try {
        $health = Invoke-RestMethod 'http://127.0.0.1:18766/health' -TimeoutSec 2
        if ($health.service -ne 'point-translator-laya') {
            throw 'Port 18766 is in use by a different service.'
        }
        return $true
    }
    catch [System.Net.WebException] {
        return $false
    }
}

if (-not (Test-Path -LiteralPath $bridge)) { throw "Laya bridge is missing: $bridge" }
$installed = $false
if (Test-Path -LiteralPath $runtime) {
    & $runtime -I -c 'import laya' 2>$null
    $installed = $LASTEXITCODE -eq 0
}

if ($Install -or -not $installed) {
    if (-not (Test-Path -LiteralPath $runtime)) {
        if (-not $Python) {
            if (Get-Command py.exe -ErrorAction SilentlyContinue) {
                $Python = 'py'
            }
            elseif (Test-Path -LiteralPath (Join-Path $PSScriptRoot '.argos-venv/Scripts/python.exe')) {
                $Python = Join-Path $PSScriptRoot '.argos-venv/Scripts/python.exe'
            }
            elseif (Get-Command python.exe -ErrorAction SilentlyContinue) {
                $Python = 'python'
            }
            else {
                throw 'ต้องมี Python 3.10 ขึ้นไปเพื่อติดตั้ง Laya'
            }
        }
        Write-Host 'กำลังสร้างสภาพแวดล้อม Python สำหรับ Laya...' -ForegroundColor Cyan
        if ($Python -eq 'py') { & $Python -3 -m venv $venv }
        else { & $Python -m venv $venv }
        if ($LASTEXITCODE -ne 0) {
            throw 'สร้างสภาพแวดล้อม Laya ไม่สำเร็จ ต้องใช้ Python 3.10 ขึ้นไป'
        }
    }
    Write-Host 'กำลังติดตั้ง Laya (ครั้งแรกอาจใช้เวลาหลายนาที)...' -ForegroundColor Cyan
    & $runtime -m pip install 'laya==0.3.20'
    if ($LASTEXITCODE -ne 0) { throw 'ติดตั้ง Laya ไม่สำเร็จ' }
    & $runtime -I -c 'import laya' 2>$null
    if ($LASTEXITCODE -ne 0) { throw 'ติดตั้ง Laya แล้วแต่ยังนำเข้าแพ็กเกจไม่ได้' }
}

if (Test-LayaService) { return }
Start-Process -FilePath $runtime -ArgumentList ('"' + $bridge + '"') -WindowStyle Hidden
for ($attempt = 0; $attempt -lt 60; $attempt++) {
    Start-Sleep -Milliseconds 500
    if (Test-LayaService) {
        Write-Output 'Laya started on this computer (127.0.0.1:18766).'
        return
    }
}
throw 'Laya ติดตั้งแล้วแต่บริการไม่เริ่มทำงาน กรุณาตรวจสอบ Python และพอร์ต 18766'
