# NSIS Packaging
# see options at: https://cmake.org/cmake/help/latest/cpack_gen/nsis.html

set(CPACK_NSIS_INSTALLED_ICON_NAME "${PROJECT__DIR}\\\\${PROJECT_EXE}")

# Extra install commands
# Restores permissions on the install directory
# Migrates config files from the root into the new config folder
# Install service
SET(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
        "${CPACK_NSIS_EXTRA_INSTALL_COMMANDS}
        nsExec::ExecToLog 'icacls \\\"$INSTDIR\\\" /reset'
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\update-path.bat\\\" add'
        # Apollo or Vibepollo installed: replace it (recommended) or keep both on another port.
        # Runs before the driver installs, because their uninstallers may remove the drivers.
        nsExec::ExecToLog \
          'powershell.exe -NoProfile -ExecutionPolicy Bypass -File \
            \\\"$INSTDIR\\\\scripts\\\\other-hosts.ps1\\\" -Action prepare'
        Pop $0
        StrCmp $0 '10' 0 OtherHostDone
        MessageBox MB_YESNO|MB_ICONQUESTION \
            'Apollo or Vibepollo is installed on this PC. Replace it with Norishine? (Recommended)$\\\\n$\\\\n\
Yes: its settings, apps and paired devices move to Norishine, and it is uninstalled. \
To go back later, uninstall Norishine, then install Apollo or Vibepollo again: the settings carry over.$\\\\n$\\\\n\
No: keep both. Norishine then uses port 48989 instead of 47989 (web UI at https://localhost:48990).' \
            /SD IDNO IDNO OtherHostKeep
            nsExec::ExecToLog \
              'powershell.exe -NoProfile -ExecutionPolicy Bypass -File \
                \\\"$INSTDIR\\\\scripts\\\\other-hosts.ps1\\\" -Action replace'
            Pop $0
            StrCmp $0 '0' OtherHostDone
            MessageBox MB_OK|MB_ICONEXCLAMATION \
                'Apollo or Vibepollo could not be uninstalled. Its service was stopped and disabled; \
uninstall it from Settings, Apps.'
            Goto OtherHostDone
        OtherHostKeep:
            nsExec::ExecToLog \
              'powershell.exe -NoProfile -ExecutionPolicy Bypass -File \
                \\\"$INSTDIR\\\\scripts\\\\other-hosts.ps1\\\" -Action side-by-side'
        OtherHostDone:
        nsExec::ExecToLog \
          'powershell.exe -NoProfile -ExecutionPolicy Bypass -File \\\"$INSTDIR\\\\drivers\\\\sudovda\\\\sudovda.ps1\\\" -Action install'
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\migrate-config.bat\\\"'
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\add-firewall-rule.bat\\\"'
        nsExec::ExecToLog \
          'powershell.exe -NoProfile -ExecutionPolicy Bypass -File \\\"$INSTDIR\\\\scripts\\\\install-gamepad.ps1\\\"'
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\install-service.bat\\\"'
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\autostart-service.bat\\\"'
        NoController:
        ")

# Extra uninstall commands
# Uninstall service
set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
        "${CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS}
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\delete-firewall-rule.bat\\\"'
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\uninstall-service.bat\\\"'
        nsExec::ExecToLog '\\\"$INSTDIR\\\\sunshine.exe\\\" --restore-nvprefs-undo'
        # If Norishine replaced Apollo or Vibepollo, leave its settings where a reinstall finds
        # them. Skipped when silent: an upgrade runs the old uninstaller silently.
        IfSilent NoHostRestore
            nsExec::ExecToLog \
              'powershell.exe -NoProfile -ExecutionPolicy Bypass -File \
                \\\"$INSTDIR\\\\scripts\\\\other-hosts.ps1\\\" -Action restore'
        NoHostRestore:
        # Apollo and Vibepollo share the ViGEmBus and SudoVDA drivers. While either is
        # installed, do not offer to remove them (exit code 0 means no other host).
        nsExec::ExecToLog \
          'powershell.exe -NoProfile -ExecutionPolicy Bypass -File \
            \\\"$INSTDIR\\\\drivers\\\\sudovda\\\\sudovda.ps1\\\" -Action check'
        Pop $0
        StrCmp $0 '0' 0 NoSudoVDA
        MessageBox MB_YESNO|MB_ICONQUESTION \
            'Do you want to remove the ViGEmBus virtual gamepad driver? Other programs, such as controller tools, may also use it.' \
            /SD IDNO IDNO NoGamepad
            nsExec::ExecToLog \
              'powershell.exe -NoProfile -ExecutionPolicy Bypass -File \
                \\\"$INSTDIR\\\\scripts\\\\uninstall-gamepad.ps1\\\"'; \
              skipped if no
        NoGamepad:
        MessageBox MB_YESNO|MB_ICONQUESTION \
            'Do you want to remove SudoVDA Virtual Display Driver?' \
            /SD IDNO IDNO NoSudoVDA
            nsExec::ExecToLog \
              'powershell.exe -NoProfile -ExecutionPolicy Bypass -File \
                \\\"$INSTDIR\\\\drivers\\\\sudovda\\\\sudovda.ps1\\\" -Action uninstall'; skipped if no
        NoSudoVDA:
        MessageBox MB_YESNO|MB_ICONQUESTION \
            'Do you want to remove $INSTDIR (this includes the configuration, cover images, and settings)?' \
            /SD IDNO IDNO NoDelete
            RMDir /r \\\"$INSTDIR\\\"; skipped if no
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\update-path.bat\\\" remove'
        NoDelete:
        ")

# Adding an option for the start menu
set(CPACK_NSIS_MODIFY_PATH OFF)
set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
# This will be shown on the installed apps Windows settings
set(CPACK_NSIS_INSTALLED_ICON_NAME "sunshine.exe")
set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "${CPACK_NSIS_CREATE_ICONS_EXTRA}
        SetOutPath '\$INSTDIR'
        CreateShortCut '\$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\${CMAKE_PROJECT_NAME}.lnk' \
            '\$INSTDIR\\\\sunshine.exe' '--shortcut'
        ")
set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "${CPACK_NSIS_DELETE_ICONS_EXTRA}
        Delete '\$SMPROGRAMS\\\\$MUI_TEMP\\\\${CMAKE_PROJECT_NAME}.lnk'
        ")

# Checking for previous installed versions
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL "ON")

# set(CPACK_NSIS_HELP_LINK "https://docs.lizardbyte.dev/projects/sunshine/latest/md_docs_2getting__started.html")
# set(CPACK_NSIS_URL_INFO_ABOUT "${CMAKE_PROJECT_HOMEPAGE_URL}")
# set(CPACK_NSIS_CONTACT "${CMAKE_PROJECT_HOMEPAGE_URL}/support")

# set(CPACK_NSIS_MENU_LINKS
#         "https://docs.lizardbyte.dev/projects/sunshine" "Sunshine documentation"
#         "https://app.lizardbyte.dev" "LizardByte Web Site"
#         "https://app.lizardbyte.dev/support" "LizardByte Support")
set(CPACK_NSIS_MANIFEST_DPI_AWARE true)
