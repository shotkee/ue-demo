# UE 5.8: установка Windows toolchain и сборка Demo

Эта инструкция фиксирует рабочую конфигурацию, на которой проект `Demo` для Unreal Engine 5.8 успешно собрался **без установки полной Visual Studio IDE**.

Проверенная конфигурация:

- Unreal Engine: **5.8**
- Visual Studio Build Tools 2022: **17.14.39**
- MSVC toolset: **14.44.35207**
- `cl.exe`: **19.44.35228 x64**
- Windows SDK: **10.0.22621.0**
- .NET Framework SDK: **4.8**
- .NET Framework Targeting Pack: **4.8**
- проект: `C:\Share\Demo\Demo\Demo.uproject`

> Полная Visual Studio IDE для сборки этого проекта не требуется. Нужны Visual Studio **Build Tools**, MSVC, Windows SDK и .NET Framework developer components.

---

## 1. Что именно требуется установить

Для UE 5.8 в этой конфигурации используются следующие компоненты Visual Studio Build Tools:

```text
Microsoft.VisualStudio.Workload.VCTools
Microsoft.VisualStudio.Component.VC.Tools.x86.x64
Microsoft.VisualStudio.Component.VC.14.44.17.14.x86.x64
Microsoft.VisualStudio.Component.Windows11SDK.22621
Microsoft.Net.Component.4.8.SDK
Microsoft.Net.Component.4.8.TargetingPack
Microsoft.Net.ComponentGroup.4.8.DeveloperTools
```

Назначение компонентов:

| Компонент | Назначение |
|---|---|
| `Microsoft.VisualStudio.Workload.VCTools` | основной workload C++ Build Tools |
| `Microsoft.VisualStudio.Component.VC.Tools.x86.x64` | стандартные C++ build tools для x86/x64 |
| `Microsoft.VisualStudio.Component.VC.14.44.17.14.x86.x64` | конкретный MSVC v143 toolset 14.44 |
| `Microsoft.VisualStudio.Component.Windows11SDK.22621` | Windows SDK 10.0.22621.0 |
| `Microsoft.Net.Component.4.8.SDK` | .NET Framework 4.8 SDK |
| `Microsoft.Net.Component.4.8.TargetingPack` | .NET Framework 4.8 Targeting Pack |
| `Microsoft.Net.ComponentGroup.4.8.DeveloperTools` | полный набор developer tools для .NET Framework 4.8 |

Последние три компонента особенно важны: без `.NET Framework SDK` UE 5.8 может остановить сборку на модуле `SwarmInterface` с ошибкой `Could not find NetFxSDK install dir`.

---

## 2. Автоматическая установка Build Tools из PowerShell

Открой **PowerShell от имени администратора**.

Сначала скачиваем официальный bootstrapper Visual Studio Build Tools 2022 во временную папку:

```powershell
$installer = "$env:TEMP\vs_BuildTools.exe"

Invoke-WebRequest `
  -Uri "https://aka.ms/vs/17/release/vs_BuildTools.exe" `
  -OutFile $installer
```

После этого запускаем установку всего требуемого набора:

```powershell
$arguments = @(
  '--passive',
  '--wait',
  '--norestart',
  '--add', 'Microsoft.VisualStudio.Workload.VCTools',
  '--add', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
  '--add', 'Microsoft.VisualStudio.Component.VC.14.44.17.14.x86.x64',
  '--add', 'Microsoft.VisualStudio.Component.Windows11SDK.22621',
  '--add', 'Microsoft.Net.Component.4.8.SDK',
  '--add', 'Microsoft.Net.Component.4.8.TargetingPack',
  '--add', 'Microsoft.Net.ComponentGroup.4.8.DeveloperTools'
)

$process = Start-Process `
  -FilePath $installer `
  -ArgumentList $arguments `
  -Wait `
  -PassThru

$process.ExitCode
```

`--passive` выбран специально вместо `--quiet`, чтобы был виден прогресс установки, но пользователь не мог случайно изменить набор компонентов.

Нормальный код завершения:

```text
0
```

Если установщик запросил перезагрузку, перезагрузи Windows перед дальнейшими проверками.

### Полностью тихая установка

Если UI вообще не нужен, `--passive` можно заменить на:

```text
--quiet
```

В таком случае PowerShell с `Start-Process -Wait` не вернёт приглашение `PS C:\...>` до окончания установки. Это нормально.

---

## 3. Проверка Visual Studio Build Tools

После установки должен существовать `vswhere.exe`:

