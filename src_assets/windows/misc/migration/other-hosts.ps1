<#
.SYNOPSIS
  Handles an Apollo or Vibepollo installation on the same PC during Norishine's
  install and uninstall.

.DESCRIPTION
  Norishine reads Apollo's configuration files unchanged (Vibepollo's too; settings
  Norishine does not know are ignored), so settings, apps and paired devices carry over
  in both directions.

  -Action prepare      (install) Exit code 10 when Apollo or Vibepollo is installed and
                       the user must choose between replacing it and keeping both.
                       Otherwise exit code 0: an earlier "keep both" choice is kept, and
                       on a fresh Norishine config the configuration left behind by an
                       uninstalled Apollo or Vibepollo is imported.
  -Action replace      (install) Copies its configuration into Norishine (only when
                       Norishine has none yet) and uninstalls it. Exit code 2 when it
                       could not be uninstalled; its service is then stopped and disabled.
  -Action side-by-side (install) Keeps both: Norishine uses port 48989 instead of 47989.
  -Action restore      (uninstall) When Norishine replaced Apollo or Vibepollo and that is
                       not installed again, puts Norishine's configuration in its config
                       folder, so installing it again carries the settings back.

  The choice is recorded in config\other_host.json next to the Norishine install.
#>
param(
  [ValidateSet('prepare', 'replace', 'side-by-side', 'restore')]
  [string]$Action,
  # For tests: the Norishine install folder (default: the folder above this script).
  [string]$Root
)

$ErrorActionPreference = 'Stop'
$NorishineRoot = if ($Root) { $Root } else { (Resolve-Path (Join-Path $PSScriptRoot '..')).Path }
$NorishineConfig = Join-Path $NorishineRoot 'config'
$RecordFile = Join-Path $NorishineConfig 'other_host.json'
$SideBySidePort = 48989
$PortComment = '# Added by the Norishine installer: Apollo or Vibepollo uses the default port'
# Sunshine's service names. Apollo's and Vibepollo's uninstallers delete SunshineSvc.
$SunshineServices = @('SunshineSvc', 'SunshineService')
$CopyMarker = 'norishine_copy.txt'

function Write-Log([string]$Message) { Write-Host "[other hosts] $Message" }

