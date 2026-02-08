New-Item -Path "HKCU:\Control Panel\Desktop","HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" -Force | Out-Null
Set-ItemProperty "HKCU:\Control Panel\Desktop" -Name WallpaperStyle -Value 6 -Type "String"
Set-ItemProperty "HKCU:\Control Panel\Desktop" -Name JPEGImportQuality -Value 100 -Type "DWord"
Set-ItemProperty "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" -Name WallpaperChanger -Value "$env:LOCALAPPDATA\Programs\WallpaperChanger\WallpaperChanger.exe" -Type "String"