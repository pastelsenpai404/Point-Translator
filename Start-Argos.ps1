[CmdletBinding()]
param([switch]$Install, [string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$venv = Join-Path $PSScriptRoot '.argos-venv'
$runtime = Join-Path $venv 'Scripts/python.exe'
$bridge = Join-Path $PSScriptRoot 'argos/bridge.py'
if ($Install) {
    if (!(Test-Path -LiteralPath $runtime)) {
        & $Python -m venv $venv
        if ($LASTEXITCODE -ne 0) { throw 'Python 3.10+ is required. Pass -Python with its full path.' }
    }
    & $runtime -m pip install 'argostranslate==1.11.0' pypinyin
    if ($LASTEXITCODE -ne 0) { throw 'Argos installation failed.' }
    & $runtime $bridge --install
    if ($LASTEXITCODE -ne 0) { throw 'Language model download failed.' }
}
if (!(Test-Path -LiteralPath $runtime)) { throw 'Run Start-Argos.ps1 -Install first.' }
try {
    $health = Invoke-RestMethod 'http://127.0.0.1:18765/' -TimeoutSec 2
    if ($health.service -eq 'point-translator-argos') { return }
    throw 'Port 18765 is in use by a different service.'
} catch [System.Net.WebException] { }
Start-Process -FilePath $runtime -ArgumentList ('"' + $bridge + '"') -WindowStyle Hidden
Write-Output 'Argos started on this computer (127.0.0.1:18765).'
