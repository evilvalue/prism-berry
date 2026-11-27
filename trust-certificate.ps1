# Adds the BerryTrip self-signed certificate to the CURRENT USER's stores so that
# Windows stops showing "Unknown publisher" for the signed launcher and installer.
#
# Only run this if you trust the source of berrytrip-codesign.cer. The certificate is
# self-signed: adding it to Root lets its holder sign anything in your name.
# Run from an unpacked release next to berrytrip-codesign.cer, no admin needed.
$ErrorActionPreference = 'Stop'
$cer = Join-Path $PSScriptRoot 'berrytrip-codesign.cer'
if (-not (Test-Path $cer)) {
    $cer = Join-Path (Split-Path -Parent $PSScriptRoot) 'berrytrip-codesign.cer'
}
if (-not (Test-Path $cer)) {
    throw "berrytrip-codesign.cer not found next to this script."
}

Write-Host "Trusting $cer for the current user..."
certutil -user -addstore Root "$cer"
certutil -user -addstore TrustedPublisher "$cer"

Write-Host ""
Write-Host "Done. Signed BerryTrip files will now verify for this Windows account."
Write-Host "To undo: certutil -user -delstore Root <thumbprint>"
