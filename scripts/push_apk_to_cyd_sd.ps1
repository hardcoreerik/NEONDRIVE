param(
  [string]$CydIp = "192.168.4.1",
  [string]$ApkPath = "C:\ESP32\CYDCompanion\app\build\outputs\apk\debug\app-debug.apk",
  [string]$DestDir = "/",
  [string]$DestName = "CYDCompanion.apk",
  [switch]$SkipDelete
)

$ErrorActionPreference = "Stop"

function Write-Step([string]$Text) {
  Write-Host "[*] $Text"
}

function Encode-Path([string]$PathValue) {
  return ([Uri]::EscapeDataString($PathValue) -replace "%2F", "/")
}

if (-not (Test-Path -LiteralPath $ApkPath)) {
  throw "APK not found: $ApkPath"
}

$apkFile = Get-Item -LiteralPath $ApkPath
if ($apkFile.PSIsContainer) {
  throw "APK path is a directory, expected a file: $ApkPath"
}

$dest = $DestDir.Trim()
if ([string]::IsNullOrWhiteSpace($dest)) {
  $dest = "/"
}
if (-not $dest.StartsWith("/")) {
  $dest = "/$dest"
}
if ($dest.Length -gt 1 -and $dest.EndsWith("/")) {
  $dest = $dest.TrimEnd("/")
}

$targetPath = if ($dest -eq "/") { "/$DestName" } else { "$dest/$DestName" }
$baseUrl = "http://$CydIp`:8080"
$encodedDest = Encode-Path $dest
$encodedTargetPath = Encode-Path $targetPath

Write-Step "Checking CYD API at $baseUrl ..."
$statusUrl = "$baseUrl/api/status"
$status = Invoke-RestMethod -Uri $statusUrl -Method Get -TimeoutSec 10
if (-not $status.ok) {
  throw "CYD /api/status did not return ok=true."
}

Write-Step ("CYD status: sdReady={0} sniffActive={1}" -f $status.sdReady, $status.sniffActive)

if (-not $SkipDelete) {
  Write-Step "Deleting existing file first (overwrite-safe): $targetPath"
  $deleteUrl = "$baseUrl/delete?fs=sd&path=$encodedTargetPath"
  try {
    $null = Invoke-WebRequest -Uri $deleteUrl -Method Get -MaximumRedirection 0 -TimeoutSec 15
  } catch {
    $resp = $_.Exception.Response
    if ($resp -eq $null) {
      throw
    }
    $code = [int]$resp.StatusCode
    if ($code -ne 302 -and $code -ne 404 -and $code -ne 200) {
      throw
    }
  }
}

Write-Step "Uploading APK to SD: $targetPath"
$uploadUrl = "$baseUrl/upload?fs=sd&path=$encodedDest"

$handler = New-Object System.Net.Http.HttpClientHandler
$handler.AllowAutoRedirect = $false
$client = New-Object System.Net.Http.HttpClient($handler)
$client.Timeout = [TimeSpan]::FromMinutes(2)

$stream = [System.IO.File]::OpenRead($apkFile.FullName)
try {
  $content = New-Object System.Net.Http.MultipartFormDataContent
  $fileContent = New-Object System.Net.Http.StreamContent($stream)
  $fileContent.Headers.ContentType = [System.Net.Http.Headers.MediaTypeHeaderValue]::Parse("application/vnd.android.package-archive")
  $content.Add($fileContent, "upfile", $DestName)

  $response = $client.PostAsync($uploadUrl, $content).GetAwaiter().GetResult()
  $statusCode = [int]$response.StatusCode
  if ($statusCode -ne 302 -and $statusCode -ne 200) {
    $body = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
    throw "Upload failed: HTTP $statusCode $body"
  }
} finally {
  $stream.Dispose()
  $client.Dispose()
}

Write-Step "Verifying file on SD ..."
$listUrl = "$baseUrl/api/list?fs=sd&path=$encodedDest"
$listing = Invoke-RestMethod -Uri $listUrl -Method Get -TimeoutSec 20
if (-not $listing.ok) {
  throw "SD verification failed: /api/list returned ok=false."
}

$entry = $null
foreach ($item in $listing.items) {
  if (-not $item.dir -and $item.name -eq $DestName) {
    $entry = $item
    break
  }
}

if ($entry -eq $null) {
  if ($listing.truncated) {
    throw "Upload may have succeeded, but directory listing is truncated so file was not confirmed. Browse SD to verify: $baseUrl/browse?fs=sd&path=$encodedDest"
  }
  throw "Upload did not verify: $targetPath not found in SD listing."
}

$srcSize = [int64]$apkFile.Length
$dstSize = [int64]$entry.size
if ($srcSize -ne $dstSize) {
  throw "Size mismatch after upload: local=$srcSize remote=$dstSize"
}

Write-Host ""
Write-Host "[+] APK pushed successfully."
Write-Host ("[+] Path: {0}" -f $targetPath)
Write-Host ("[+] Size: {0} bytes" -f $dstSize)
Write-Host ("[+] Install page: {0}/android" -f $baseUrl)
Write-Host ("[+] Direct APK URL: {0}/android.apk" -f $baseUrl)