# Installed Apollo or Vibepollo, from the visible uninstall entries and ApolloService.
function Get-OtherHost {
  $keys = @(
    'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
    'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*'
  )
  $entry = Get-ItemProperty -Path $keys -ErrorAction SilentlyContinue |
    Where-Object { $_.DisplayName -match '^(Apollo|Vibepollo)\b' -and $_.SystemComponent -ne 1 } |
    Select-Object -First 1
  # Registry instead of WMI, which can hang under an installer.
  $service = Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Services\ApolloService' -ErrorAction SilentlyContinue

  if (-not $entry -and -not $service) { return $null }

  $installDir = $null
  if ($entry -and $entry.InstallLocation) {
    $installDir = $entry.InstallLocation
  } elseif ($entry -and $entry.UninstallString -match '^"?([^"]+\\)[^\\"]+\.exe') {
    $installDir = $Matches[1]
  } elseif ($service -and $service.ImagePath -match '^"?([^"]+)\\tools\\sunshinesvc\.exe') {
    $installDir = $Matches[1]
  }
  if ($installDir) { $installDir = $installDir.TrimEnd('\') }

  return [pscustomobject]@{
    Name = $(if ($entry) { $entry.DisplayName } else { 'Apollo' })
    InstallDir = $installDir
    Entry = $entry
    HasService = [bool]$service
  }
}

function Get-Record {
  if (Test-Path -LiteralPath $RecordFile) {
    try { return Get-Content -LiteralPath $RecordFile -Raw | ConvertFrom-Json } catch {}
  }
  return $null
}

function Set-Record([string]$Mode, [string]$Name, [string]$InstallDir) {
  New-Item -ItemType Directory -Force -Path $NorishineConfig | Out-Null
  [pscustomobject]@{ mode = $Mode; host = $Name; install_dir = $InstallDir } |
    ConvertTo-Json | Set-Content -LiteralPath $RecordFile -Encoding UTF8
}

# Apollo, Vibepollo and older Norishine keep the state in sunshine_state.json; Norishine
# splits it into state\host.json, pairing.json and credentials.json on first start.
function Test-HasState([string]$ConfigDir) {
  (Test-Path -LiteralPath (Join-Path $ConfigDir 'sunshine_state.json')) -or
    (Test-Path -LiteralPath (Join-Path $ConfigDir 'state\host.json'))
}

function Read-JsonFile([string]$Path) {
  if (-not (Test-Path -LiteralPath $Path)) { return $null }
  try { return [IO.File]::ReadAllText($Path) | ConvertFrom-Json } catch { return $null }
}

# Rebuilds sunshine_state.json, which Apollo and Vibepollo read, from Norishine's split
# state: unique ID (so clients still recognise the host) and paired devices. The web UI
# account is only included while it still has the old password hash; Norishine's stronger
# hash can't be read by them, and leaving it out makes them ask for a new account instead
# of refusing every password. The split files are removed so a later Norishine install
# imports what Apollo or Vibepollo saved meanwhile, not this old copy.
function Export-LegacyState([string]$ConfigDir) {
  $stateDir = Join-Path $ConfigDir 'state'
  $hostState = Read-JsonFile (Join-Path $stateDir 'host.json')
  if (-not $hostState) { return }
  $pairing = Read-JsonFile (Join-Path $stateDir 'pairing.json')
  $account = Read-JsonFile (Join-Path $stateDir 'credentials.json')

  $root = [ordered]@{ uniqueid = $hostState.uniqueid; named_devices = @() }
  if ($pairing -and $pairing.named_devices) { $root.named_devices = @($pairing.named_devices) }
  $legacy = [ordered]@{}
  if ($account -and $account.password -and -not ([string]$account.password).StartsWith('pbkdf2-')) {
    $legacy.username = $account.username
    $legacy.password = $account.password
    $legacy.salt = $account.salt
  } elseif ($account) {
    Write-Log 'The web UI password uses a hash Apollo and Vibepollo cannot read; they will ask for a new account.'
  }
  $legacy.root = $root
  $json = ConvertTo-Json -InputObject $legacy -Depth 20
  [IO.File]::WriteAllText((Join-Path $ConfigDir 'sunshine_state.json'), $json)
  Remove-Item -LiteralPath $stateDir -Recurse -Force
  Get-ChildItem -LiteralPath $ConfigDir -Filter 'sunshine_state.json.v0*.bak' -Force | Remove-Item -Force
  Write-Log "Wrote sunshine_state.json with $(@($root.named_devices).Count) paired device(s)."
}

# Rewrites absolute paths into one install folder (as written in sunshine.conf and,
# JSON-escaped, in apps.json) to the other.
function Update-Paths([string]$ConfigDir, [string]$FromRoot, [string]$ToRoot) {
  $pairs = @(
    @($FromRoot, $ToRoot),
    @($FromRoot.Replace('\', '\\'), $ToRoot.Replace('\', '\\')),
    @($FromRoot.Replace('\', '/'), $ToRoot.Replace('\', '/'))
  )
  foreach ($name in 'sunshine.conf', 'apps.json') {
    $file = Join-Path $ConfigDir $name
    if (-not (Test-Path -LiteralPath $file)) { continue }
    $text = [IO.File]::ReadAllText($file)
    $updated = $text
    foreach ($pair in $pairs) {
      $updated = [regex]::Replace($updated, [regex]::Escape($pair[0]), $pair[1].Replace('$', '$$'),
        [Text.RegularExpressions.RegexOptions]::IgnoreCase)
    }
    if ($updated -ne $text) {
      [IO.File]::WriteAllText($file, $updated)
      Write-Log "Updated install paths in $name."
    }
  }
}

# Copies a configuration folder: settings, apps, state (pairing), credentials, covers and
# anything else, except logs.
function Copy-Config([string]$From, [string]$To) {
  New-Item -ItemType Directory -Force -Path $To | Out-Null
  foreach ($item in Get-ChildItem -LiteralPath $From -Force) {
    if ($item.Name -eq 'logs' -or $item.Name -like '*.log' -or $item.Name -like '*.log.*' -or $item.Name -eq $CopyMarker) { continue }
    Copy-Item -LiteralPath $item.FullName -Destination $To -Recurse -Force
  }
}

# Same access rules as migrate-config.bat: only administrators can read the private key.
function Protect-Credentials([string]$ConfigDir) {
  $credentials = Join-Path $ConfigDir 'credentials'
  if (-not (Test-Path -LiteralPath $credentials)) { return }
  icacls.exe "$credentials" /inheritance:r | Out-Null
  icacls.exe "$credentials" /grant:r '*S-1-5-32-544:(OI)(CI)(F)' | Out-Null
  icacls.exe "$credentials" /grant:r '*S-1-5-32-545:(R)' | Out-Null
}

# Vibepollo has capture methods Norishine lacks (for example wgcc), and Norishine's service
# cannot use wgc. Anything but ddx is dropped, so Norishine chooses automatically.
function Remove-UnsupportedCapture([string]$ConfigDir) {
  $conf = Join-Path $ConfigDir 'sunshine.conf'
  if (-not (Test-Path -LiteralPath $conf)) { return }
  $lines = @(Get-Content -LiteralPath $conf)
  $kept = @($lines | Where-Object { $_ -notmatch '^\s*capture\s*=' -or $_ -match '^\s*capture\s*=\s*ddx\s*$' })
  if ($kept.Count -ne $lines.Count) {
    Set-Content -LiteralPath $conf -Value $kept -Encoding UTF8
    Write-Log 'Removed a capture method Norishine does not support; it chooses automatically.'
  }
}

function Import-Config([string]$Name, [string]$InstallDir) {
  $from = Join-Path $InstallDir 'config'
  if (-not (Test-HasState $from)) {
    Write-Log "No $Name configuration found in $from."
    return $false
  }
  if (Test-HasState $NorishineConfig) {
    Write-Log "Norishine already has a configuration; not importing $Name's."
    return $false
  }
  Copy-Config $from $NorishineConfig
  Update-Paths $NorishineConfig $InstallDir $NorishineRoot
  Remove-UnsupportedCapture $NorishineConfig
  Write-Log "Imported settings, apps and paired devices from $Name ($from)."
  return $true
}

function Set-SideBySidePort {
  $conf = Join-Path $NorishineConfig 'sunshine.conf'
  New-Item -ItemType Directory -Force -Path $NorishineConfig | Out-Null
  $lines = @()
  if (Test-Path -LiteralPath $conf) { $lines = @(Get-Content -LiteralPath $conf) }
  if ($lines | Where-Object { $_ -match '^\s*port\s*=' }) {
    Write-Log 'A port is already configured; keeping it.'
    return
  }
  $lines += $PortComment
  $lines += "port = $SideBySidePort"
  Set-Content -LiteralPath $conf -Value $lines -Encoding UTF8
  Write-Log "Norishine uses port $SideBySidePort (web UI https://localhost:$($SideBySidePort + 1))."
}

function Remove-SideBySidePort([string]$ConfigDir) {
  $conf = Join-Path $ConfigDir 'sunshine.conf'
  if (-not (Test-Path -LiteralPath $conf)) { return }
  $lines = @(Get-Content -LiteralPath $conf)
  $kept = @()
  for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -eq $PortComment -and $i + 1 -lt $lines.Count -and $lines[$i + 1] -eq "port = $SideBySidePort") {
      $i++
      continue
    }
    $kept += $lines[$i]
  }
  if ($kept.Count -ne $lines.Count) {
    Set-Content -LiteralPath $conf -Value $kept -Encoding UTF8
    Write-Log 'Removed the side-by-side port; Norishine uses the default port.'
  }
}

# Vibepollo's uninstaller leaves its folder and tools\ on the system PATH. Removes them when
# the folder no longer has its sunshine.exe.
function Remove-StalePathEntries([string]$InstallDir, [string]$Key = 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment') {
  if (-not $InstallDir -or (Test-Path -LiteralPath (Join-Path $InstallDir 'sunshine.exe'))) { return }
  $stale = @($InstallDir.TrimEnd('\'), (Join-Path $InstallDir.TrimEnd('\') 'tools'))
  $item = Get-Item -LiteralPath $Key
  # Read unexpanded, so %SystemRoot% style entries stay as they are.
  $raw = $item.GetValue('Path', '', 'DoNotExpandEnvironmentNames')
  $entries = @($raw -split ';')
  $kept = @($entries | Where-Object { $e = $_.TrimEnd('\'); -not ($stale | Where-Object { $_ -ieq $e }) })
  if ($kept.Count -ne $entries.Count) {
    Set-ItemProperty -LiteralPath $Key -Name 'Path' -Value ($kept -join ';') -Type ExpandString
    Write-Log "Removed $($entries.Count - $kept.Count) leftover PATH entries of $InstallDir."
  }
}

# Settings of Sunshine services that don't belong to the host being replaced, so they can
# be put back if its uninstaller deletes them: start type, account, dependencies and
# recovery (failure) actions, as stored under the service's registry key.
function Save-SunshineServices([string]$OtherInstallDir) {
  $saved = @()
  foreach ($name in $SunshineServices) {
    $props = Get-ItemProperty -LiteralPath "HKLM:\SYSTEM\CurrentControlSet\Services\$name" -ErrorAction SilentlyContinue
    if (-not $props -or -not $props.ImagePath) { continue }
    if ($OtherInstallDir -and $props.ImagePath -like "*$($OtherInstallDir.TrimEnd('\'))\*") { continue }
    $saved += [pscustomobject]@{
      Name = $name
      ImagePath = [string]$props.ImagePath
      Start = [int]$props.Start
      Delayed = [int]$props.DelayedAutostart
      ErrorControl = $props.ErrorControl
      DisplayName = $(if ($props.DisplayName) { [string]$props.DisplayName } else { $name })
      Description = [string]$props.Description
      Account = [string]$props.ObjectName
      DependsOn = @($props.DependOnService | Where-Object { $_ })
      FailureActions = $props.FailureActions
      FailureCommand = [string]$props.FailureCommand
      RebootMessage = [string]$props.RebootMessage
      FailureFlag = $props.FailureActionsOnNonCrashFailures
      Running = (Get-Service -Name $name -ErrorAction SilentlyContinue).Status -eq 'Running'
    }
  }
  return $saved
}

# The FailureActions registry value is a SERVICE_FAILURE_ACTIONS header (reset period,
# then reboot message, command, action count and actions as 32-bit fields) followed by
# one {type, delay ms} pair per action. Types: 0 none, 1 restart, 2 reboot, 3 run command.
# Returns the sc.exe failure arguments, or nothing when there are no actions.
function ConvertTo-FailureArguments($Service) {
  $bytes = [byte[]]$Service.FailureActions
  if (-not $bytes -or $bytes.Length -lt 20) { return @() }
  $reset = [BitConverter]::ToUInt32($bytes, 0)
  $count = [BitConverter]::ToUInt32($bytes, 12)
  if ($count -eq 0 -or $bytes.Length -lt 20 + 8 * $count) { return @() }
  $names = @{ 0 = ''; 1 = 'restart'; 2 = 'reboot'; 3 = 'run' }
  $actions = @()
  for ($i = 0; $i -lt $count; $i++) {
    $type = [BitConverter]::ToUInt32($bytes, 20 + 8 * $i)
    $delay = [BitConverter]::ToUInt32($bytes, 24 + 8 * $i)
    $actions += "$($names[[int]$type])/$delay"
  }
  $arguments = @('failure', $Service.Name, 'reset=', "$reset", 'actions=', ($actions -join '/'))
  if ($Service.FailureCommand) { $arguments += @('command=', $Service.FailureCommand) }
  if ($Service.RebootMessage) { $arguments += @('reboot=', $Service.RebootMessage) }
  return $arguments
}

function Restore-SunshineServices($Saved) {
  foreach ($service in @($Saved)) {
    $key = "HKLM:\SYSTEM\CurrentControlSet\Services\$($service.Name)"
    # A deleted service disappears once nothing holds it open.
    for ($i = 0; $i -lt 10 -and (Get-ItemProperty -LiteralPath $key -Name DeleteFlag -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Seconds 1 }
    if (Test-Path -LiteralPath $key) { continue }

    $binary = $service.ImagePath
    if ($binary -match '^\s*"([^"]+)"' -or $binary -match '^\s*(\S+)') { $binary = $Matches[1] }
    if (-not (Test-Path -LiteralPath $binary)) { continue }

    $startup = switch ($service.Start) { 2 { 'Automatic' } 4 { 'Disabled' } default { 'Manual' } }
    try {
      $create = @{ Name = $service.Name; BinaryPathName = $service.ImagePath; DisplayName = $service.DisplayName; StartupType = $startup }
      if ($service.DependsOn.Count) { $create.DependsOn = $service.DependsOn }
      New-Service @create | Out-Null
      if ($service.Description) { Set-ItemProperty -LiteralPath $key -Name Description -Value $service.Description }
      if ($service.Start -eq 2 -and $service.Delayed -eq 1) { & sc.exe config $service.Name start= delayed-auto | Out-Null }
      if ($null -ne $service.ErrorControl) {
        $errorControl = switch ([int]$service.ErrorControl) { 0 { 'ignore' } 2 { 'severe' } 3 { 'critical' } default { 'normal' } }
        & sc.exe config $service.Name error= $errorControl | Out-Null
      }
      # Built-in accounts need no password; other accounts can't be put back without one.
      if ($service.Account -and $service.Account -ne 'LocalSystem') {
        if ($service.Account -match '^NT AUTHORITY\(LocalService|NetworkService)$') {
          & sc.exe config $service.Name obj= $service.Account password= '' | Out-Null
        } else {
          Write-Log "$($service.Name) ran as $($service.Account); set that account again in Services (it now runs as LocalSystem)."
        }
      }
      $failure = ConvertTo-FailureArguments $service
      if ($failure.Count) { & sc.exe @failure | Out-Null }
      if ($null -ne $service.FailureFlag) { & sc.exe failureflag $service.Name ([int]$service.FailureFlag) | Out-Null }
      if ($service.Running) { Start-Service -Name $service.Name -ErrorAction SilentlyContinue }
      Write-Log "Put back the $($service.Name) service, which the uninstaller removed."
    } catch {
      Write-Log "Could not put back the $($service.Name) service: $_"
    }
  }
}

function Uninstall-OtherHost($OtherHost) {
  $entry = $OtherHost.Entry
  $sunshine = Save-SunshineServices $OtherHost.InstallDir
  if ($entry) {
    if ($entry.QuietUninstallString) {
      # Vibepollo: uninstall.exe /quiet (runs msiexec /x with /norestart)
      $command = $entry.QuietUninstallString
    } else {
      # Apollo: NSIS uninstaller. _?= makes it run in place so we can wait for it.
      $command = "$($entry.UninstallString) /S _?=$($OtherHost.InstallDir)"
    }
    Write-Log "Uninstalling $($OtherHost.Name): $command"
    if ($command -match '^\s*"([^"]+)"\s*(.*)$' -or $command -match '^\s*(\S+)\s*(.*)$') {
      $process = Start-Process -FilePath $Matches[1] -ArgumentList $Matches[2] -Wait -PassThru
      Write-Log "Uninstaller exit code $($process.ExitCode)."
    }
    # Some uninstallers hand off to msiexec and return early.
    for ($i = 0; $i -lt 120 -and (Get-OtherHost); $i++) { Start-Sleep -Seconds 1 }
    # NSIS run with _?= leaves its own uninstaller behind.
    $leftover = Join-Path $OtherHost.InstallDir 'Uninstall.exe'
    if (-not $entry.QuietUninstallString -and (Test-Path -LiteralPath $leftover)) {
      Remove-Item -LiteralPath $leftover -Force -ErrorAction SilentlyContinue
    }
    Restore-SunshineServices $sunshine
  }

  if (-not (Get-OtherHost)) {
    Write-Log "$($OtherHost.Name) is uninstalled."
    Remove-StalePathEntries $OtherHost.InstallDir
    return $true
  }

  # Could not uninstall: at least free the ports.
  if (Get-Service -Name 'ApolloService' -ErrorAction SilentlyContinue) {
    Stop-Service -Name 'ApolloService' -Force -ErrorAction SilentlyContinue
    Set-Service -Name 'ApolloService' -StartupType Disabled -ErrorAction SilentlyContinue
    Write-Log 'Could not uninstall it; stopped and disabled ApolloService instead.'
  }
  return $false
}

# Puts Norishine's configuration in the replaced host's config folder.
function Copy-ConfigBack($record) {
  $target = Join-Path $record.install_dir 'config'
  $marker = Join-Path $target $CopyMarker
  if (Test-Path -LiteralPath $marker) {
    # Our copy from an earlier uninstall (upgrades uninstall too): refresh it.
    Remove-Item -LiteralPath $target -Recurse -Force
  } elseif ((Test-Path -LiteralPath $target) -and (Get-ChildItem -LiteralPath $target -Force)) {
    $backup = "$target.before-norishine"
    if (Test-Path -LiteralPath $backup) { $backup = "$backup-$(Get-Date -Format yyyyMMddHHmmss)" }
    Move-Item -LiteralPath $target -Destination $backup
    Write-Log "Kept the old configuration as $backup."
  }
  Copy-Config $NorishineConfig $target
  Remove-Item -LiteralPath (Join-Path $target 'other_host.json') -Force -ErrorAction SilentlyContinue
  Export-LegacyState $target
  Set-Content -LiteralPath $marker -Value 'Copy of the Norishine configuration, made when Norishine was uninstalled or upgraded.'
  Update-Paths $target $NorishineRoot $record.install_dir
  Protect-Credentials $target
  Write-Log "Norishine's settings, apps and paired devices are in $target for $($record.host)."
}

# Dot-sourced (tests): define the functions only.
if ($MyInvocation.InvocationName -eq '.') { return }
if (-not $Action) { throw 'Specify -Action prepare, replace, side-by-side or restore.' }

switch ($Action) {
  'prepare' {
    $other = Get-OtherHost
    $record = Get-Record
    if ($other) {
      if ($record -and $record.mode -eq 'side-by-side') {
        Write-Log "$($other.Name) is installed; keeping both, as chosen before."
        Set-SideBySidePort
        exit 0
      }
      Write-Log "$($other.Name) is installed in $($other.InstallDir)."
      exit 10
    }
    # Not installed: carry over what an uninstalled Apollo or Vibepollo left behind.
    $leftover = Join-Path $env:ProgramFiles 'Apollo'
    if (-not (Test-HasState $NorishineConfig) -and (Test-HasState (Join-Path $leftover 'config'))) {
      if (Import-Config 'Apollo or Vibepollo' $leftover) {
        Set-Record 'replaced' 'Apollo or Vibepollo' $leftover
      }
    }
    exit 0
  }
  'replace' {
    $other = Get-OtherHost
    if (-not $other) { exit 0 }
    if (-not $other.InstallDir) { throw "Cannot find where $($other.Name) is installed." }
    Import-Config $other.Name $other.InstallDir | Out-Null
    Remove-SideBySidePort $NorishineConfig
    Set-Record 'replaced' $other.Name $other.InstallDir
    if (Uninstall-OtherHost $other) { exit 0 }
    exit 2
  }
  'side-by-side' {
    $other = Get-OtherHost
    Set-SideBySidePort
    Set-Record 'side-by-side' $(if ($other) { $other.Name } else { '' }) $(if ($other) { $other.InstallDir } else { '' })
    exit 0
  }
  'restore' {
    $record = Get-Record
    if (-not $record -or $record.mode -ne 'replaced' -or -not $record.install_dir) { exit 0 }
    if (Get-OtherHost) {
      Write-Log "$($record.host) is installed again; leaving its configuration alone."
      exit 0
    }
    if (-not (Test-HasState $NorishineConfig)) { exit 0 }
    Copy-ConfigBack $record
    exit 0
  }
}
