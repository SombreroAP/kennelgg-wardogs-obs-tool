# Build the self-contained ClipHound folder on Windows (used by CI; works locally too).
#   pwsh app/build_exe.ps1  -> app/dist/ClipHound/
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
python -m pip install --quiet --upgrade pip
python -m pip install --quiet -r requirements.txt pyinstaller
pyinstaller --noconfirm --clean cliphound.spec
# Tesseract: choco puts it in Program Files; ship it next to the exe with English data only
$tess = "C:\Program Files\Tesseract-OCR"
if (Test-Path $tess) {
  New-Item -ItemType Directory -Force "dist/ClipHound/tesseract/tessdata" | Out-Null
  Copy-Item "$tess\*.exe","$tess\*.dll" "dist/ClipHound/tesseract/"
  Copy-Item "$tess\tessdata\eng.traineddata","$tess\tessdata\osd.traineddata" "dist/ClipHound/tesseract/tessdata/" -ErrorAction SilentlyContinue
}
Copy-Item config.yaml dist/ClipHound/config.default.yaml
Remove-Item dist/ClipHound/config.yaml -ErrorAction SilentlyContinue
Copy-Item README.md dist/ClipHound/README.md
Get-ChildItem dist/ClipHound | Select-Object Name,Length
