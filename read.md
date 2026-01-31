Отлично, тогда делаем финальную версию README.md, уже под взрослую структуру проекта (src / scripts / bin / docs), без хаоса, с нормальной навигацией и двумя путями установки.
Ниже — ГОТОВЫЙ README.md, который можно класть в корень репозитория без доработок.

⸻


# SLIDESHOW

Minimalistic Windows utility for rotating wallpapers across **multiple monitors**.  
Designed to be executed periodically via **Windows Task Scheduler** (e.g. every 5 minutes).

No UI. No background process. No services.

---

## Contents
- [Overview](#overview)
- [How it works](#how-it-works)
- [Project structure](#project-structure)
- [Installation paths](#installation-paths)
- [Path A — Build from source](#path-a--build-from-source)
- [Path B — Install prebuilt EXE](#path-b--install-prebuilt-exe)
- [Task Scheduler](#task-scheduler)
- [Wallpapers](#wallpapers)
- [Documentation](#documentation)
- [Troubleshooting](#troubleshooting)

---

## Overview

SLIDESHOW changes desktop wallpapers on Windows **across all monitors**.

Key features:
- each monitor always gets a **different image**
- all images are used **exactly once per cycle**
- order is **randomized**
- state is preserved between runs
- extremely fast startup
- designed for Task Scheduler execution

The executable performs one action and exits.

[⬆ Back to contents](#contents)

---

## How it works

1. Images are stored in a single folder
2. On first run, a randomized queue is created and saved to disk
3. Each execution:
   - assigns different wallpapers to each monitor
   - advances the queue cursor
4. When all images are used:
   - the queue is reshuffled
   - a new cycle begins

If no images are found, the program exits with an error.

[⬆ Back to contents](#contents)

---

## Project structure

```text
SLIDESHOW/
│
├─ README.md
├─ LICENSE
│
├─ src/
│   └─ SLIDESHOW.cpp
│
├─ scripts/
│   ├─ install.ps1
│   ├─ build.ps1
│   ├─ setup-wallpapers.ps1
│   ├─ setup-task.ps1
│   └─ uninstall.ps1
│
├─ bin/
│   └─ SLIDESHOW.exe
│
└─ docs/
    ├─ build.md
    ├─ powershell.md
    └─ task-scheduler.md

⬆ Back to contents￼

⸻

Installation paths

Choose one of the following:
	•	Path A — build everything yourself (recommended for developers)
	•	Path B — download a ready-to-use EXE
	•	Go to Path A — Build from source￼
	•	Go to Path B — Install prebuilt EXE￼

⸻

Path A — Build from source

1. Install C++ compiler (winget)

winget install Microsoft.VisualStudio.2022.BuildTools


⸻

2. Open x64 build environment

"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

Verify:

cl


⸻

3. Build the project

cd src

cl SLIDESHOW.cpp ^
  /std:c++20 ^
  /O2 /MT /DNDEBUG ^
  /EHsc /GR- ^
  ole32.lib shell32.lib ^
  /link ^
  /SUBSYSTEM:WINDOWS ^
  /OUT:SLIDESHOW.exe


⸻

4. Install executable

New-Item "C:\Program Files\SLIDESHOW" -ItemType Directory -Force
Move-Item SLIDESHOW.exe "C:\Program Files\SLIDESHOW\"
Unblock-File "C:\Program Files\SLIDESHOW\SLIDESHOW.exe"

⬆ Back to contents￼

⸻

Path B — Install prebuilt EXE

1. Download EXE

Use the provided binary:

bin/SLIDESHOW.exe

Copy it to:

C:\Program Files\SLIDESHOW\

Then unblock:

Unblock-File "C:\Program Files\SLIDESHOW\SLIDESHOW.exe"

⬆ Back to contents￼

⸻

Wallpapers

Wallpapers are available publicly:

https://github.com/GoHo-DohJi/WALLPAPER

Download without git:

$dest = "$env:LOCALAPPDATA\SLIDESHOW\BACKGROUND"
New-Item -ItemType Directory -Force -Path $dest

Invoke-WebRequest `
  "https://github.com/GoHo-DohJi/WALLPAPER/archive/refs/heads/main.zip" `
  -OutFile "$env:TEMP\wallpapers.zip"

Expand-Archive "$env:TEMP\wallpapers.zip" "$env:TEMP\wallpapers"

Copy-Item `
  "$env:TEMP\wallpapers\WALLPAPER-main\BACKGROUND\*" `
  $dest `
  -Recurse -Force

⬆ Back to contents￼

⸻

Task Scheduler

Create a task that runs every 5 minutes:

$action = New-ScheduledTaskAction `
  -Execute "C:\Program Files\SLIDESHOW\SLIDESHOW.exe"

$trigger = New-ScheduledTaskTrigger `
  -Once -At (Get-Date) `
  -RepetitionInterval (New-TimeSpan -Minutes 5)

Register-ScheduledTask `
  -TaskName "SLIDESHOW" `
  -Action $action `
  -Trigger $trigger `
  -RunLevel Highest `
  -Force

⬆ Back to contents￼

⸻

Documentation

Additional documentation:
	•	Build details￼
	•	PowerShell notes￼
	•	Task Scheduler details￼

⬆ Back to contents￼

⸻

Troubleshooting

Task does not run
	•	Ensure EXE is unblocked
	•	Check Task Scheduler history

Same wallpaper on all monitors
	•	Ensure enough images exist

Nothing happens
	•	Run EXE manually to test

⬆ Back to contents￼

---

## Итог

Этот README:
- масштабируется  
- читается  
- кликабельный  
- подходит и юзеру, и деву  
- выглядит как **реальный продакшн-проект**

Если хочешь — дальше могу:
- написать `install.ps1` «одной кнопкой»
- оформить `docs/*.md`
- подготовить GitHub Release
- привести build-флаги к LTO/PGO

Говори, что следующий шаг.