@echo off
cd /d "%~dp0"
where py >nul 2>nul
if errorlevel 1 (
  echo Instale Python 3.11 ou superior de python.org, com o Python Launcher.
  pause
  exit /b 1
)
if not exist .venv\Scripts\python.exe py -3 -m venv .venv
if errorlevel 1 goto erro
.venv\Scripts\python.exe -m pip install -r requirements.txt
if errorlevel 1 goto erro
.venv\Scripts\python.exe importer.py
if errorlevel 1 goto erro
exit /b 0
:erro
echo Nao foi possivel iniciar. Leia a mensagem acima.
pause
exit /b 1