```powershell
Test-Path "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
```

Ожидается:

```text
True
```

Проверяем зарегистрированный экземпляр Build Tools:

```powershell
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
  -products * `
  -all `
  -format json
```

Для рабочей установки должны присутствовать значения:

```json
"installationPath": "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools",
"isComplete": true,
"isLaunchable": true
```

Если `isComplete` равен `false`, установка ещё не закончилась.

---

## 4. Проверка C++ toolchain

Проверяем, что Build Tools содержит общий C++ component:

```powershell
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
  -products * `
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
  -property installationPath

$vs
```

Ожидаемый путь:

```text
C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
```

Проверяем конкретный MSVC 14.44:

```powershell
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
  -products * `
  -requires Microsoft.VisualStudio.Component.VC.14.44.17.14.x86.x64 `
  -property installationPath
```

Результат должен быть тем же:

```text
C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
```

### Проверка `cl.exe`

```powershell
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && cl'
```

На проверенной машине вывод содержит:

```text
Visual Studio 2022 Developer Command Prompt v17.14.39
Оптимизирующий компилятор Microsoft (R) C/C++ версии 19.44.35228 для x64
```

### Проверка compiler + linker + resource compiler

```powershell
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && where cl && where link && where rc'
```

В рабочей конфигурации были обнаружены:

```text
C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe
C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\link.exe
C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\rc.exe
```

Если все три файла находятся, базовая Windows C++ toolchain установлена корректно.

---

## 5. Проверка .NET Framework 4.8 SDK

UE 5.8 при сборке editor target использует модули, которым может понадобиться классический `.NET Framework SDK`. Bundled `.NET SDK`, который UE показывает строкой вроде:

```text
Using bundled DotNet SDK version: 10.0 win-x64
```

не заменяет `NETFXSDK`.

Проверяем SDK:

```powershell
Test-Path "C:\Program Files (x86)\Windows Kits\NETFXSDK\4.8"
```

Ожидается:

```text
True
```

Проверяем Targeting Pack/reference assemblies:

```powershell
Test-Path "C:\Program Files (x86)\Reference Assemblies\Microsoft\Framework\.NETFramework\v4.8"
```

Ожидается:

```text
True
```

---

## 6. Создание Demo C++ проекта в UE 5.8

После установки toolchain **полностью закрой Unreal Editor**, если он был запущен во время установки. Затем запусти UE 5.8 заново через Epic Games Launcher.

В Unreal Project Browser выбери:

```text
Games
→ Blank
→ C++
→ Starter Content: Off
```

Для проекта из этой инструкции используется имя:

```text
Demo
```

Файл проекта в проверенной конфигурации находится здесь:

```text
C:\Share\Demo\Demo\Demo.uproject
```

После создания проекта UE запускает `UnrealBuildTool` и пытается собрать `DemoEditor`.

---

## 7. Ручная сборка Demo из PowerShell

Editor запускать не обязательно. Проект можно собрать напрямую через UE `Build.bat`.

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" `
  Development Win64 `
  -Project="C:\Share\Demo\Demo\Demo.uproject" `
  -TargetType=Editor `
  -Progress `
  -NoEngineChanges `
  -NoHotReloadFromIDE
```

Смысл основных параметров:

| Параметр | Значение |
|---|---|
| `Development` | development-конфигурация сборки |
| `Win64` | целевая Windows x64 платформа |
| `-Project=...` | путь к `.uproject` |
| `-TargetType=Editor` | собирается editor target проекта |
| `-Progress` | вывод прогресса |
| `-NoEngineChanges` | не разрешать сборке модифицировать исходники Engine |
| `-NoHotReloadFromIDE` | обычная полноценная сборка без IDE hot reload |

Успешное завершение выглядит как:

```text
Result: Succeeded
```

После успешной сборки можно открыть:

```text
C:\Share\Demo\Demo\Demo.uproject
```

Visual Studio IDE для запуска проекта не требуется.

---

## 8. Типовые проблемы

### UE пишет «Компилятор не найден»

Сначала полностью закрой `UnrealEditor.exe` и запусти UE заново. UE, запущенный до установки Build Tools, может не увидеть только что появившийся toolchain.

Проверить процессы:

```powershell
Get-Process |
  Where-Object {
    $_.ProcessName -match 'Unreal|UE5|EpicGamesLauncher'
  } |
  Select-Object ProcessName, Id, Path
```

Далее повторно проверь `vswhere`, `cl`, `link` и `rc` командами выше.

