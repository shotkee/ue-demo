import assert from "node:assert/strict";
import { once } from "node:events";
import test from "node:test";

import WebSocket from "ws";

import { ArenaBridgeServer } from "../src/arenaBridgeServer.js";
import type { BridgeConfig } from "../src/config.js";
import { ARENA_PROTOCOL_VERSION, type ArenaCommand } from "../src/protocol.js";

function createConfig(overrides: Partial<BridgeConfig> = {}): BridgeConfig {
  return {
    host: "127.0.0.1",
    port: 0,
    allowPrivateNetworkConnections: false,
    queueLimit: 3,
    queueTtlMs: 5_000,
    statusTimeoutMs: 2_000,
    mode: "stdin",
    ...overrides,
  };
}

function createCommand(requestId: string): ArenaCommand {
  return {
    version: ARENA_PROTOCOL_VERSION,
    requestId,
    actorId: "test:alice",
    command: "spawn",
    parameters: { displayName: "Alice" },
  };
}

async function openClient(server: ArenaBridgeServer): Promise<WebSocket> {
  const client = new WebSocket(`ws://127.0.0.1:${server.port}`);
  await once(client, "open");
  return client;
}

test("queues a command until Unreal Engine connects", async () => {
  const server = new ArenaBridgeServer(createConfig());
  await server.start();

  try {
    const command = createCommand("queued-command");
    assert.equal(server.send(command), "queued");
    assert.equal(server.queuedCommandCount, 1);

    const client = new WebSocket(`ws://127.0.0.1:${server.port}`);
    const messagePromise = once(client, "message") as Promise<[WebSocket.RawData, boolean]>;
    await once(client, "open");
    const [message] = await messagePromise;
    assert.deepEqual(JSON.parse(message.toString()), command);
    assert.equal(server.queuedCommandCount, 0);
  } finally {
    await server.stop();
  }
});

test("forwards commands and emits statuses", async () => {
  const server = new ArenaBridgeServer(createConfig());
  await server.start();

  try {
    const client = await openClient(server);
    const command = createCommand("live-command");
    const commandMessage = once(client, "message") as Promise<[WebSocket.RawData, boolean]>;
    assert.equal(server.send(command), "sent");
    assert.deepEqual(JSON.parse((await commandMessage)[0].toString()), command);

    const statusPromise = new Promise((resolve) => {
      const removeListener = server.onStatus((status) => {
        removeListener();
        resolve(status);
      });
    });
    const status = {
      version: 1,
      requestId: command.requestId,
      status: "completed",
      errorCode: null,
      message: "",
    };
    client.send(JSON.stringify(status));
    assert.deepEqual(await statusPromise, status);
  } finally {
    await server.stop();
  }
});

test("flushes commands queued during an Unreal Engine reconnect", async () => {
  const server = new ArenaBridgeServer(createConfig());
  await server.start();

  try {
    const firstClient = await openClient(server);
    const disconnected = new Promise<void>((resolve) => {
      const removeListener = server.onConnectionChange((connected) => {
        if (!connected) {
          removeListener();
          resolve();
        }
      });
    });
    firstClient.close();
    await disconnected;

    const command = createCommand("reconnect-command");
    assert.equal(server.send(command), "queued");

    const secondClient = new WebSocket(`ws://127.0.0.1:${server.port}`);
    const messagePromise = once(secondClient, "message") as Promise<[WebSocket.RawData, boolean]>;
    await once(secondClient, "open");
    assert.deepEqual(JSON.parse((await messagePromise)[0].toString()), command);
    assert.equal(server.queuedCommandCount, 0);
  } finally {
    await server.stop();
  }
});

test("allows only one active Unreal Engine connection", async () => {
  const server = new ArenaBridgeServer(createConfig());
  await server.start();

  try {
    const firstClient = await openClient(server);
    const secondClient = await openClient(server);
    const [closeCode] = await once(secondClient, "close") as [number, Buffer];
    assert.equal(closeCode, 1013);
    assert.equal(firstClient.readyState, WebSocket.OPEN);
    assert.equal(server.connected, true);
  } finally {
    await server.stop();
  }
});

test("reports a queued command when its reconnect deadline expires", async () => {
  const server = new ArenaBridgeServer(createConfig({ queueTtlMs: 100 }));
  await server.start();

  try {
    const command = createCommand("expired-command");
    const expired = new Promise<ArenaCommand>((resolve) => {
      const removeListener = server.onCommandExpired((expiredCommand) => {
        removeListener();
        resolve(expiredCommand);
      });
    });
    assert.equal(server.send(command), "queued");
    await new Promise((resolve) => setTimeout(resolve, 120));
    assert.equal(server.queuedCommandCount, 0);
    assert.deepEqual(await expired, command);
  } finally {
    await server.stop();
  }
});
