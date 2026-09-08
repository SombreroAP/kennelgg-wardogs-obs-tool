@echo off
:: ClipHound setup wizard: OBS connection, which input shows the gameplay, your name, library, Twitch.
cd /d "%~dp0"
if exist "%ProgramFiles%\Tesseract-OCR\tesseract.exe" set "PATH=%ProgramFiles%\Tesseract-OCR;%PATH%"
if not exist .venv ( echo Run install.bat first. & pause & exit /b 1 )
.venv\Scripts\python setup.py %*
if exist roi.png start roi.png
pause
