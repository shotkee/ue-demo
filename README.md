# Demo

Проект арены на Unreal Engine 5.8.1 с управляемыми манекенами, локальной панелью команд и JSON/WebSocket-интерфейсом.

Проект содержит runtime-модуль `Demo`, стандартный Third Person Content Pack и карту арены. Стартовая карта проекта:

```text
/Game/Arena/Maps/Arena
```

## Требования

- Unreal Engine 5.8.1 (ветка 5.8).
- Git.
- Комплект C++-инструментов для используемой платформы:
  - Windows: Visual Studio 2022 или Visual Studio Build Tools 2022 с MSVC и Windows SDK;
  - macOS: Xcode с Command Line Tools.

Проверенная конфигурация Windows и подробная инструкция по установке находятся в [how_to_install_toolchain.md](how_to_install_toolchain.md).

## Автоматическая настройка Windows

Корневой скрипт `setup.ps1` подготавливает одну Windows-машину одновременно для Unreal Engine и TwitchBridge. Он устанавливает или проверяет Git, Node.js LTS, npm и Visual Studio Build Tools 2022 с компонентами для UE 5.8, создаёт локальный `.env`, устанавливает зависимости моста, запускает его тесты, генерирует файлы Unreal-проекта, собирает `DemoEditor` и выполняет Twitch-авторизацию.

Установка самого Unreal Engine остаётся интерактивной: сначала установите **Unreal Engine 5.8.1** через Epic Games Launcher. После этого откройте **PowerShell от имени администратора**, перейдите в корень клона и выполните:

```powershell
powershell -ExecutionPolicy Bypass -File .\setup.ps1
```

Скрипт безопасно запускать повторно: установленные компоненты будут проверены, существующая Twitch-авторизация сохранится, а локальный `.env` не попадёт в Git. Для нестандартного расположения Unreal Engine укажите каталог явно:

```powershell
powershell -ExecutionPolicy Bypass -File .\setup.ps1 `
  -UnrealEngineRoot "D:\Epic Games\UE_5.8"
```

Дополнительные параметры:

| Параметр | Назначение |
| --- | --- |
| `-SkipBuild` | сгенерировать файлы проекта, но не собирать `DemoEditor` |
| `-SkipTests` | не запускать автоматические тесты TwitchBridge |
| `-SkipTwitchAuthorization` | подготовить мост без интерактивной OAuth-авторизации |
| `-ResetBridgeEnvironment` | заново создать локальный `.env` из `.env.example` |

Для сценария на одной машине скрипт всегда устанавливает безопасный локальный адрес `ws://127.0.0.1:8080` одновременно в `.env` моста и локальном Unreal-конфиге. Отслеживаемый `Config/DefaultGame.ini` при этом не изменяется. Если Visual Studio Installer запросит перезагрузку, перезагрузите Windows и повторно выполните ту же команду.

## Развёртывание проекта из чистого клона

Ниже приведена ручная процедура для Windows. Для автоматической подготовки используйте `setup.ps1` из предыдущего раздела. Полная Visual Studio не требуется: достаточно Visual Studio Build Tools с MSVC и Windows SDK.

### 1. Клонирование репозитория

Откройте PowerShell, перейдите в каталог, где должен находиться проект, и выполните:

```powershell
git clone https://github.com/shotkee/ue-demo.git
Set-Location .\ue-demo
git status
```

Для приватного репозитория GitHub может запросить авторизацию через Git Credential Manager. После клонирования `git status` должен сообщать, что рабочая папка чистая.

Не размещайте новый клон внутри другого клона этого проекта.

### 2. Проверка инструментов

Убедитесь, что установлены:

- Unreal Engine 5.8.1;
- Visual Studio Build Tools 2022 с компонентами MSVC и Windows SDK;
- Git for Windows.

Подробная конфигурация Build Tools приведена в [how_to_install_toolchain.md](how_to_install_toolchain.md).

### 3. Сборка редактора проекта