### Ошибка `Could not find NetFxSDK install dir`

Полная ошибка может выглядеть так:

```text
Unable to instantiate module 'SwarmInterface':
Could not find NetFxSDK install dir;
this will prevent SwarmInterface from installing.
Install a version of .NET Framework SDK at 4.6.0 or higher.
```

Для проверенной конфигурации проблема была устранена установкой:

```text
Microsoft.Net.Component.4.8.SDK
Microsoft.Net.Component.4.8.TargetingPack
Microsoft.Net.ComponentGroup.4.8.DeveloperTools
```

После установки обязательно проверь:

```powershell
Test-Path "C:\Program Files (x86)\Windows Kits\NETFXSDK\4.8"
Test-Path "C:\Program Files (x86)\Reference Assemblies\Microsoft\Framework\.NETFramework\v4.8"
```

Оба результата должны быть `True`.

### Как понять, идёт ли Visual Studio Installer

```powershell
Get-Process |
  Where-Object {
    $_.ProcessName -match 'setup|installer|vs_'
  } |
  Select-Object ProcessName, Id, CPU, StartTime
```

При активной установке обычно видны процессы `setup.exe`, `vs_BuildTools.exe` или `vs_setup_bootstrapper.exe`.

### Как смотреть installer logs

Журналы Visual Studio Installer находятся во временной папке пользователя.

Показать последние журналы:

```powershell
Get-ChildItem $env:TEMP -Filter "dd_setup*.log" |
  Sort-Object LastWriteTime -Descending |
  Select-Object -First 5 Name, Length, LastWriteTime
```

Следить за изменениями текущего журнала:

```powershell
$log = Get-ChildItem $env:TEMP -Filter "dd_setup*.log" |
  Sort-Object LastWriteTime -Descending |
  Select-Object -First 1

Get-Content $log.FullName -Tail 30 -Wait
```

Выйти из режима просмотра:

```text
Ctrl+C
```

### UnrealBuildTool log

Если сборка UE падает, основной лог находится здесь:

```text
C:\Users\<USER>\AppData\Local\UnrealBuildTool\Log.txt
```

Для текущего пользователя:

```text
C:\Users\vit\AppData\Local\UnrealBuildTool\Log.txt
```

Последние 100 строк:

```powershell
Get-Content "$env:LOCALAPPDATA\UnrealBuildTool\Log.txt" -Tail 100
```

---

## 9. Быстрая итоговая проверка окружения

Эти команды удобно выполнить перед диагностикой проблем со сборкой:

```powershell
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
  -products * `
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
  -property installationPath

"Build Tools: $vs"

cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && cl'

cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && where cl && where link && where rc'

"NETFXSDK 4.8: $(Test-Path 'C:\Program Files (x86)\Windows Kits\NETFXSDK\4.8')"
".NET 4.8 refs: $(Test-Path 'C:\Program Files (x86)\Reference Assemblies\Microsoft\Framework\.NETFramework\v4.8')"
```

Для проверенного окружения ожидается:

```text
Build Tools: C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
MSVC: 19.44.xxxxx x64
cl.exe: найден
link.exe: найден
rc.exe: найден
NETFXSDK 4.8: True
.NET 4.8 refs: True
```

После этого команда сборки из раздела 7 должна успешно собрать `DemoEditor`.

---

## 10. Размещение файла в репозитории

Этот файл должен лежать в корне проекта рядом с `.uproject`:

```text
C:\Share\Demo\Demo\
├── how_to_install_toolchain.md
├── Demo.uproject
├── Config\
├── Content\
└── Source\
```

Его имеет смысл добавить в Git вместе с проектом, чтобы новый разработчик мог подготовить Windows-машину без установки полной Visual Studio IDE.

---

## Официальные источники

- Microsoft — Visual Studio Build Tools workload/component IDs:  
  https://learn.microsoft.com/en-us/visualstudio/install/workload-component-id-vs-build-tools?view=visualstudio
- Microsoft — command-line parameters for Visual Studio Installer:  
  https://learn.microsoft.com/en-us/visualstudio/install/use-command-line-parameters-to-install-visual-studio?view=visualstudio
- Microsoft — command-line installation examples and `--wait`:  
  https://learn.microsoft.com/en-us/visualstudio/install/command-line-parameter-examples?view=visualstudio
- Epic Games — Unreal Engine 5.8 Release Notes:  
  https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes
- Epic Games — Visual Studio setup for Unreal Engine C++ projects:  
  https://dev.epicgames.com/documentation/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine
