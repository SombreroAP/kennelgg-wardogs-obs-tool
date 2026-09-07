@echo off
:: ClipHound setup helpers. Run after install.bat and after filling config.yaml.
cd /d "%~dp0"
if exist "%ProgramFiles%\Tesseract-OCR\tesseract.exe" set "PATH=%ProgramFiles%\Tesseract-OCR;%PATH%"
echo --- OBS inputs (copy the game capture name into capture.obs_source) ---
.venv\Scripts\python calibrate.py --sources
echo.
echo --- Feed region check: opens roi.png next to this file; the kill feed must be inside ---
.venv\Scripts\python calibrate.py
start roi.png
echo.
choice /m "Log in to Twitch now (as InfoKennel) to get the clip token"
if errorlevel 2 goto :done
.venv\Scripts\python get_token.py
:done
pause
