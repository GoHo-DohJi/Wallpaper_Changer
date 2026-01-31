

"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

cd {PATH_TO_PROJECT}\SLIDESHOW

rc src\app.rc

cl /nologo ^
   /std:c++17 ^
   /O2 /GL ^
   /DNDEBUG ^
   /MT ^
   /W4 ^
   /EHsc ^
   src\WallpaperChanger.cpp src\app.res ^
   /link ^
   /LTCG ^
   /INCREMENTAL:NO ^
   /SUBSYSTEM:WINDOWS ^
   /OPT:REF /OPT:ICF





-----------------------------------------------------
!!! ADD TO REALSES EXE PROGRAM

PATH PROJECT 
%LOCALAPPDATA%\Programs\WallpaperChanger\WallpaperChanger.exe

AUTORUN
HKCU\Software\Microsoft\Windows\CurrentVersion\Run
  WallpaperChanger = "%LOCALAPPDATA%\Programs\WallpaperChanger\WallpaperChanger.exe"




SCRIPT TO DOWNLOAD BACKGROUND 
# Target directory (used by SLIDESHOW.exe)
$target = "$env:LOCALAPPDATA\SLIDESHOW\BACKGROUND"
New-Item -ItemType Directory -Force -Path $target | Out-Null

# Temporary paths
$tempZip = "$env:TEMP\WALLPAPER.zip"
$tempDir = "$env:TEMP\WALLPAPER_extract"

# Download repository as ZIP
Invoke-WebRequest `
  -Uri "https://github.com/GoHo-DohJi/WALLPAPER/archive/refs/heads/main.zip" `
  -OutFile $tempZip

# Extract ZIP
Expand-Archive -Force $tempZip $tempDir

# Copy wallpapers
Copy-Item `
  "$tempDir\WALLPAPER-main\BACKGROUND\*" `
  $target `
  -Recurse -Force

# Cleanup
Remove-Item $tempZip -Force
Remove-Item $tempDir -Recurse -Force
