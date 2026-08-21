# Arena WebSocket test server

This dependency-free PowerShell server verifies the local UE-9 WebSocket connection without Twitch or an external package manager. It listens only on `127.0.0.1`.

## Run the smoke test

1. Open PowerShell in the project root.
2. Start the server:

   ```powershell
   powershell -ExecutionPolicy Bypass -File .\Tools\WebSocketTestServer\RunArenaWebSocketTestServer.ps1
   ```

3. Wait until the server reports that it is listening on `ws://127.0.0.1:8080`.
4. Open the `Arena` map in Unreal Editor and start Play In Editor.

The script waits for Unreal Engine to connect, then creates a uniquely named test mannequin and sends it to the `center` arena point. It prints every structured command status and succeeds only after both commands reach `completed`.

The debug panel should show `WS: Connected`. After the smoke test, the script keeps the connection open until you press `Ctrl+C`.

## Run the capacity test

The arena manager is configured for 20 simultaneous participants. Start this test with a fresh Play In Editor session:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\WebSocketTestServer\RunArenaWebSocketTestServer.ps1 -Scenario Capacity -ParticipantCount 20
```

After the server starts listening, start Play In Editor. The test must successfully spawn all 20 mannequins and then confirm that one additional mannequin is refused. A successful run ends with:

```text
Capacity test completed successfully: 20 participants spawned and the extra participant was refused.
```

## Run the queue overflow test

Start this test with a fresh Play In Editor session:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\WebSocketTestServer\RunArenaWebSocketTestServer.ps1 -Scenario QueueOverflow
```

The test spawns one mannequin, submits one active movement command and eight pending movement commands, and verifies that the next command is rejected with `queue_full`. It then sends `stop` and verifies that the queue is cleared. A successful run ends with:

```text
Queue overflow test completed successfully: 1 active and 8 pending commands were accepted, the extra command returned queue_full, and stop cleared the queue.
```

## Run the target removal test

Start this test with a fresh Play In Editor session:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\WebSocketTestServer\RunArenaWebSocketTestServer.ps1 -Scenario TargetRemoved
```

The test places a chaser at `north` and its target at `south`, starts `move_to_actor`, waits two seconds so movement is visible, queues another movement command, and removes the target. It verifies that the chaser stops immediately, its active command terminates with `failed / unknown_target`, and its pending queue is cancelled. A successful run ends with:

```text
Target removal test completed successfully: the chaser stopped immediately, failed with unknown_target, and its pending queue was cancelled.
```

## Run the unreachable NavMesh test

This scenario requires the map fixture `unreachable_test` to reference a target point at `(10000, 10000, 100)`, outside the arena NavMesh. Start the test with a fresh Play In Editor session:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\WebSocketTestServer\RunArenaWebSocketTestServer.ps1 -Scenario UnreachableNavMesh
```

The test spawns one mannequin and asks it to move to the known off-mesh point. The command must terminate with `failed / unreachable_target`, and the mannequin must remain inside the arena. A successful run ends with:

```text
Unreachable NavMesh test completed successfully: the known off-mesh point failed with unreachable_target and the mannequin remained inside the arena.
```

## Run the invalid ID test

Start this test with a fresh Play In Editor session:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\WebSocketTestServer\RunArenaWebSocketTestServer.ps1 -Scenario InvalidIds
```

The test submits commands with an unknown participant, an unknown arena object, and an unknown action. It requires the exact errors `unknown_participant`, `unknown_target`, and `unknown_action`. Its temporary mannequin is removed at the end so rejected commands leave the arena unchanged. A successful run ends with:

```text
Invalid ID test completed successfully: unknown ActorId, ObjectId, and ActionId were rejected with the expected error codes without changing the arena.
```

## Run the duplicate RequestId test

Start this test with a fresh Play In Editor session:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\WebSocketTestServer\RunArenaWebSocketTestServer.ps1 -Scenario DuplicateRequestId
```

The test spawns a temporary mannequin, then sends a movement command using the same `requestId` as the completed spawn command. The repeated request must be rejected with `duplicate_request_id` and must not move the mannequin. The temporary mannequin is removed at the end. A successful run ends with:

```text
Duplicate RequestId test completed successfully: the repeated request was rejected with duplicate_request_id and was not executed.
```

## Run the reconnect test

Start this test with a fresh Play In Editor session:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\WebSocketTestServer\RunArenaWebSocketTestServer.ps1 -Scenario Reconnect
```

The server accepts the first connection, closes it intentionally, waits while the editor enters `WS: Reconnecting`, and then accepts the automatic reconnect. It submits spawn, movement, and leave commands over the new connection. A successful run ends with:

```text
WebSocket reconnect test completed successfully: Unreal Engine reconnected automatically and completed commands over the new connection.
```

## Run the network load test

Start this test with a fresh Play In Editor session and keep the Unreal Editor window active while observing the mannequin:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\WebSocketTestServer\RunArenaWebSocketTestServer.ps1 -Scenario NetworkLoad
```

The test positions one mannequin at the south point, starts a walk to the north point, and submits 100 additional WebSocket commands while it is moving. The additional commands intentionally use unknown participants and must be rejected without interrupting or visibly stalling the active movement. A successful run ends with:

```text
Network load test completed successfully: the mannequin completed its movement while 100 WebSocket commands were processed.
```

## Run the error log test

Start this test with a fresh Play In Editor session:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\WebSocketTestServer\RunArenaWebSocketTestServer.ps1 -Scenario ErrorLog
```

The test submits a command with an unsupported protocol version. It must be rejected without changing the arena, and Unreal Engine must write a structured `LogArenaWebSocket` warning containing the request ID, error, explanation, and payload size. A successful server run ends with:

```text
Error log test completed successfully: the protocol error was rejected and returned unsupported_version.
```

## Settings

The matching Unreal setting is under `Edit > Project Settings > Game > Arena WebSocket`. The default server URL is `ws://127.0.0.1:8080`.

Use a different port with:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\WebSocketTestServer\RunArenaWebSocketTestServer.ps1 -Port 8090
```

If the port changes, update `Server URL` in Unreal Editor to match.
