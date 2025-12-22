# HMS_BLE Example Setup Script
# Run this after cloning the repository to fetch required modules

Write-Host "Setting up HMS_BLE Example for nRF52..." -ForegroundColor Cyan

# Create Modules directory if it doesn't exist
if (-not (Test-Path "Modules")) {
    New-Item -ItemType Directory -Path "Modules" | Out-Null
}

# Clone HMS_BLE if not present
if (-not (Test-Path "Modules/HMS_BLE")) {
    Write-Host "Cloning HMS_BLE..." -ForegroundColor Yellow
    git clone https://github.com/Hamas888/HMS_BLE.git Modules/HMS_BLE
} else {
    Write-Host "HMS_BLE already exists, updating..." -ForegroundColor Yellow
    Push-Location "Modules/HMS_BLE"
    git pull
    Pop-Location
}

# Clone ChronoLog if not present
if (-not (Test-Path "Modules/ChronoLog")) {
    Write-Host "Cloning ChronoLog..." -ForegroundColor Yellow
    git clone https://github.com/Hamas888/ChronoLog.git Modules/ChronoLog
} else {
    Write-Host "ChronoLog already exists, updating..." -ForegroundColor Yellow
    Push-Location "Modules/ChronoLog"
    git pull
    Pop-Location
}

Write-Host ""
Write-Host "Setup complete!" -ForegroundColor Green
Write-Host "You can now build using nRF Connect extension or run:" -ForegroundColor Cyan
Write-Host "  west build -b nrf52dk/nrf52832" -ForegroundColor White
