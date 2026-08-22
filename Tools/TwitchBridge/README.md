# Twitch Bridge

`TwitchBridge` — локальная Node.js/TypeScript-программа между Twitch Chat и ареной Unreal Engine. Программа принимает одно соединение Unreal Engine, передаёт JSON-команды, получает их статусы и безопасно авторизуется в Twitch через Device Code Grant.

По умолчанию сервер привязан только к loopback-адресу и слушает `ws://127.0.0.1:8080`. Доступ с приватного IPv4-адреса локальной сети включается отдельно.

## Требования

- Node.js 20.6 или новее;
- npm, входящий в установку Node.js;
- собранный проект Unreal Engine.

## Установка

Откройте PowerShell в корне репозитория и выполните:

```powershell
Set-Location .\Tools\TwitchBridge
npm ci
npm run build
```

В Terminal на macOS используются аналогичные команды:

```bash
cd Tools/TwitchBridge
npm ci
npm run build
```

## Создание и настройка `.env`

TwitchBridge читает слушаемый IP и порт из файла `.env`, расположенного рядом с `package.json`. После клонирования создайте локальный файл из отслеживаемого шаблона.

PowerShell:

```powershell
Copy-Item .env.example .env
```

Terminal на macOS:

```bash
cp .env.example .env
```

Если TwitchBridge и Unreal Engine работают на одном компьютере, оставьте безопасные локальные значения:

```dotenv
ARENA_BRIDGE_HOST=127.0.0.1
ARENA_BRIDGE_PORT=8080
ARENA_BRIDGE_ALLOW_PRIVATE_NETWORK_CONNECTIONS=false
```

Если TwitchBridge работает на Mac, а Unreal Engine — на Windows в той же локальной сети, укажите приватный IPv4-адрес Mac:

```dotenv
ARENA_BRIDGE_HOST=192.168.1.100
ARENA_BRIDGE_PORT=8080
ARENA_BRIDGE_ALLOW_PRIVATE_NETWORK_CONNECTIONS=true
```

Замените `192.168.1.100` на фактический адрес Mac. Его можно найти в `System Settings > Network > Wi-Fi > Details > TCP/IP`. После запуска событие `arena_bridge_listening` должно содержать ожидаемый адрес, например:

```json
{"event":"arena_bridge_listening","url":"ws://192.168.1.100:8080"}
```

Если IP Mac изменился, обновите одновременно `ARENA_BRIDGE_HOST` в `.env` и `Server URL` в Unreal Editor.

Файл `.env` предназначен только для локальных настроек и исключён из Git. OAuth-токены в `.env` и каталоге репозитория не записываются: мост хранит их в личном домашнем каталоге пользователя.

## Подключение Twitch после клонирования репозитория

Пользователю клона не требуется собственный Twitch Developer-аккаунт, регистрация нового приложения или `Client Secret`. В репозитории уже находится публичный `Client ID` приложения `Shotkee Arena Command Bridge`. Каждый пользователь авторизует свой Twitch-аккаунт, а секретные токены создаются отдельно только на его компьютере.

### Windows PowerShell

Из корня клонированного репозитория выполните:

```powershell
Set-Location .\Tools\TwitchBridge
npm ci
Copy-Item .env.example .env
npm run build
npm run auth
```

### macOS Terminal

Из корня клонированного репозитория выполните:

```bash
cd Tools/TwitchBridge
npm ci
cp .env.example .env
npm run build
npm run auth
```

Если `.env` уже существует, повторно копировать шаблон не нужно. Оставьте значение `TWITCH_CLIENT_ID`, находящееся в шаблоне: это общий публичный идентификатор приложения проекта. Параметры `ARENA_BRIDGE_HOST` и `ARENA_BRIDGE_ALLOW_PRIVATE_NETWORK_CONNECTIONS` настройте по разделу «Создание и настройка `.env`» выше.

Команда `npm run auth` напечатает адрес Twitch и временный код. После этого:

1. Откройте напечатанный адрес в браузере.
2. Войдите под Twitch-аккаунтом владельца канала, чат которого должен слушать мост.
3. Введите временный код, если Twitch его запросит.
4. Подтвердите доступ `user:read:chat`.
5. Вернитесь в Terminal или PowerShell и дождитесь события `twitch_authorization_ready`.

При стандартной настройке поля `authorizedUserId` и `channelUserId` должны совпадать. Это подтверждает, что мост авторизован под владельцем выбранного канала.

Мост запрашивает только минимальный scope `user:read:chat`. Scope `user:write:chat` не запрашивается, пока проект не отправляет ответы в чат.

Успешный результат содержит технические идентификаторы авторизованного пользователя и выбранного канала:

```json
{
  "event": "twitch_authorization_ready",
  "authorizedUserId": "123456",
  "authorizedUserLogin": "channel_login",
  "channelUserId": "123456",
  "channelLogin": "channel_login",
  "scopes": ["user:read:chat"]
}
```

По умолчанию мост использует канал авторизованного пользователя, поэтому `TWITCH_CHANNEL_LOGIN` следует оставить пустым. Явный логин другого канала можно указать без `#`, но для чтения его чата аккаунту могут понадобиться дополнительные права Twitch:

