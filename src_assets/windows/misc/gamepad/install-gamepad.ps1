# Check if a compatible version of ViGEmBus is already installed (1.17 or later)
try {
    $vigemBusPath = "$env:SystemRoot\System32\drivers\ViGEmBus.sys"
    $versionInfo = (Get-Item $vigemBusPath -ErrorAction Stop).VersionInfo
    $fileVersion = [System.Version]::new($versionInfo.FileMajorPart, $versionInfo.FileMinorPart,
        $versionInfo.FileBuildPart, $versionInfo.FilePrivatePart)

    if ($fileVersion -ge [System.Version]"1.17") {
        Write-Information "ViGEmBus $fileVersion is installed (1.17 or later), no update needed. Exiting."
        exit 0
    }
}
catch {
    Write-Information "ViGEmBus driver not found or inaccessible, proceeding with installation."
}

# Install Virtual Gamepad
$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Path
$installerPath = Join-Path $scriptPath "vigembus_installer.exe"
Start-Process `
    -FilePath $installerPath `
    -ArgumentList "/passive", "/promptrestart"
