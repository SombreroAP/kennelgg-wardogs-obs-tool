@echo off
cd /d "%~dp0"
if exist "%ProgramFiles%\Tesseract-OCR\tesseract.exe" set "PATH=%ProgramFiles%\Tesseract-OCR;%PATH%"
if not exist .venv ( echo Run install.bat first. & pause & exit /b 1 )
title ClipHound
.venv\Scripts\python main.py %*
pause
