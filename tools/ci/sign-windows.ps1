# File: tools/ci/sign-windows.ps1
# Purpose: Authenticode-sign the given files with signtool, using a certificate and password taken from the environment
# Author: Akilesh
# Date: 2026-11-05
#
# Usage: pwsh tools/ci/sign-windows.ps1 [-NoVerify] <file>...
# Environment (never pass these on a command line you log):
#   CROSSPORT_SIGN_PFX        path of the .pfx file holding the code signing certificate and key
#   CROSSPORT_SIGN_PASSWORD   its password (empty when the file has none)
#   CROSSPORT_SIGN_TIMESTAMP  RFC 3161 timestamp service, one of the allowlisted hosts below (default: DigiCert)
# -NoVerify skips the final trust check; use it only for a self-signed test certificate.
param(
    [switch]$NoVerify,
    [Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)][string[]]$Files
)
$ErrorActionPreference = 'Stop'

$AllowedTimestampHosts = @('timestamp.digicert.com', 'timestamp.sectigo.com', 'timestamp.globalsign.com')
$DefaultTimestamp = 'http://timestamp.digicert.com'

function Get-TimestampUrl {
    $url = $env:CROSSPORT_SIGN_TIMESTAMP
    if (-not $url) { return $DefaultTimestamp }
    $uri = [Uri]$url
    $isAllowed = ($uri.Scheme -in @('http', 'https')) -and ($AllowedTimestampHosts -contains $uri.Host) -and (-not $uri.UserInfo)
    if (-not $isAllowed) { throw "timestamp service $($uri.Host) is not on the allowlist" }
    return $url
}

function Get-SignTool {
    $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $found = Get-ChildItem -Path $kits -Filter signtool.exe -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '[\\/]x64[\\/]' } | Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $found) { throw 'signtool.exe was not found (install the Windows SDK)' }
    return $found.FullName
}

$timestamp = Get-TimestampUrl
if (-not $env:CROSSPORT_SIGN_PFX -or -not (Test-Path -LiteralPath $env:CROSSPORT_SIGN_PFX -PathType Leaf)) {
    throw 'CROSSPORT_SIGN_PFX must name the certificate file'
}
$signtool = Get-SignTool
foreach ($file in $Files) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "cannot sign ${file}: no such file" }
    & $signtool sign /fd SHA256 /td SHA256 /tr $timestamp /f $env:CROSSPORT_SIGN_PFX /p $env:CROSSPORT_SIGN_PASSWORD $file | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "signtool failed on $file" }
    if (-not $NoVerify) {
        & $signtool verify /pa /q $file
        if ($LASTEXITCODE -ne 0) { throw "the signature on $file does not verify" }
    }
    Write-Host "signed $file"
}