Находясь в корне проекта, выполните команду из раздела [Сборка из PowerShell в Windows](#сборка-из-powershell-в-windows). Сборка должна завершиться сообщением `Result: Succeeded`.

### 4. Первый запуск

Откройте `Demo.uproject`. Если Windows попросит выбрать программу, укажите установленный Unreal Editor 5.8.1. Стартовая карта `/Game/Arena/Maps/Arena` должна открыться автоматически.

При первом запуске Unreal Engine создаст локальные каталоги `Binaries`, `DerivedDataCache`, `Intermediate` и `Saved`, а также подготовит шейдеры. Эти каталоги не входят в репозиторий; их появление не должно изменять `git status`.

### 5. Проверка карты

На карте `Arena` нажмите `Play` и убедитесь, что:

- камера начинает с обзорного ракурса всей арены и допускает свободное перемещение;
- панель `Arena Command Panel` появляется в свёрнутом состоянии;
- локальные команды создания, движения, действия, остановки и удаления манекена выполняются без ошибок.

### 6. Проверка WebSocket

Остановите Play. В PowerShell из корня проекта запустите тестовый сервер:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\WebSocketTestServer\RunArenaWebSocketTestServer.ps1 -Scenario Smoke
```

После сообщения `listening` снова нажмите `Play`. Панель должна показать `WS: Connected`, тестовый манекен должен появиться и выполнить команду движения, а сервер — вывести:

```text
WebSocket smoke test completed successfully.
```

Остановите Play и сервер сочетанием `Ctrl+C`. После проверки команда `git status --short` не должна показывать изменений отслеживаемых файлов.

## Настройка адреса TwitchBridge

Unreal Engine является WebSocket-клиентом и подключается к адресу TwitchBridge. Чтобы найти настройки в Unreal Editor:

1. Остановите `Play`, если проект запущен.
2. В верхнем меню выберите `Edit > Project Settings`.
3. В поле поиска слева введите `Arena WebSocket`.
4. В результатах откройте `Game > Arena WebSocket`.
5. Справа раскройте секцию `Connection`. Флаг `Allow Private Network Connections` расположен сразу под полем `Server URL`.

Если раздел или флаг не появился после сборки C++-модуля, полностью закройте и заново откройте Unreal Editor.

Если Unreal Engine и TwitchBridge работают на одном компьютере, оставьте значения по умолчанию:

```text
Server URL: ws://127.0.0.1:8080
Allow Private Network Connections: выключено
```

Если TwitchBridge работает на другом компьютере в той же локальной сети:

1. Включите `Allow Private Network Connections`.
2. Укажите приватный IPv4-адрес компьютера с мостом, например `ws://192.168.1.100:8080`.
3. В локальном `.env` моста укажите тот же IP и включите `ARENA_BRIDGE_ALLOW_PRIVATE_NETWORK_CONNECTIONS=true`.

Значения по умолчанию отслеживаются Git в `Config/DefaultGame.ini`. Локальное переопределение Unreal сохраняется в `Saved/Config/WindowsEditor/Game.ini`; каталог `Saved` намеренно исключён из Git, чтобы IP одного компьютера не применялся ко всем клонам проекта.

Полная инструкция по установке, smoke-тесту и настройке локальной сети приведена в [Tools/TwitchBridge/README.md](Tools/TwitchBridge/README.md).

## Сборка из PowerShell в Windows

Для сборки не требуется открывать `.sln` или полную Visual Studio: достаточно установленных Visual Studio Build Tools с MSVC и Windows SDK.

Закройте Unreal Editor, откройте PowerShell и перейдите в корень клонированного проекта — каталог, в котором находится `Demo.uproject`. Затем выполните:

```powershell
$projectFile = (Resolve-Path .\Demo.uproject).Path
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" DemoEditor Win64 Development -Project="$projectFile" -WaitMutex
```

Успешная сборка заканчивается строкой:

```text
Result: Succeeded
```

Если Unreal Engine установлен в другой каталог, измените путь к `Build.bat` в команде.

## Структура проекта

```text
Config/             общие настройки проекта
Content/            карты, Blueprint и другие ассеты Unreal
Source/             исходный C++-код
Demo.uproject       описание проекта и подключённых модулей
```

## Git и большие файлы

Генерируемые файлы Unreal Engine и IDE исключены через `.gitignore`. В репозиторий должны попадать исходники, конфигурация, `Demo.uproject` и необходимые файлы из `Content`.

Git LFS не используется. После импорта Third Person Content Pack каталог `Content` занимает около 132 МБ, а самый крупный отдельный ассет — около 20 МБ. Текущие ассеты хранятся в обычном Git; необходимость LFS следует повторно оценить только при появлении существенно более крупных файлов.

## Текущее состояние

- один C++-модуль `Demo`;
- создание и удаление манекенов по ID, очереди команд и обработка ошибок;
- локальная панель команд и локальный WebSocket-интерфейс;
- добавлены Manny, Quinn, базовая локомоция, атаки, реакции на попадание и другие стандартные анимации;
- создана карта арены с полом, физическими стенами, NavMesh, зрительской камерой, точкой случайного появления и именованными целями;
- включены Enhanced Input и редакторский Modeling Tools;
- для рендеринга настроены DX12/SM6, Lumen, Virtual Shadow Maps, Ray Tracing и Substrate.
