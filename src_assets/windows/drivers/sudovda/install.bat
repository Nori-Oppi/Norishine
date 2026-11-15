@echo off
rem Installs or updates SudoVDA without removing a device another host (Apollo, Vibepollo) uses.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0sudovda.ps1" -Action install %*
exit /b %ERRORLEVEL%