```dotenv
TWITCH_CHANNEL_LOGIN=channel_login
```

Проверить сохранённую авторизацию можно командой:

```powershell
npm run auth:status
```

Для обычного запуска моста после успешной авторизации используется:

```powershell
npm run start:env
```

На этапе `TW-2` эта команда проверяет Twitch-токен и запускает локальный WebSocket-мост в ручном режиме. Получение реальных сообщений Twitch Chat через EventSub добавляется на этапе `TW-3`.

При запуске мост также проверяет токен через Twitch и при необходимости обновляет его. Если доступ был отозван, refresh-токен истёк или изменился `Client ID`, журнал покажет событие `twitch_reauthorization_required` с предложением снова выполнить `npm run auth`.

Секретные access- и refresh-токены хранятся вне репозитория:

- macOS/Linux: `~/.arena-twitch-bridge/twitch-auth.json`;
- Windows: `%USERPROFILE%\.arena-twitch-bridge\twitch-auth.json`.

Не копируйте этот файл между компьютерами и не публикуйте его. Для независимого форка можно зарегистрировать собственное публичное Twitch-приложение и переопределить `TWITCH_CLIENT_ID` в `.env`; `Client Secret` для Device Code Grant не нужен.

## Автоматический smoke-тест без Twitch

1. Закройте старый PowerShell WebSocket-сервер, чтобы освободить порт `8080`.
2. Запустите мост:

   ```powershell
   npm run start:env -- --smoke
   ```

3. Дождитесь события `arena_bridge_listening`.
4. Откройте карту `Arena` в Unreal Editor и нажмите `Play`.

Панель должна показать `WS: Connected`. Мост создаст тестовый манекен и отправит его к точке `center`. Успешное завершение отмечается структурированной строкой с событием `smoke_completed` и сообщением:

```text
Twitch bridge smoke test completed successfully.
```

После теста соединение остаётся открытым. Для остановки нажмите `Ctrl+C`.

## Ручной режим без Twitch

Запустите мост без аргументов:

```powershell
npm run start:env
```

Вставляйте по одной JSON-команде в строку. Например:

```json
{"version":1,"requestId":"manual-spawn-alice","actorId":"manual:alice","command":"spawn","parameters":{"displayName":"Alice"}}
```

Если Unreal Engine временно отключён, команда помещается в ограниченную очередь. После восстановления соединения она отправляется автоматически. Просроченные команды удаляются, чтобы старые действия не выполнялись после длительного отключения.

## Запуск на отдельном Mac в локальной сети

По умолчанию сетевые подключения запрещены. Чтобы Unreal Engine на Windows подключался к мосту на Mac:

1. Узнайте локальный IPv4-адрес Mac в `System Settings > Network > Wi-Fi > Details > TCP/IP`.
2. В локальном `.env` на Mac укажите этот адрес и явно разрешите приватную сеть:

   ```dotenv
   ARENA_BRIDGE_HOST=192.168.1.100
   ARENA_BRIDGE_PORT=8080
   ARENA_BRIDGE_ALLOW_PRIVATE_NETWORK_CONNECTIONS=true
   ```

3. Запустите мост на Mac командой `npm run start:env -- --smoke`.
4. В Unreal Editor включите `Edit > Project Settings > Game > Arena WebSocket > Allow Private Network Connections`.
5. В расположенном рядом поле `Server URL` укажите `ws://192.168.1.100:8080`, заменив адрес на IP своего Mac.

При первом запуске macOS может запросить разрешение входящих подключений для Node.js — разрешите их для частной локальной сети. Не используйте `0.0.0.0`, публичный IP и проброс порта `8080` на роутере.

## Настройки

Настройки читаются из переменных окружения или локального `.env` при использовании `start:env`:

- `ARENA_BRIDGE_HOST` — loopback-адрес или явно разрешённый приватный IPv4-адрес, по умолчанию `127.0.0.1`;
- `ARENA_BRIDGE_PORT` — порт сервера, по умолчанию `8080`;
- `ARENA_BRIDGE_ALLOW_PRIVATE_NETWORK_CONNECTIONS` — разрешает конкретный приватный IPv4-адрес из `ARENA_BRIDGE_HOST`;
- `ARENA_BRIDGE_QUEUE_LIMIT` — максимальный размер очереди переподключения;
- `ARENA_BRIDGE_QUEUE_TTL_MS` — срок жизни команды в очереди;
- `ARENA_BRIDGE_STATUS_TIMEOUT_MS` — таймаут smoke-теста;
- `ARENA_BRIDGE_MODE` — `stdin` или `smoke`.
- `TWITCH_CLIENT_ID` — публичный идентификатор Twitch-приложения; в шаблоне уже задан общий идентификатор проекта;
- `TWITCH_CHANNEL_LOGIN` — необязательный логин выбранного канала; если поле пустое, используется канал авторизованного пользователя.

Если порт изменён, укажите тот же URL в Unreal Editor: `Edit > Project Settings > Game > Arena WebSocket > Server URL`.

## Проверки

```powershell
npm test
```

Тесты проверяют очередь до подключения, двустороннюю передачу сообщений, запрет второго активного соединения Unreal Engine, Device Code OAuth, обновление токенов и отсутствие `Client Secret` в запросах.
