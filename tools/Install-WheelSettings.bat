@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install.ps1" %*
set "installResult=%errorlevel%"
if not "%installResult%"=="0" echo Installation failed. Read the message above.
pause
exit /b %installResult%
