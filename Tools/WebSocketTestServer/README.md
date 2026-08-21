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

## Settings

The matching Unreal setting is under `Edit > Project Settings > Game > Arena WebSocket`. The default server URL is `ws://127.0.0.1:8080`.

Use a different port with:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\WebSocketTestServer\RunArenaWebSocketTestServer.ps1 -Port 8090
```

If the port changes, update `Server URL` in Unreal Editor to match.
