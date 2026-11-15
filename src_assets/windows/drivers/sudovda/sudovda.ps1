<#
.SYNOPSIS
  Installs, updates or removes the SudoVDA virtual display driver without breaking
  other hosts (Apollo, Vibepollo) that share the same device.

.DESCRIPTION
  SudoVDA has one root-enumerated device (hardware ID root\sudomaker\sudovda) that every
  host using it shares. Removing and recreating that device, as Apollo's install script
  did on every install, breaks a running Apollo or Vibepollo.

  -Action install
      Adds the driver certificate. If no SudoVDA device exists, creates it and installs the
      bundled driver. If one exists, keeps it, and updates its driver in place only when
      the bundled driver is newer.
  -Action check
      Exit code 0 when no other host that uses SudoVDA is installed (removal is safe),
      1 when one is, so removal should not be offered.
  -Action uninstall
      Removes the SudoVDA device, unless another host uses it (override with -Force).

  -DryRun prints what would be done without changing anything.

  Detection reads the device registry instead of using Get-PnpDevice (can hang when run
  from an installer) or parsing pnputil output (localized).
#>
param(
  [Parameter(Mandatory = $true)]
  [ValidateSet('install', 'check', 'uninstall')]
  [string]$Action,
  [switch]$Force,
  [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$HardwareId = 'root\sudomaker\sudovda'
$DisplayClassGuid = '4D36E968-E325-11CE-BFC1-08002BE10318'
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Nefconc = Join-Path $Here 'nefconc.exe'
$Inf = Join-Path $Here 'SudoVDA.inf'

function Invoke-Step([string]$Description, [scriptblock]$Command) {
  if ($DryRun) {
    Write-Host "[SudoVDA] [dry run] $Description"
    return
  }
  Write-Host "[SudoVDA] $Description"
  & $Command
}

function Invoke-Nefconc([string[]]$Arguments) {
  if (-not (Test-Path -LiteralPath $Nefconc -PathType Leaf)) {
    throw "[SudoVDA] nefconc.exe is missing from $Here"
  }
  & $Nefconc @Arguments
  # 3010: success, reboot required.
  if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 3010) {
    throw "[SudoVDA] nefconc.exe $($Arguments[0]) failed with exit code $LASTEXITCODE"
  }
}

# Existing SudoVDA device nodes, with the version of the driver bound to each (or $null).
# Root-enumerated devices keep their Enum key exactly as long as the device exists.
function Get-SudoVdaDevices {
  $devices = @()
  foreach ($key in Get-ChildItem 'HKLM:\SYSTEM\CurrentControlSet\Enum\ROOT' -Recurse -Depth 1 -ErrorAction SilentlyContinue) {
    $properties = Get-ItemProperty -LiteralPath $key.PSPath -ErrorAction SilentlyContinue
    if (-not $properties -or -not ($properties.HardwareID -contains $HardwareId)) {
      continue
    }
    $version = $null
    if ($properties.Driver) {
      $class = Get-ItemProperty -LiteralPath "HKLM:\SYSTEM\CurrentControlSet\Control\Class\$($properties.Driver)" -ErrorAction SilentlyContinue
      if ($class -and $class.DriverVersion) {
        try { $version = [version]$class.DriverVersion } catch {}
      }
    }
    $devices += [pscustomobject]@{
      InstanceId = ($key.Name -replace '^.*\\Enum\\', '')
      DriverVersion = $version
    }
  }
  return $devices
}

function Get-BundledDriverVersion {
  $line = Get-Content -LiteralPath $Inf | Where-Object { $_ -match '^\s*DriverVer\s*=' } | Select-Object -First 1
  if ($line -match ',\s*([\d.]+)') { return [version]$Matches[1] }
  throw "[SudoVDA] No DriverVer in $Inf"
}

# Other hosts that create virtual displays through SudoVDA. Apollo and Vibepollo both
# install a service named ApolloService and an uninstall entry under their own name.
function Get-OtherSudoVdaHosts {
  $found = @()
  if (Get-Service -Name 'ApolloService' -ErrorAction SilentlyContinue) {
    $found += 'ApolloService (Apollo or Vibepollo)'
  }
  $uninstallKeys = @(
    'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
    'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*'
  )
  $found += @(Get-ItemProperty -Path $uninstallKeys -ErrorAction SilentlyContinue |
      Where-Object { $_.DisplayName -match '^(Apollo|Vibepollo)\b' } |
      ForEach-Object { $_.DisplayName } | Sort-Object -Unique)
  return $found
}

function Install-Driver {
  $certificate = Join-Path $Here 'sudovda.cer'
  Invoke-Step 'Adding the driver certificate.' {
    certutil.exe -addstore -f root "$certificate" | Out-Null
    certutil.exe -addstore -f TrustedPublisher "$certificate" | Out-Null
  }

  $bundled = Get-BundledDriverVersion
  $devices = @(Get-SudoVdaDevices)

  if ($devices.Count -eq 0) {
    Invoke-Step "No SudoVDA device found; creating it and installing driver $bundled." {
      Invoke-Nefconc @('--create-device-node', '--class-name', 'Display', '--class-guid', $DisplayClassGuid, '--hardware-id', $HardwareId)
      Invoke-Nefconc @('--install-driver', '--inf-path', $Inf)
    }
    return
  }

  foreach ($device in $devices) {
    Write-Host ("[SudoVDA] Existing device {0}, driver {1}." -f $device.InstanceId,
      $(if ($device.DriverVersion) { $device.DriverVersion } else { 'not installed' }))
  }

  $newest = $devices | ForEach-Object { $_.DriverVersion } | Where-Object { $_ } |
    Sort-Object -Descending | Select-Object -First 1
  if ($newest -and $newest -ge $bundled) {
    Write-Host "[SudoVDA] Keeping the existing device and driver (bundled driver is $bundled)."
    return
  }

  # Updates the driver on the existing device without removing it. Windows restarts the
  # device, so a stream another host is running on a virtual display is interrupted once.
  Invoke-Step "Updating the driver on the existing device to $bundled." {
    Invoke-Nefconc @('--install-driver', '--inf-path', $Inf)
  }
}

switch ($Action) {
  'install' {
    Install-Driver
    exit 0
  }
  'check' {
    $others = @(Get-OtherSudoVdaHosts)
    if ($others.Count -gt 0) {
      Write-Host "[SudoVDA] Also used by: $($others -join ', ')."
      exit 1
    }
    Write-Host '[SudoVDA] No other host uses it.'
    exit 0
  }
  'uninstall' {
    $others = @(Get-OtherSudoVdaHosts)
    if ($others.Count -gt 0 -and -not $Force) {
      Write-Host "[SudoVDA] Not removing the device: also used by $($others -join ', '). Use -Force to remove it anyway."
      exit 1
    }
    Invoke-Step 'Removing the SudoVDA device.' {
      Invoke-Nefconc @('--remove-device-node', '--hardware-id', $HardwareId, '--class-guid', $DisplayClassGuid)
    }
    exit 0
  }
}
