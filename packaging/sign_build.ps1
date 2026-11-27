# Signs BerryTrip binaries with Authenticode (SHA-256).
# Works with a .pfx file (self-signed or CA-issued) or a certificate thumbprint already
# installed in the current user's certificate store (SimplySign, USB token, etc.).
param(
    [string]$PfxPath,
    [string]$PfxPassword,
    [string]$Thumbprint,
    [string[]]$Files,
    [string]$TimestampUrl = '',
    [switch]$VerifyOnly
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if (-not $Files -or $Files.Count -eq 0) {
    $Files = @()
    $exe = Join-Path $root 'dist\berrytrip.exe'
    if (Test-Path $exe) { $Files += $exe }
    $setup = Get-ChildItem (Join-Path $root 'logs') -Filter 'BerryTrip-Setup-*.exe' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($setup) { $Files += $setup.FullName }
}

$Files = $Files | Where-Object { $_ -and (Test-Path $_) } | Select-Object -Unique
if (-not $Files) { throw 'Nothing to sign: build the launcher and the installer first.' }

function Show-Signatures($paths) {
    foreach ($p in $paths) {
        $sig = Get-AuthenticodeSignature -LiteralPath $p
        Write-Host ("{0}`n  status: {1}`n  signer: {2}" -f $p, $sig.Status, $sig.SignerCertificate.Subject)
    }
}

if ($VerifyOnly) {
    Show-Signatures $Files
    return
}

if (-not $Thumbprint -and -not $PfxPath) {
    throw 'Pass -PfxPath (and -PfxPassword) or -Thumbprint.'
}

$signtool = $null
$kits = @("${env:ProgramFiles(x86)}\Windows Kits\10\bin", "${env:ProgramFiles}\Windows Kits\10\bin")
foreach ($kit in $kits) {
    if (-not (Test-Path $kit)) { continue }
    $candidate = Get-ChildItem $kit -Filter signtool.exe -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\signtool\.exe$' } |
        Sort-Object FullName -Descending | Select-Object -First 1
    if ($candidate) { $signtool = $candidate.FullName; break }
}
if (-not $signtool) {
    $cmd = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($cmd) { $signtool = $cmd.Source }
}
if (-not $signtool) {
    throw 'signtool.exe not found. Install the Windows SDK (or add signtool to PATH).'
}

$signArgs = @('sign', '/fd', 'SHA256')
if ($TimestampUrl) { $signArgs += @('/td', 'SHA256', '/tr', $TimestampUrl) }
if ($Thumbprint) {
    $signArgs += @('/sha1', $Thumbprint)
} else {
    $signArgs += @('/f', (Resolve-Path $PfxPath))
    if ($PfxPassword) { $signArgs += @('/p', $PfxPassword) }
}

foreach ($file in $Files) {
    Write-Host "Signing $file"
    & $signtool @signArgs $file | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "signtool failed for $file (exit $LASTEXITCODE)" }
}

Show-Signatures $Files
