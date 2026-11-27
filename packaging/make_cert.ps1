# Creates a self-signed code signing certificate for BerryTrip (TEST / SELF-SIGNED ONLY).
# A self-signed certificate is NOT trusted by other machines and does not remove SmartScreen
# warnings unless the user installs the .cer into Trusted Publishers first.
param(
    [string]$OutDir = (Join-Path $PSScriptRoot 'codesign'),
    [string]$Password = 'berrytrip',
    [string]$Subject = 'CN=BerryTrip, O=BerryTrip, C=RU',
    [int]$Years = 5
)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$cert = New-SelfSignedCertificate `
    -Type CodeSigningCert `
    -Subject $Subject `
    -KeyUsage DigitalSignature `
    -KeyAlgorithm RSA `
    -KeyLength 3072 `
    -HashAlgorithm SHA256 `
    -FriendlyName 'BerryTrip self-signed code signing' `
    -CertStoreLocation 'Cert:\CurrentUser\My' `
    -NotAfter (Get-Date).AddYears($Years)

$pfx = Join-Path $OutDir 'berrytrip-codesign.pfx'
$cer = Join-Path $OutDir 'berrytrip-codesign.cer'
$secure = ConvertTo-SecureString -String $Password -Force -AsPlainText

Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $secure | Out-Null
Export-Certificate -Cert $cert -FilePath $cer | Out-Null
$cert.Thumbprint | Set-Content -Encoding ASCII (Join-Path $OutDir 'thumbprint.txt')

Write-Host "Certificate created."
Write-Host "  subject   : $($cert.Subject)"
Write-Host "  thumbprint: $($cert.Thumbprint)"
Write-Host "  pfx       : $pfx (password: $Password)"
Write-Host "  cer       : $cer"
Write-Host ""
Write-Host "This certificate is self-signed: other computers will still warn until they trust it."
