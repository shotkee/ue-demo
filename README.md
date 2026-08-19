# Demo

Минимальный C++-проект на Unreal Engine 5.8.1.

Сейчас проект содержит базовый runtime-модуль `Demo` без игровой логики и пользовательских ассетов. В качестве стартовой используется стандартная карта Unreal Engine:

```text
/Engine/Maps/Templates/OpenWorld
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

## Структура проекта

```text
Config/             общие настройки проекта
Content/            карты, Blueprint и другие ассеты Unreal
Source/             исходный C++-код
Demo.uproject       описание проекта и подключённых модулей
```

## Git и большие файлы

Генерируемые файлы Unreal Engine и IDE исключены через `.gitignore`. В репозиторий должны попадать исходники, конфигурация, `Demo.uproject` и необходимые файлы из `Content`.

Сейчас больших бинарных ассетов нет, поэтому Git LFS не требуется. Перед добавлением `.uasset`, `.umap` и других крупных бинарных ресурсов рекомендуется установить Git LFS и добавить соответствующие правила в `.gitattributes`:

```gitattributes
*.uasset filter=lfs diff=lfs merge=lfs -text
*.umap filter=lfs diff=lfs merge=lfs -text
```

## Текущее состояние

- один C++-модуль `Demo`;
- игровая логика пока не реализована;
- пользовательские карты и ассеты пока отсутствуют;
- включены Enhanced Input и редакторский Modeling Tools;
- для рендеринга настроены DX12/SM6, Lumen, Virtual Shadow Maps, Ray Tracing и Substrate.
