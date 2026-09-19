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
# ffmpeg: choco puts the gyan.dev build under chocolatey\lib; the highlights compilation needs it
$ffs = @("C:\ProgramData\chocolatey\lib\ffmpeg\tools\ffmpeg\bin\ffmpeg.exe", "C:\ProgramData\chocolatey\bin\ffmpeg.exe")
foreach ($ff in $ffs) {
  if (Test-Path $ff) {
    New-Item -ItemType Directory -Force "dist/ClipHound/ffmpeg" | Out-Null
    Copy-Item $ff "dist/ClipHound/ffmpeg/ffmpeg.exe"
    break
  }
}
# the brand font for the compilation's title cards
if (Test-Path "../data/overlay/SairaCondensed-Bold.ttf") {
  New-Item -ItemType Directory -Force "dist/ClipHound/fonts" | Out-Null
  Copy-Item "../data/overlay/SairaCondensed-Bold.ttf" "dist/ClipHound/fonts/"
}
(Get-Content ../buildspec.json -Raw | ConvertFrom-Json).version | Set-Content dist/ClipHound/version.txt
Copy-Item config.yaml dist/ClipHound/config.default.yaml
Remove-Item dist/ClipHound/config.yaml -ErrorAction SilentlyContinue
Copy-Item README.md dist/ClipHound/README.md
Get-ChildItem dist/ClipHound | Select-Object Name,Length
