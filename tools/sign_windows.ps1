# Plan 33b Task 5: optional Authenticode signing hook for rr-game.exe and the
# installer. It does nothing (exit 0) unless the owner configures one of:
#   RR_SIGN_CERT_THUMBPRINT      SHA-1 thumbprint of a certificate in the
#                                current user's or machine's store
#   RR_SIGN_AZURE_DLIB and RR_SIGN_AZURE_METADATA
#                                Azure Trusted Signing: the dlib path and the
#                                metadata.json (endpoint, account, profile)
# RR_SIGN_TIMESTAMP_URL overrides the RFC 3161 timestamp server. Certificates
# and passwords stay with the owner; nothing here stores or prints them.
#   pwsh -NoProfile -File tools/sign_windows.ps1 <file> [<file> ...]
# `-Check` exits 0 when signing is configured and 1 when it is not.
param(
    [switch]$Check,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Files
)
$ErrorActionPreference = 'Stop'

$thumbprint = $env:RR_SIGN_CERT_THUMBPRINT
$azureDlib = $env:RR_SIGN_AZURE_DLIB
$azureMetadata = $env:RR_SIGN_AZURE_METADATA
$configured = [bool]$thumbprint -or ([bool]$azureDlib -and [bool]$azureMetadata)

if ($Check) {
    if ($configured) { exit 0 } else { exit 1 }
}
if (-not $configured) {
    Write-Output 'signing skipped: no RR_SIGN_* configuration'
    exit 0
}
if (-not $Files -or $Files.Count -eq 0) {
    throw 'sign_windows.ps1: no files to sign'
}

function Find-SignTool {
    $command = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $arch = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'x64' }
    $found = Get-ChildItem -Path $kits -Filter signtool.exe -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.Directory.Name -eq $arch } | Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $found) { throw 'signtool.exe not found (install the Windows SDK)' }
    return $found.FullName
}

$signtool = Find-SignTool
if ($thumbprint) {
    $timestamp = if ($env:RR_SIGN_TIMESTAMP_URL) { $env:RR_SIGN_TIMESTAMP_URL } else { 'http://timestamp.digicert.com' }
    $arguments = @('sign', '/sha1', $thumbprint, '/fd', 'sha256', '/tr', $timestamp, '/td', 'sha256')
} else {
    $timestamp = if ($env:RR_SIGN_TIMESTAMP_URL) { $env:RR_SIGN_TIMESTAMP_URL } else { 'http://timestamp.acs.microsoft.com' }
    $arguments = @('sign', '/fd', 'sha256', '/tr', $timestamp, '/td', 'sha256', '/dlib', $azureDlib, '/dmdf', $azureMetadata)
}
foreach ($file in $Files) {
    & $signtool @arguments $file
    if ($LASTEXITCODE -ne 0) { throw "signtool sign failed for $file ($LASTEXITCODE)" }
    & $signtool verify /pa $file
    if ($LASTEXITCODE -ne 0) { throw "signtool verify failed for $file ($LASTEXITCODE)" }
    Write-Output "signed: $file"
}
