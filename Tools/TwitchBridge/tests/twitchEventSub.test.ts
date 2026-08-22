import assert from "node:assert/strict";
import { once } from "node:events";
import test from "node:test";

import WebSocket, { WebSocketServer } from "ws";

import type { TwitchAuthorizationIdentity } from "../src/twitchAuth.js";
import { TwitchEventSubClient, type TwitchChatMessage } from "../src/twitchEventSub.js";

const CHAT_SUBSCRIPTION_TYPE = "channel.chat.message";

function authorization(): TwitchAuthorizationIdentity {
  return {
    accessToken: "access-token",
    authorizedUserId: "1001",
    authorizedUserLogin: "broadcaster",
    channelUserId: "1001",
    channelLogin: "broadcaster",
    channelDisplayName: "Broadcaster",
    scopes: ["user:read:chat"],
    expiresIn: 14_000,
  };
}

function envelope(
  messageType: string,
  messageId: string,
  payload: Record<string, unknown>,
  subscriptionType?: string,
): string {
  return JSON.stringify({
    metadata: {
      message_id: messageId,
      message_type: messageType,
      message_timestamp: "2026-08-22T10:00:00.000000000Z",
      ...(subscriptionType === undefined ? {} : {
        subscription_type: subscriptionType,
        subscription_version: "1",
      }),
    },
    payload,
  });
}

function welcome(sessionId: string): string {
  return envelope("session_welcome", `welcome-${sessionId}`, {
    session: {
      id: sessionId,
      status: "connected",
      connected_at: "2026-08-22T10:00:00.000000000Z",
      keepalive_timeout_seconds: 1_000,
      reconnect_url: null,
    },
  });
}

function reconnect(sessionId: string, reconnectUrl: string): string {
  return envelope("session_reconnect", `reconnect-${sessionId}`, {
    session: {
      id: sessionId,
      status: "reconnecting",
      connected_at: "2026-08-22T10:00:00.000000000Z",
      keepalive_timeout_seconds: null,
      reconnect_url: reconnectUrl,
    },
  });
}

function chatNotification(
  deliveryId: string,
  messageId: string,
  text: string,
  options: {
    chatterUserId?: string;
    chatterUserLogin?: string;
    chatterUserName?: string;
    badges?: Array<Record<string, unknown>>;
    color?: string;
  } = {},
): string {
  return envelope("notification", deliveryId, {
    subscription: {
      id: "subscription-1",
      status: "enabled",
      type: CHAT_SUBSCRIPTION_TYPE,
      version: "1",
    },
    event: {
      broadcaster_user_id: "1001",
      broadcaster_user_login: "broadcaster",
      broadcaster_user_name: "Broadcaster",
      chatter_user_id: options.chatterUserId ?? "2002",
      chatter_user_login: options.chatterUserLogin ?? "alice",
      chatter_user_name: options.chatterUserName ?? "Alice",
      color: options.color ?? "#1e90ff",
      badges: options.badges ?? [],
      message_id: messageId,
      message: { text },
    },
  }, CHAT_SUBSCRIPTION_TYPE);
}

function revocation(): string {
  return envelope("revocation", "revocation-1", {
    subscription: {
      id: "subscription-1",
      status: "authorization_revoked",
      type: CHAT_SUBSCRIPTION_TYPE,
      version: "1",
    },
  }, CHAT_SUBSCRIPTION_TYPE);
}

function subscriptionResponse(subscriptionId: string): Response {
  return new Response(JSON.stringify({
    data: [{ id: subscriptionId, status: "enabled" }],
    total: 1,
  }), {
    status: 202,
    headers: { "Content-Type": "application/json" },
  });
}

async function createServer(
  onConnection: (socket: WebSocket) => void,
): Promise<{ server: WebSocketServer; url: string }> {
  const server = new WebSocketServer({ host: "127.0.0.1", port: 0 });
  server.on("connection", onConnection);
  await once(server, "listening");
  const address = server.address();
  assert.ok(address !== null && typeof address === "object");
  return { server, url: `ws://127.0.0.1:${address.port}` };
}

async function closeServer(server: WebSocketServer): Promise<void> {
  for (const socket of server.clients) {
    socket.terminate();
  }
  await new Promise<void>((resolve, reject) => {
    server.close((error) => error === undefined ? resolve() : reject(error));
  });
}

