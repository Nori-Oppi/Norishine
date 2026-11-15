# Uninstall the ViGEm Bus Driver through its Windows Installer entry.
# Reads the uninstall registry keys instead of Win32_Product, which is slow and makes
# Windows Installer verify every installed MSI product.
$uninstallKeys = @(
    'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
    'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*'
)
$products = @(Get-ItemProperty -Path $uninstallKeys -ErrorAction SilentlyContinue |
    Where-Object { $_.DisplayName -eq 'ViGEm Bus Driver' -and $_.PSChildName -match '^\{[0-9A-Fa-f-]+\}$' })

if ($products.Count -eq 0) {
    Write-Warning "ViGEm Bus Driver not found"
    exit 0
}

foreach ($product in $products) {
    $process = Start-Process -FilePath 'msiexec.exe' -ArgumentList '/x', $product.PSChildName, '/passive', '/norestart' `
        -Wait -PassThru
    if ($process.ExitCode -eq 0 -or $process.ExitCode -eq 3010) {
        Write-Information "ViGEm Bus Driver $($product.DisplayVersion) uninstalled successfully"
    } else {
        Write-Warning "Uninstalling ViGEm Bus Driver failed with exit code $($process.ExitCode)"
    }
}
