import { EventEmitter } from "node:events";
import type { AddressInfo } from "node:net";

import WebSocket, { WebSocketServer, type RawData } from "ws";

import type { BridgeConfig } from "./config.js";
import { errorFields, log } from "./logger.js";
import { parseArenaStatus, type ArenaCommand, type ArenaCommandStatus } from "./protocol.js";

export type SendDisposition = "sent" | "queued";

interface QueuedCommand {
  command: ArenaCommand;
  expiresAt: number;
}

interface BridgeEvents {
  status: [ArenaCommandStatus];
  connection: [boolean];
}

export class ArenaBridgeServer {
  private readonly events = new EventEmitter<BridgeEvents>();
  private readonly pendingCommands: QueuedCommand[] = [];
  private webSocketServer: WebSocketServer | undefined;
  private unrealSocket: WebSocket | undefined;
  private queueCleanupTimer: NodeJS.Timeout | undefined;

  public constructor(private readonly config: BridgeConfig) {}

  public async start(): Promise<void> {
    if (this.webSocketServer !== undefined) {
      throw new Error("Arena bridge server is already running.");
    }

    const webSocketServer = new WebSocketServer({
      host: this.config.host,
      port: this.config.port,
      maxPayload: 16_384,
      perMessageDeflate: false,
    });
    this.webSocketServer = webSocketServer;

    webSocketServer.on("connection", (socket) => this.handleConnection(socket));
    webSocketServer.on("error", (error) => {
      log("error", "arena_websocket_server_error", errorFields(error));
    });

    await new Promise<void>((resolve, reject) => {
      const onListening = (): void => {
        webSocketServer.off("error", onInitialError);
        resolve();
      };
      const onInitialError = (error: Error): void => {
        webSocketServer.off("listening", onListening);
        reject(error);
      };

      webSocketServer.once("listening", onListening);
      webSocketServer.once("error", onInitialError);
    });

    this.queueCleanupTimer = setInterval(() => this.removeExpiredCommands(), 1_000);
    this.queueCleanupTimer.unref();

    log("info", "arena_bridge_listening", {
      url: `ws://${this.config.host}:${this.port}`,
      mode: this.config.mode,
      allowPrivateNetworkConnections: this.config.allowPrivateNetworkConnections,
      queueLimit: this.config.queueLimit,
      queueTtlMs: this.config.queueTtlMs,
    });
  }

  public async stop(): Promise<void> {
    if (this.queueCleanupTimer !== undefined) {
      clearInterval(this.queueCleanupTimer);
      this.queueCleanupTimer = undefined;
    }

    const webSocketServer = this.webSocketServer;
    this.webSocketServer = undefined;

    const unrealSocket = this.unrealSocket;
    this.unrealSocket = undefined;
    if (unrealSocket !== undefined) {
      unrealSocket.terminate();
    }

    if (webSocketServer !== undefined) {
      for (const client of webSocketServer.clients) {
        client.terminate();
      }

      await new Promise<void>((resolve) => webSocketServer.close(() => resolve()));
    }

    this.pendingCommands.length = 0;
    log("info", "arena_bridge_stopped");
  }

  public get port(): number {
    const address = this.webSocketServer?.address();
    if (address === undefined || address === null || typeof address === "string") {
      return this.config.port;
    }

    return (address as AddressInfo).port;
  }

  public get connected(): boolean {
    return this.unrealSocket?.readyState === WebSocket.OPEN;
  }

  public get queuedCommandCount(): number {
    this.removeExpiredCommands();
    return this.pendingCommands.length;
  }

  public onStatus(listener: (status: ArenaCommandStatus) => void): () => void {
    this.events.on("status", listener);
    return () => this.events.off("status", listener);
  }

  public onConnectionChange(listener: (connected: boolean) => void): () => void {
    this.events.on("connection", listener);
    return () => this.events.off("connection", listener);
  }

  public send(command: ArenaCommand): SendDisposition {
    const text = JSON.stringify(command);
    if (Buffer.byteLength(text, "utf8") > 16_384) {
      throw new Error("Arena command exceeds the 16 KiB protocol limit.");
    }

    if (this.connected) {
      this.unrealSocket?.send(text);
      log("info", "arena_command_sent", {
        requestId: command.requestId,
        actorId: command.actorId,
        command: command.command,
      });
      return "sent";
    }

    this.removeExpiredCommands();
    if (this.pendingCommands.length >= this.config.queueLimit) {
      throw new Error(`Arena reconnect queue is full (${this.config.queueLimit} commands).`);
    }

    this.pendingCommands.push({
      command,
      expiresAt: Date.now() + this.config.queueTtlMs,
    });
    log("info", "arena_command_queued", {
      requestId: command.requestId,
      actorId: command.actorId,
      command: command.command,
      queueSize: this.pendingCommands.length,
    });
    return "queued";
  }

  public waitForConnection(timeoutMs: number): Promise<void> {
    if (this.connected) {
      return Promise.resolve();
    }

    return new Promise<void>((resolve, reject) => {
      let timeout: NodeJS.Timeout | undefined;
      const removeListener = this.onConnectionChange((connected) => {
        if (connected) {
          if (timeout !== undefined) {
            clearTimeout(timeout);
          }
          removeListener();
          resolve();
        }
      });

      if (timeoutMs > 0) {
        timeout = setTimeout(() => {
          removeListener();
          reject(new Error(`Unreal Engine did not connect within ${timeoutMs} ms.`));
        }, timeoutMs);
      }
    });
  }

  private handleConnection(socket: WebSocket): void {
    if (this.connected) {
      log("warn", "arena_connection_rejected", { reason: "active_connection_exists" });
      socket.close(1013, "Only one Unreal Engine connection is supported.");
      return;
    }

    this.unrealSocket = socket;
    log("info", "arena_connected");
    this.events.emit("connection", true);

    socket.on("message", (data, isBinary) => this.handleMessage(data, isBinary));
    socket.on("error", (error) => {
      log("warn", "arena_socket_error", errorFields(error));
    });
    socket.on("close", (code, reason) => {
      if (this.unrealSocket !== socket) {
        return;
      }

      this.unrealSocket = undefined;
      log("warn", "arena_disconnected", {
        code,
        reason: reason.toString(),
        queuedCommands: this.pendingCommands.length,
      });
      this.events.emit("connection", false);
    });

    this.flushPendingCommands();
  }

  private handleMessage(data: RawData, isBinary: boolean): void {
    if (isBinary) {
      log("warn", "arena_status_ignored", { reason: "binary_message" });
      return;
    }

    try {
      const status = parseArenaStatus(data.toString());
      log("info", "arena_command_status", { ...status });
      this.events.emit("status", status);
    } catch (error) {
      log("warn", "arena_status_ignored", {
        reason: "invalid_status",
        ...errorFields(error),
      });
    }
  }

  private flushPendingCommands(): void {
    this.removeExpiredCommands();
    while (this.connected && this.pendingCommands.length > 0) {
      const queuedCommand = this.pendingCommands.shift();
      if (queuedCommand === undefined) {
        break;
      }

      this.send(queuedCommand.command);
    }
  }

  private removeExpiredCommands(): void {
    const now = Date.now();
    while (this.pendingCommands[0]?.expiresAt !== undefined && this.pendingCommands[0].expiresAt <= now) {
      const expiredCommand = this.pendingCommands.shift();
      if (expiredCommand !== undefined) {
        log("warn", "arena_command_expired", {
          requestId: expiredCommand.command.requestId,
          actorId: expiredCommand.command.actorId,
          command: expiredCommand.command.command,
        });
      }
    }
  }
}
