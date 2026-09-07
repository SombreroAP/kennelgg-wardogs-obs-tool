@echo off
:: ClipHound installer for the streaming PC (Windows 10/11). Run once. Safe to re-run.
cd /d "%~dp0"
echo === ClipHound install ===
where python >nul 2>nul || (
  echo Installing Python 3.12 via winget...
  winget install -e --id Python.Python.3.12 --accept-package-agreements --accept-source-agreements
  set "PATH=%LOCALAPPDATA%\Programs\Python\Python312;%LOCALAPPDATA%\Programs\Python\Python312\Scripts;%PATH%"
)
where tesseract >nul 2>nul || if not exist "%ProgramFiles%\Tesseract-OCR\tesseract.exe" (
  echo Installing Tesseract OCR via winget...
  winget install -e --id UB-Mannheim.TesseractOCR --accept-package-agreements --accept-source-agreements
)
if exist "%ProgramFiles%\Tesseract-OCR\tesseract.exe" set "PATH=%ProgramFiles%\Tesseract-OCR;%PATH%"
if not exist .venv ( python -m venv .venv || goto :fail )
.venv\Scripts\python -m pip install -q --upgrade pip
.venv\Scripts\pip install -q -r requirements.txt || goto :fail
if not exist config.yaml.bak copy config.yaml config.yaml.bak >nul
echo.
echo Installed. Next:
echo   1. Edit config.yaml: OBS websocket password, obs_source name, Twitch client id/secret.
echo   2. Run setup.bat  (lists OBS sources, checks the feed region, logs in to Twitch as InfoKennel).
echo   3. Run ClipHound.bat  (add --dry-run to test without clipping).
pause
exit /b 0
:fail
echo INSTALL FAILED - see messages above. Python 3.11+ and Tesseract must be installed and on PATH.
pause
exit /b 1
