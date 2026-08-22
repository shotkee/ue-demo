# Arena JSON Protocol

Текущая версия протокола: `1`.

Протокол передаёт только данные. Значение `command` выбирается из фиксированного списка и никогда не интерпретируется как имя функции Unreal Engine.

## Ограничения

- максимальная длина JSON-сообщения: 16 384 символа;
- максимальная длина `requestId`, `actorId`, `targetId`, `interactionPointId` и `actionId`: 128 символов;
- максимальная длина `displayName`: 64 символа;
- `displayNameColor`, если указан, должен иметь формат `#RRGGBB`;
- неизвестные поля, команды и значения параметров отклоняются;
- один `requestId` может выполнить команду только один раз в пределах игровой сессии.

## Входящее сообщение

Обязательные поля верхнего уровня:

| Поле | Тип | Описание |
| --- | --- | --- |
| `version` | integer | Должно быть равно `1`. |
| `requestId` | string | Непустой уникальный ID запроса. |
| `actorId` | string | Непустой ID участника, выполняющего команду. Для `spawn` это ID нового участника. |
| `command` | string | Одно из разрешённых значений из таблицы ниже. |

Поле `parameters` необязательно только для команд без параметров. Если оно присутствует, его значение должно быть JSON-объектом.

| `command` | Обязательные параметры | Необязательные параметры |
| --- | --- | --- |
| `spawn` | — | `displayName`; по умолчанию используется `actorId`. `displayNameColor`: цвет надписи в формате `#RRGGBB`, по умолчанию `#FFFFFF`. |
| `move_to_point` | `targetId` | `movementMode`: `walk` или `run`, по умолчанию `walk`. |
| `move_to_actor` | `targetId` | `movementMode`: `walk` или `run`, по умолчанию `walk`. |
| `approach_object` | `targetId` | `interactionPointId`, по умолчанию `default`; `movementMode`, по умолчанию `walk`. |
| `play_action` | `actionId` | `targetType`: `none`, `participant` или `arena_object`, по умолчанию `none`; `targetId` обязателен для цели типа `participant` или `arena_object`. |
| `stop` | — | — |
| `leave` | — | — |

Пример:

```json
{
  "version": 1,
  "requestId": "42",
  "actorId": "test:participant-1",
  "command": "move_to_point",
  "parameters": {
    "targetId": "center",
    "movementMode": "run"
  }
}
```

## Исходящее сообщение

Каждое изменение состояния команды можно представить одинаковым сообщением:

```json
{
  "version": 1,
  "requestId": "42",
  "status": "completed",
  "errorCode": null,
  "message": ""
}
```

Допустимые статусы:

```text
received
accepted
started
completed
rejected
failed
cancelled
```

При отсутствии ошибки `errorCode` имеет значение `null`. При ошибке поле содержит стабильный идентификатор в `snake_case`, например `invalid_request`, `unsupported_version`, `duplicate_request_id` или `unknown_target`. Поле `message` предназначено для диагностического текста и не должно использоваться программой как код результата.
