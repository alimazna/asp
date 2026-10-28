# Build the ASTRA Collector for Windows.
#
# Run from the windows-collector directory on a Windows machine with Python:
#     powershell -ExecutionPolicy Bypass -File packaging/build_windows.ps1
#
# Output: dist\ASTRA-Collector.exe

$ErrorActionPreference = "Stop"

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $here
Set-Location $root

Write-Host "== ASTRA Collector Windows build =="
Write-Host "root: $root"

$python = "python"
if (Get-Command py -ErrorAction SilentlyContinue) { $python = "py" }

Write-Host "`n-- checking Python --"
& $python --version

Write-Host "`n-- verifying tkinter is available --"
& $python -c "import tkinter; print('tkinter', tkinter.TkVersion)"

Write-Host "`n-- running the test suite before packaging --"
& $python -m unittest discover -s tests
if ($LASTEXITCODE -ne 0) {
    throw "Tests failed. Not packaging a build that does not pass."
}

Write-Host "`n-- installing PyInstaller --"
& $python -m pip install --upgrade pip
& $python -m pip install "pyinstaller>=6.0"

Write-Host "`n-- building the executable --"
& $python -m PyInstaller --clean --noconfirm packaging/astra_collector.spec

$exe = Join-Path $root "dist\ASTRA-Collector.exe"
if (-not (Test-Path $exe)) {
    throw "Build did not produce $exe"
}

$size = [math]::Round((Get-Item $exe).Length / 1MB, 1)
Write-Host "`n== build complete =="
Write-Host "executable: $exe"
Write-Host "size      : $size MB"
