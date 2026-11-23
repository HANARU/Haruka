# Ensures embedded Python 3.15.2 is downloaded, SHA-256 verified, and configured in ThirdParty/Python.
# SHA-256 source: https://www.python.org/downloads/release/python-3152/ (Windows embeddable package 64-bit)
param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot
)

$ErrorActionPreference = "Stop"

$PythonUrl      = "https://www.python.org/ftp/python/3.15.2/python-3.15.2-embed-amd64.zip"
$ExpectedSha256 = "d012f3aa29cf07436dae8aea1fec0fd195c2612a0b059b1f915ae0afb9ad1f62"

$PythonDir  = Join-Path $RepoRoot "ThirdParty\Python"
$PythonExe  = Join-Path $PythonDir "python.exe"
$MarkerFile = Join-Path $PythonDir ".haruka-python.sha256"
$ZipPath    = Join-Path $PythonDir "python-3.15.2-embed-amd64.zip"

if ((Test-Path $PythonExe) -and (Test-Path $MarkerFile)) {
    $recorded = (Get-Content -Raw -Path $MarkerFile).Trim().ToLowerInvariant()
    if ($recorded -eq $ExpectedSha256) {
        Write-Host "[Setup] Embedded Python 3.15.2 already present (SHA-256 verified)."
        exit 0
    }
}

New-Item -ItemType Directory -Force -Path $PythonDir | Out-Null

Write-Host "[Setup] Downloading embedded Python 3.15.2..."
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Invoke-WebRequest -Uri $PythonUrl -OutFile $ZipPath -UseBasicParsing

$actual = (Get-FileHash -Path $ZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actual -ne $ExpectedSha256) {
    Remove-Item -Force -ErrorAction SilentlyContinue $ZipPath
    Write-Error "[Setup] FAILED at Python download: SHA-256 mismatch (expected $ExpectedSha256, got $actual). Deleted corrupted archive; check network/proxy and retry."
    exit 1
}

Write-Host "[Setup] Verifying and extracting Python 3.15.2 (SHA-256 OK)..."
Expand-Archive -Path $ZipPath -DestinationPath $PythonDir -Force
Remove-Item -Force -ErrorAction SilentlyContinue $ZipPath

$pthFiles = Get-ChildItem -Path $PythonDir -Filter "python*._pth"
foreach ($pth in $pthFiles) {
    $text = Get-Content -Raw -Path $pth.FullName
    $updated = $text -replace "(?m)^#\s*import site\s*$", "import site"
    if ($updated -notmatch "(?m)^import site\s*$") {
        $updated = $updated.TrimEnd() + "`r`nimport site`r`n"
    }
    Set-Content -Path $pth.FullName -Value $updated -Encoding Ascii -NoNewline
}

Set-Content -Path $MarkerFile -Value $ExpectedSha256 -Encoding Ascii -NoNewline
Write-Host "[Setup] Embedded Python 3.15.2 ready at $PythonDir"
exit 0
