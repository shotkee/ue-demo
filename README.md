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

## Первый запуск

1. Клонируйте репозиторий в локальную папку.
2. Установите Unreal Engine 5.8.1 и C++-инструменты для своей платформы.
3. Откройте `Demo.uproject`.
4. Если Unreal Engine предложит пересобрать отсутствующий модуль `Demo`, подтвердите пересборку.

При первом запуске Unreal Engine автоматически создаст локальные каталоги `Binaries`, `DerivedDataCache`, `Intermediate` и `Saved`. Они не входят в репозиторий.

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
