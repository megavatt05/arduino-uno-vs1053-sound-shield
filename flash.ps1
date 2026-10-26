param(
    [string]$Port = "",
    [string]$HexFile = "$PSScriptRoot\ci-artifacts\arduino-uno-vs1053-firmware\firmware.hex"
)

$avrdude = "C:\Program Files (x86)\LaserGRBL\Firmware\avrdude.exe"
$avrdudeConf = "C:\Program Files (x86)\LaserGRBL\Firmware\avrdude.conf"

if (-not (Test-Path $HexFile)) {
    Write-Error "Файл прошивки не найден: $HexFile"
    exit 1
}

# Автоопределение COM-порта, если не указан явно
if (-not $Port) {
    $ports = [System.IO.Ports.SerialPort]::GetPortNames()
    if ($ports.Count -eq 1) {
        $Port = $ports[0]
        Write-Host "[INFO] Обнаружен активный порт: $Port" -ForegroundColor Cyan
    } elseif ($ports.Count -gt 1) {
        # Ищем CH340 или Arduino
        $chPort = Get-PnpDevice -Class Ports -PresentOnly -ErrorAction SilentlyContinue | 
                  Where-Object { $_.FriendlyName -match "CH340|Arduino|USB Serial" } | 
                  ForEach-Object { 
                      if ($_.FriendlyName -match "(COM\d+)") { $matches[1] } 
                  } | Select-Object -First 1
        if ($chPort) {
            $Port = $chPort
            Write-Host "[INFO] Выбран порт устройства: $Port" -ForegroundColor Cyan
        } else {
            $Port = $ports[0]
            Write-Host "[INFO] Доступно несколько портов, выбран: $Port" -ForegroundColor Yellow
        }
    } else {
        Write-Host "[ОЖИДАНИЕ] Подключите плату Arduino Uno по USB к компьютеру..." -ForegroundColor Yellow
        for ($i = 0; $i -lt 30; $i++) {
            Start-Sleep -Seconds 1
            $ports = [System.IO.Ports.SerialPort]::GetPortNames()
            if ($ports.Count -gt 0) {
                $Port = $ports[0]
                Write-Host "`n[OK] Плата обнаружена на порту: $Port!" -ForegroundColor Green
                break
            }
            Write-Host -NoNewline "."
        }
        if (-not $Port) {
            Write-Error "Плата не подключена! Вставьте кабель USB Arduino Uno и повторите попытку."
            exit 1
        }
    }
}

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host " Прошивка Arduino Uno (ATmega328P) через avrdude" -ForegroundColor Cyan
Write-Host " Порт: $Port" -ForegroundColor Cyan
Write-Host " Файл: $HexFile" -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan

# Попытка 1: Скорость 115200 (Arduino Uno / Optiboot)
Write-Host "`n[1/2] Прошивка на скорости 115200 бод (стандарт Uno / Optiboot)..." -ForegroundColor Yellow
& $avrdude -C $avrdudeConf -c arduino -p m328p -P $Port -b 115200 -D -U flash:w:"$HexFile":i

if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[2/2] Не удалось на 115200. Пробуем скорость 57600 бод (старый загрузчик)..." -ForegroundColor Yellow
    Start-Sleep -Seconds 1
    & $avrdude -C $avrdudeConf -c arduino -p m328p -P $Port -b 57600 -D -U flash:w:"$HexFile":i
}

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n[УСПЕХ] Прошивка успешно загружена в Arduino Uno!" -ForegroundColor Green
    Write-Host "Откройте Serial Monitor на скорости 115200 бод для управления." -ForegroundColor Cyan
} else {
    Write-Host "`n[ОШИБКА] Не удалось прошить микроконтроллер. Проверьте подключение и порт." -ForegroundColor Red
    exit 1
}
