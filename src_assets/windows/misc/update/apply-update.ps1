# Installs a downloaded Norishine update. Started by the host (src/updater.cpp) outside the
# service's process group, from a copy in config\updates, so it keeps running while the
# installer stops the service and replaces the files.
#
# Runs the installer silently, then makes sure the service is running again (starting it if
# the installer didn't), and records the result in the status file for the new host to report.
param(
    [Parameter(Mandatory = $true)][string]$Installer,
    [Parameter(Mandatory = $true)][string]$InstallDir,
    [Parameter(Mandatory = $true)][string]$StatusFile
)

$ErrorActionPreference = 'Continue'
$ServiceName = 'NorishineService'
$Log = Join-Path (Split-Path -Parent $StatusFile) 'update.log'

function Write-Log([string]$Message) {
    Add-Content -Path $Log -Value ('{0} {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $Message)
}

function Update-Status([hashtable]$Fields) {
    $status = @{}
    if (Test-Path $StatusFile) {
        try {
            $existing = Get-Content -Raw -Path $StatusFile | ConvertFrom-Json
            foreach ($p in $existing.PSObject.Properties) { $status[$p.Name] = $p.Value }
        } catch {
            Write-Log "Couldn't read $StatusFile, starting a new one"
        }
    }
    foreach ($k in $Fields.Keys) { $status[$k] = $Fields[$k] }
    # No byte order mark: the host's JSON reader expects plain UTF-8.
    [IO.File]::WriteAllText($StatusFile, ($status | ConvertTo-Json), (New-Object Text.UTF8Encoding $false))
}

function Get-Now { (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ') }

Write-Log "Update: $Installer into $InstallDir"

# Let the host answer the request that started the update before the installer stops it.
Start-Sleep -Seconds 3

$exitCode = -1
try {
    # /D must come last and stays unquoted, as NSIS expects.
    $process = Start-Process -FilePath $Installer -ArgumentList @('/S', "/D=$InstallDir") -Wait -PassThru
    $exitCode = $process.ExitCode
    Write-Log "Installer exit code: $exitCode"
} catch {
    Write-Log "Couldn't run the installer: $_"
}

# Whatever happened, try to leave a running host behind, so it can still be reached remotely.
$running = $false
for ($i = 0; $i -lt 30; $i++) {
    $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if ($service -and $service.Status -eq 'Running') {
        $running = $true
        break
    }
    if ($service -and $service.Status -eq 'Stopped') {
        Write-Log 'Service stopped; starting it'
        Start-Service -Name $ServiceName -ErrorAction SilentlyContinue
    }
    Start-Sleep -Seconds 2
}
Write-Log "Service running: $running"

if ($exitCode -eq 0 -and $running) {
    Update-Status @{ state = 'installed'; finished = (Get-Now); exit_code = $exitCode }
} else {
    $reason = if ($exitCode -ne 0) { "The installer stopped with code $exitCode." } else { "The service didn't start after the update." }
    Update-Status @{ state = 'failed'; finished = (Get-Now); exit_code = $exitCode; error = $reason }
}
