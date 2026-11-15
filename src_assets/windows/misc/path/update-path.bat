@echo off
setlocal EnableDelayedExpansion

rem Check if parameter is provided
if "%~1"=="" (
    echo Usage: %0 [add^|remove]
    echo   add    - Adds Norishine directories to system PATH
    echo   remove - Removes Norishine directories from system PATH
    exit /b 1
)

rem Get Norishine root directory
for %%I in ("%~dp0\..") do set "ROOT_DIR=%%~fI"
echo Norishine root directory: !ROOT_DIR!

rem Only bin\ goes on PATH: it holds norishine.cmd. The install folder itself has sunshine.exe,
rem which Apollo, Vibepollo and Sunshine also put on PATH under the same name.
set "BIN_DIR=!ROOT_DIR!\bin"
rem Folders earlier versions added; removed on add and remove.
set "OLD_DIRS[0]=!ROOT_DIR!"
set "OLD_DIRS[1]=!ROOT_DIR!\tools"

rem System path registry location
set "KEY_NAME=HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\Environment"
set "VALUE_NAME=Path"

rem Get the current path
for /f "tokens=2*" %%A in ('reg query "%KEY_NAME%" /v "%VALUE_NAME%"') do set "CURRENT_PATH=%%B"
echo Current path: !CURRENT_PATH!
set "ORIGINAL_PATH=!CURRENT_PATH!"

if /i "%~1"=="add" (
    for /L %%i in (0,1,1) do call :remove_entry "!OLD_DIRS[%%i]!"
    call :has_entry "!BIN_DIR!"
    if !ERRORLEVEL!==0 (
        echo !BIN_DIR! already in path
    ) else (
        echo Adding to path: !BIN_DIR!
        set "CURRENT_PATH=!CURRENT_PATH!;!BIN_DIR!"
    )
    goto apply
)

if /i "%~1"=="remove" (
    call :remove_entry "!BIN_DIR!"
    for /L %%i in (0,1,1) do call :remove_entry "!OLD_DIRS[%%i]!"
    goto apply
)

echo Unknown parameter: %~1
echo Usage: %0 [add^|remove]
exit /b 1

rem Write the path if it changed.
:apply
if "!CURRENT_PATH!"=="!ORIGINAL_PATH!" (
    echo No changes needed to PATH
    exit /b 0
)
set "PATH_TO_WRITE=!CURRENT_PATH!"
call :write_path
if !ERRORLEVEL!==0 (
    echo Updated PATH
) else (
    echo Failed to update PATH
)
exit /b !ERRORLEVEL!

rem @brief Exit code 0 when %~1 is an entry of CURRENT_PATH (whole entry, not a substring).
:has_entry
for %%p in ("!CURRENT_PATH:;=" "!") do (
    if /i "%%~p"=="%~1" exit /b 0
)
exit /b 1

rem @brief Remove every CURRENT_PATH entry equal to %~1.
:remove_entry
call :has_entry "%~1"
if !ERRORLEVEL! neq 0 exit /b 0
echo Removing from path: %~1
set "NEW_PATH="
for %%p in ("!CURRENT_PATH:;=" "!") do (
    if /i "%%~p" neq "%~1" (
        if defined NEW_PATH (
            set "NEW_PATH=!NEW_PATH!;%%~p"
        ) else (
            set "NEW_PATH=%%~p"
        )
    )
)
set "CURRENT_PATH=!NEW_PATH!"
exit /b 0

rem @brief Write PATH_TO_WRITE to KEY_NAME\VALUE_NAME without corrupting trailing backslashes
rem (Sunshine 4dadc7d4).
rem @return The exit code from reg.exe.
:write_path
set "REG_PATH=!PATH_TO_WRITE!"
set "TRAILING_PATH=!PATH_TO_WRITE!"

rem Windows argument parsing requires every backslash before the closing quote to be doubled.
rem Escape a copy so PATH comparisons and the original entries remain unchanged.
:escape_trailing_backslashes
if "!TRAILING_PATH:~-1!"=="\" (
    set "REG_PATH=!REG_PATH!\"
    set "TRAILING_PATH=!TRAILING_PATH:~0,-1!"
    goto escape_trailing_backslashes
)

rem Some localized systems may unexpectedly ignore /f at the end of the command.
rem Keep it before /d so the installer never waits for overwrite confirmation.
reg add "%KEY_NAME%" /v "%VALUE_NAME%" /t REG_EXPAND_SZ /f /d "!REG_PATH!"
exit /b !ERRORLEVEL!
