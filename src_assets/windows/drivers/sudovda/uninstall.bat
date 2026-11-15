@echo off
rem Removes the SudoVDA device, unless Apollo or Vibepollo also uses it. Pass -Force to remove it anyway.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0sudovda.ps1" -Action uninstall %*
set RESULT=%ERRORLEVEL%
pause
exit /b %RESULT%
