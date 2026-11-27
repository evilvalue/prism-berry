# Uploads BerryTrip builds to VirusTotal and prints the per-engine verdicts.
#
# The API key is read from (in this order):
#   1. -ApiKey parameter
#   2. $env:VT_API_KEY
#   3. file .vtkey in the repository root (one line, nothing else) - gitignored
#
# Get a free key at https://www.virustotal.com/gui/user/apikey (account required).
# The key is never echoed and never written anywhere.
param(
    [string]$ApiKey,
    [string[]]$Files,
    [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Net.Http
$root = Split-Path -Parent $PSScriptRoot

if (-not $ApiKey) { $ApiKey = $env:VT_API_KEY }
$keyFile = Join-Path $root '.vtkey'
if (-not $ApiKey -and (Test-Path $keyFile)) {
    $ApiKey = (Get-Content -Raw $keyFile).Trim()
}
if (-not $ApiKey) {
    throw "No VirusTotal API key. Save it to $keyFile or set VT_API_KEY."
}

if (-not $Files -or $Files.Count -eq 0) {
    $Files = @(
        (Join-Path $root 'logs\BerryTrip-Setup-0.3.1.exe'),
        (Join-Path $root 'dist\berrytrip.exe')
    ) | Where-Object { Test-Path $_ }
}
if (-not $Files) { throw 'No files to upload.' }

$client = New-Object System.Net.Http.HttpClient
$client.DefaultRequestHeaders.Add('x-apikey', $ApiKey)
$client.DefaultRequestHeaders.Add('accept', 'application/json')

$results = @()
foreach ($path in $Files) {
    $name = Split-Path -Leaf $path
    Write-Host "Uploading $name ..."

    $bytes = [System.IO.File]::ReadAllBytes($path)
    $multipart = New-Object System.Net.Http.MultipartFormDataContent
    $fileContent = New-Object System.Net.Http.ByteArrayContent($bytes, 0, $bytes.Length)
    $fileContent.Headers.ContentType = [System.Net.Http.Headers.MediaTypeHeaderValue]::Parse('application/octet-stream')
    $multipart.Add($fileContent, 'file', $name)

    $response = $client.PostAsync('https://www.virustotal.com/api/v3/files', $multipart).Result
    $body = $response.Content.ReadAsStringAsync().Result
    if (-not $response.IsSuccessStatusCode) { throw "Upload failed for $name : $body" }

    $analysisId = ($body | ConvertFrom-Json).data.id
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        Start-Sleep -Seconds 5
        $analysis = Invoke-RestMethod -Uri "https://www.virustotal.com/api/v3/analyses/$analysisId" -Headers @{ 'x-apikey' = $ApiKey }
        $status = $analysis.data.attributes.status
    } while ($status -ne 'completed' -and (Get-Date) -lt $deadline)

    $stats = $analysis.data.attributes.stats
    $hash = $analysis.meta.file_info.sha256
    $link = "https://www.virustotal.com/gui/file/$hash"

    Write-Host ("  status : {0}" -f $status)
    Write-Host ("  sha256 : {0}" -f $hash)
    Write-Host ("  verdict: {0} malicious, {1} suspicious, {2} undetected" -f $stats.malicious, $stats.suspicious, $stats.undetected)
    Write-Host ("  report : {0}" -f $link)

    $flagged = @()
    foreach ($prop in $analysis.data.attributes.results.PSObject.Properties) {
        $category = $prop.Value.category
        if ($category -eq 'malicious' -or $category -eq 'suspicious') {
            $flagged += ("{0} ({1}): {2}" -f $prop.Name, $category, $prop.Value.result)
        }
    }
    if ($flagged) { $flagged | ForEach-Object { Write-Host "  ! $_" } }

    $results += [pscustomobject]@{
        file      = $name
        status    = $status
        sha256    = $hash
        link      = $link
        malicious = $stats.malicious
        suspicious= $stats.suspicious
        undetected= $stats.undetected
        flagged   = $flagged
    }
}

$out = Join-Path $root 'logs\vt-report.json'
$results | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 $out
Write-Host "Saved $out"