async function waitFor(predicate: () => boolean, message: string): Promise<void> {
  const deadline = Date.now() + 2_000;
  while (!predicate()) {
    if (Date.now() >= deadline) {
      throw new Error(message);
    }
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
}

test("subscribes to chat, filters non-commands, deduplicates messages, and reports revocation", async () => {
  let serverSocket: WebSocket | undefined;
  const { server, url } = await createServer((socket) => {
    serverSocket = socket;
    socket.send(welcome("session-1"));
  });
  const subscriptionRequests: Array<Record<string, unknown>> = [];
  const fetchFn = (async (_input: string | URL | Request, init?: RequestInit): Promise<Response> => {
    subscriptionRequests.push(JSON.parse(String(init?.body)) as Record<string, unknown>);
    return subscriptionResponse("subscription-1");
  }) as typeof fetch;
  const client = new TwitchEventSubClient("public-client-id", async () => authorization(), {
    eventSubUrl: url,
    fetchFn,
    reconnectDelaysMs: [0],
  });
  const commands: TwitchChatMessage[] = [];
  const revocations: string[] = [];
  client.onCommand((message) => commands.push(message));
  client.onRevocation((event) => revocations.push(event.status));

  try {
    await client.start();
    assert.equal(subscriptionRequests.length, 1);
    assert.deepEqual(subscriptionRequests[0], {
      type: CHAT_SUBSCRIPTION_TYPE,
      version: "1",
      condition: {
        broadcaster_user_id: "1001",
        user_id: "1001",
      },
      transport: {
        method: "websocket",
        session_id: "session-1",
      },
    });

    assert.ok(serverSocket !== undefined);
    serverSocket.send(envelope("session_keepalive", "keepalive-1", {}));
    serverSocket.send(chatNotification("delivery-ordinary", "chat-ordinary", "hello"));
    serverSocket.send(chatNotification("delivery-command", "chat-command", "  !join", {
      badges: [{ set_id: "moderator", id: "1", info: "" }],
    }));
    await waitFor(() => commands.length === 1, "The Twitch command was not emitted.");
    assert.equal(commands[0]?.chatterUserId, "2002");
    assert.equal(commands[0]?.chatterUserLogin, "alice");
    assert.equal(commands[0]?.chatterUserName, "Alice");
    assert.equal(commands[0]?.color, "#1E90FF");
    assert.equal(commands[0]?.isBroadcaster, false);
    assert.equal(commands[0]?.isModerator, true);
    assert.equal(commands[0]?.text, "  !join");

    serverSocket.send(chatNotification("delivery-broadcaster", "chat-broadcaster", "!stop", {
      chatterUserId: "1001",
      chatterUserLogin: "broadcaster",
      chatterUserName: "Broadcaster",
    }));
    await waitFor(() => commands.length === 2, "The broadcaster command was not emitted.");
    assert.equal(commands[1]?.isBroadcaster, true);
    assert.equal(commands[1]?.isModerator, false);

    serverSocket.send(chatNotification("delivery-retry", "chat-command", "  !join"));
    serverSocket.send(revocation());
    await waitFor(() => revocations.length === 1, "The revocation event was not emitted.");
    await new Promise((resolve) => setTimeout(resolve, 25));
    assert.equal(commands.length, 2);
    assert.deepEqual(revocations, ["authorization_revoked"]);
  } finally {
    await client.stop();
    await closeServer(server);
  }
});

test("migrates a server-requested reconnect without creating a duplicate subscription", async () => {
  let originalSocket: WebSocket | undefined;
  let migratedSocket: WebSocket | undefined;
  const migrated = await createServer((socket) => {
    migratedSocket = socket;
    socket.send(welcome("session-migrated"));
  });
  const original = await createServer((socket) => {
    originalSocket = socket;
    socket.send(welcome("session-original"));
  });
  let subscriptionCount = 0;
  const fetchFn = (async (): Promise<Response> => {
    subscriptionCount += 1;
    return subscriptionResponse(`subscription-${subscriptionCount}`);
  }) as typeof fetch;
  const client = new TwitchEventSubClient("public-client-id", async () => authorization(), {
    eventSubUrl: original.url,
    fetchFn,
    reconnectDelaysMs: [0],
  });
  const commands: TwitchChatMessage[] = [];
  client.onCommand((message) => commands.push(message));

  try {
    await client.start();
    assert.ok(originalSocket !== undefined);
    originalSocket.send(reconnect("session-original", migrated.url));
    await waitFor(() => migratedSocket !== undefined, "The reconnect WebSocket was not opened.");
    await new Promise((resolve) => setTimeout(resolve, 25));
    assert.equal(subscriptionCount, 1);

    migratedSocket?.send(chatNotification("delivery-migrated", "chat-migrated", "!goto center"));
    await waitFor(() => commands.length === 1, "The migrated EventSub socket did not deliver chat.");
    assert.equal(commands[0]?.messageId, "chat-migrated");
  } finally {
    await client.stop();
    await closeServer(original.server);
    await closeServer(migrated.server);
  }
});

test("creates a new chat subscription after an ordinary connection loss", async () => {
  let connectionCount = 0;
  let firstSocket: WebSocket | undefined;
  const local = await createServer((socket) => {
    connectionCount += 1;
    if (connectionCount === 1) {
      firstSocket = socket;
    }
    socket.send(welcome(`session-${connectionCount}`));
  });
  let subscriptionCount = 0;
  const fetchFn = (async (): Promise<Response> => {
    subscriptionCount += 1;
    return subscriptionResponse(`subscription-${subscriptionCount}`);
  }) as typeof fetch;
  const client = new TwitchEventSubClient("public-client-id", async () => authorization(), {
    eventSubUrl: local.url,
    fetchFn,
    reconnectDelaysMs: [0],
  });

  try {
    await client.start();
    assert.equal(subscriptionCount, 1);
    assert.ok(firstSocket !== undefined);
    firstSocket.close(4000, "ordinary test disconnect");
    await waitFor(
      () => connectionCount === 2 && subscriptionCount === 2,
      "The ordinary reconnect did not recreate the chat subscription.",
    );
  } finally {
    await client.stop();
    await closeServer(local.server);
  }
});
