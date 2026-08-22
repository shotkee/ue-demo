import { EventEmitter } from "node:events";

import WebSocket, { type RawData } from "ws";

import { errorFields, log } from "./logger.js";
import type { TwitchAuthorizationIdentity } from "./twitchAuth.js";

const DEFAULT_EVENTSUB_URL = "wss://eventsub.wss.twitch.tv/ws";
const CREATE_SUBSCRIPTION_URL = "https://api.twitch.tv/helix/eventsub/subscriptions";
const CHAT_SUBSCRIPTION_TYPE = "channel.chat.message";
const CHAT_SUBSCRIPTION_VERSION = "1";
const REQUEST_TIMEOUT_MS = 15_000;
const MESSAGE_ID_TTL_MS = 10 * 60 * 1_000;
const MAX_RECENT_MESSAGE_IDS = 5_000;
const DEFAULT_RECONNECT_DELAYS_MS = [1_000, 2_000, 5_000, 10_000, 30_000] as const;

export interface TwitchChatMessage {
  deliveryMessageId: string;
  messageId: string;
  broadcasterUserId: string;
  broadcasterUserLogin: string;
  broadcasterUserName: string;
  chatterUserId: string;
  chatterUserLogin: string;
  chatterUserName: string;
  isBroadcaster: boolean;
  isModerator: boolean;
  text: string;
  receivedAt: string;
}

export type TwitchEventSubConnectionState = "connecting" | "connected" | "reconnecting" | "disconnected";

export interface TwitchSubscriptionRevocation {
  subscriptionId: string;
  type: string;
  version: string;
  status: string;
}

interface EventSubMetadata {
  messageId: string;
  messageType: string;
  messageTimestamp: string;
  subscriptionType: string | undefined;
}

interface EventSubEnvelope {
  metadata: EventSubMetadata;
  payload: Record<string, unknown>;
}

interface EventSubSession {
  id: string;
  keepaliveTimeoutSeconds: number | undefined;
  reconnectUrl: string | undefined;
}

interface TwitchEventSubEvents {
  command: [TwitchChatMessage];
  connection: [TwitchEventSubConnectionState];
  revocation: [TwitchSubscriptionRevocation];
}

interface TwitchEventSubDependencies {
  fetchFn?: typeof fetch;
  socketFactory?: (url: string) => WebSocket;
  eventSubUrl?: string;
  reconnectDelaysMs?: readonly number[];
  now?: () => number;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function requiredRecord(value: unknown, fieldName: string): Record<string, unknown> {
  if (!isRecord(value)) {
    throw new Error(`EventSub message is missing object '${fieldName}'.`);
  }
  return value;
}

function requiredString(value: unknown, fieldName: string): string {
  if (typeof value !== "string" || value.trim() === "") {
    throw new Error(`EventSub message is missing string '${fieldName}'.`);
  }
  return value;
}

function optionalString(value: unknown, fieldName: string): string | undefined {
  if (value === null || value === undefined) {
    return undefined;
  }
  if (typeof value !== "string" || value.trim() === "") {
    throw new Error(`EventSub message contains invalid '${fieldName}'.`);
  }
  return value;
}

function requiredNonNegativeInteger(value: unknown, fieldName: string): number {
  if (!Number.isInteger(value) || (value as number) < 0) {
    throw new Error(`EventSub message contains invalid '${fieldName}'.`);
  }
  return value as number;
}

function optionalNonNegativeInteger(value: unknown, fieldName: string): number | undefined {
  if (value === null || value === undefined) {
    return undefined;
  }
  return requiredNonNegativeInteger(value, fieldName);
}

function parseEnvelope(text: string): EventSubEnvelope {
  const value: unknown = JSON.parse(text);
  const root = requiredRecord(value, "root");
  const metadata = requiredRecord(root.metadata, "metadata");
  return {
    metadata: {
      messageId: requiredString(metadata.message_id, "metadata.message_id"),
      messageType: requiredString(metadata.message_type, "metadata.message_type"),
      messageTimestamp: requiredString(metadata.message_timestamp, "metadata.message_timestamp"),
      subscriptionType: optionalString(metadata.subscription_type, "metadata.subscription_type"),
    },
    payload: requiredRecord(root.payload, "payload"),
  };
}

function parseSession(payload: Record<string, unknown>): EventSubSession {
  const session = requiredRecord(payload.session, "payload.session");
  return {
    id: requiredString(session.id, "payload.session.id"),
    keepaliveTimeoutSeconds: optionalNonNegativeInteger(
      session.keepalive_timeout_seconds,
      "payload.session.keepalive_timeout_seconds",
    ),
    reconnectUrl: optionalString(session.reconnect_url, "payload.session.reconnect_url"),
  };
}

function parseBadgeSetIds(value: unknown): Set<string> {
  if (value === null || value === undefined) {
    return new Set();
  }
  if (!Array.isArray(value)) {
    throw new Error("EventSub message contains invalid 'payload.event.badges'.");
  }

  const badgeSetIds = new Set<string>();
  value.forEach((badge, index) => {
    const badgeRecord = requiredRecord(badge, `payload.event.badges[${index}]`);
    badgeSetIds.add(requiredString(
      badgeRecord.set_id,
      `payload.event.badges[${index}].set_id`,
    ).toLowerCase());
  });
  return badgeSetIds;
}

function parseChatMessage(envelope: EventSubEnvelope): TwitchChatMessage {
  const event = requiredRecord(envelope.payload.event, "payload.event");
  const message = requiredRecord(event.message, "payload.event.message");
  const broadcasterUserId = requiredString(
    event.broadcaster_user_id,
    "payload.event.broadcaster_user_id",
  );
  const chatterUserId = requiredString(event.chatter_user_id, "payload.event.chatter_user_id");
  const badgeSetIds = parseBadgeSetIds(event.badges);
  return {
    deliveryMessageId: envelope.metadata.messageId,
    messageId: requiredString(event.message_id, "payload.event.message_id"),
    broadcasterUserId,
    broadcasterUserLogin: requiredString(
      event.broadcaster_user_login,
      "payload.event.broadcaster_user_login",
    ),
    broadcasterUserName: requiredString(event.broadcaster_user_name, "payload.event.broadcaster_user_name"),
    chatterUserId,
    chatterUserLogin: requiredString(event.chatter_user_login, "payload.event.chatter_user_login"),
    chatterUserName: requiredString(event.chatter_user_name, "payload.event.chatter_user_name"),
    isBroadcaster: chatterUserId === broadcasterUserId || badgeSetIds.has("broadcaster"),
    isModerator: badgeSetIds.has("moderator"),
    text: requiredString(message.text, "payload.event.message.text"),
    receivedAt: envelope.metadata.messageTimestamp,
  };
}

function parseRevocation(payload: Record<string, unknown>): TwitchSubscriptionRevocation {
  const subscription = requiredRecord(payload.subscription, "payload.subscription");
  return {
    subscriptionId: requiredString(subscription.id, "payload.subscription.id"),
    type: requiredString(subscription.type, "payload.subscription.type"),
    version: requiredString(subscription.version, "payload.subscription.version"),
    status: requiredString(subscription.status, "payload.subscription.status"),
  };
}

async function responseBody(response: Response): Promise<unknown> {
  const text = await response.text();
  if (text.trim() === "") {
    return undefined;
  }
  try {
    return JSON.parse(text) as unknown;
  } catch {
    throw new Error(`Twitch returned non-JSON data with HTTP ${response.status}.`);
  }
}

function responseError(response: Response, body: unknown): Error {
  const message = isRecord(body) && typeof body.message === "string"
    ? body.message
    : `HTTP ${response.status}`;
  return new Error(`Could not create Twitch chat subscription: ${message}`);
}

export class TwitchEventSubClient {
  private readonly events = new EventEmitter<TwitchEventSubEvents>();
  private readonly fetchFn: typeof fetch;
  private readonly socketFactory: (url: string) => WebSocket;
  private readonly eventSubUrl: string;
  private readonly reconnectDelaysMs: readonly number[];
  private readonly now: () => number;
  private readonly recentMessageIds = new Map<string, number>();
  private activeSocket: WebSocket | undefined;
  private pendingReconnectSocket: WebSocket | undefined;
  private pendingReconnectUrl: string | undefined;
  private keepaliveTimer: NodeJS.Timeout | undefined;
  private reconnectTimer: NodeJS.Timeout | undefined;
  private reconnectAttempt = 0;
  private running = false;
  private readyResolve: (() => void) | undefined;
  private readyReject: ((error: Error) => void) | undefined;

  public constructor(
    private readonly clientId: string,
    private readonly authorizationProvider: () => Promise<TwitchAuthorizationIdentity>,
    dependencies: TwitchEventSubDependencies = {},
  ) {
    this.fetchFn = dependencies.fetchFn ?? fetch;
    this.socketFactory = dependencies.socketFactory ?? ((url) => new WebSocket(url));
    this.eventSubUrl = dependencies.eventSubUrl ?? DEFAULT_EVENTSUB_URL;
    this.reconnectDelaysMs = dependencies.reconnectDelaysMs ?? DEFAULT_RECONNECT_DELAYS_MS;
    this.now = dependencies.now ?? Date.now;
  }

  public async start(): Promise<void> {
    if (this.running) {
      throw new Error("Twitch EventSub client is already running.");
    }

    this.running = true;
    this.emitConnection("connecting");
    const ready = new Promise<void>((resolve, reject) => {
      this.readyResolve = resolve;
      this.readyReject = reject;
    });
    this.openSocket(this.eventSubUrl, false);
    return ready;
  }

  public async stop(): Promise<void> {
    if (!this.running && this.activeSocket === undefined && this.pendingReconnectSocket === undefined) {
      return;
    }

    this.running = false;
    this.clearTimers();
    const sockets = new Set([this.activeSocket, this.pendingReconnectSocket]);
    this.activeSocket = undefined;
    this.pendingReconnectSocket = undefined;
    this.pendingReconnectUrl = undefined;
    for (const socket of sockets) {
      if (socket !== undefined) {
        socket.close(1000, "Twitch bridge stopped.");
      }
    }
    this.rejectReady(new Error("Twitch EventSub client stopped before it became ready."));
    this.emitConnection("disconnected");
    log("info", "twitch_eventsub_stopped");
  }

  public onCommand(listener: (message: TwitchChatMessage) => void): () => void {
    this.events.on("command", listener);
    return () => this.events.off("command", listener);
  }

  public onConnectionChange(listener: (state: TwitchEventSubConnectionState) => void): () => void {
    this.events.on("connection", listener);
    return () => this.events.off("connection", listener);
  }

  public onRevocation(listener: (revocation: TwitchSubscriptionRevocation) => void): () => void {
    this.events.on("revocation", listener);
    return () => this.events.off("revocation", listener);
  }

  private openSocket(url: string, isServerReconnect: boolean): void {
    if (!this.running) {
      return;
    }

    let socket: WebSocket;
    try {
      socket = this.socketFactory(url);
    } catch (error) {
      log("warn", "twitch_eventsub_connect_failed", { url, ...errorFields(error) });
      this.scheduleReconnect(url, isServerReconnect);
      return;
    }

    if (isServerReconnect) {
      this.pendingReconnectSocket = socket;
      this.pendingReconnectUrl = url;
    } else {
      this.activeSocket = socket;
    }

    socket.on("open", () => {
      log("info", "twitch_eventsub_socket_open", { reconnect: isServerReconnect });
    });
    socket.on("message", (data, isBinary) => {
      void this.handleMessage(socket, isServerReconnect, data, isBinary).catch((error: unknown) => {
        const normalizedError = error instanceof Error ? error : new Error(String(error));
        log("warn", "twitch_eventsub_message_rejected", errorFields(normalizedError));
        if (this.readyReject !== undefined) {
          this.rejectReady(normalizedError);
        }
        socket.close(4000, "Invalid Twitch EventSub message.");
      });
    });
    socket.on("error", (error) => {
      log("warn", "twitch_eventsub_socket_error", errorFields(error));
    });
    socket.on("close", (code, reason) => {
      this.handleClose(socket, isServerReconnect, code, reason.toString());
    });
  }

  private async handleMessage(
    socket: WebSocket,
    isServerReconnect: boolean,
    data: RawData,
    isBinary: boolean,
  ): Promise<void> {
    if (isBinary) {
      throw new Error("Twitch EventSub sent an unexpected binary message.");
    }

    const envelope = parseEnvelope(data.toString());
    if (socket === this.activeSocket
      && (envelope.metadata.messageType === "session_keepalive"
        || envelope.metadata.messageType === "notification")) {
      this.resetKeepaliveTimer();
    }

    switch (envelope.metadata.messageType) {
      case "session_welcome":
        await this.handleWelcome(socket, isServerReconnect, parseSession(envelope.payload));
        return;
      case "session_keepalive":
        return;
      case "session_reconnect":
        this.handleServerReconnect(socket, parseSession(envelope.payload));
        return;
      case "notification":
        this.handleNotification(envelope);
        return;
      case "revocation":
        this.handleRevocation(envelope.payload);
        return;
      default:
        log("warn", "twitch_eventsub_message_ignored", {
          messageId: envelope.metadata.messageId,
          messageType: envelope.metadata.messageType,
          reason: "unknown_message_type",
        });
    }
  }

  private async handleWelcome(
    socket: WebSocket,
    isServerReconnect: boolean,
    session: EventSubSession,
  ): Promise<void> {
    if (session.keepaliveTimeoutSeconds === undefined) {
      throw new Error("EventSub Welcome message does not contain a keepalive timeout.");
    }

    if (isServerReconnect) {
      if (socket !== this.pendingReconnectSocket) {
        return;
      }

      const oldSocket = this.activeSocket;
      this.activeSocket = socket;
      this.pendingReconnectSocket = undefined;
      this.pendingReconnectUrl = undefined;
      this.reconnectAttempt = 0;
      this.armKeepaliveTimer(session.keepaliveTimeoutSeconds);
      this.emitConnection("connected");
      log("info", "twitch_eventsub_reconnected", {
        sessionId: session.id,
        keepaliveTimeoutSeconds: session.keepaliveTimeoutSeconds,
      });
      if (oldSocket !== undefined && oldSocket !== socket) {
        oldSocket.close(1000, "Twitch EventSub session migrated.");
      }
      return;
    }

    if (socket !== this.activeSocket) {
      return;
    }

    this.armKeepaliveTimer(session.keepaliveTimeoutSeconds);
    try {
      await this.createChatSubscription(session.id);
    } catch (error) {
      const normalizedError = error instanceof Error ? error : new Error(String(error));
      log("error", "twitch_subscription_failed", errorFields(normalizedError));
      this.rejectReady(normalizedError);
      socket.close(4000, "Twitch subscription failed.");
      return;
    }

    this.reconnectAttempt = 0;
    this.emitConnection("connected");
    this.resolveReady();
    log("info", "twitch_eventsub_ready", {
      sessionId: session.id,
      keepaliveTimeoutSeconds: session.keepaliveTimeoutSeconds,
    });
  }

  private handleServerReconnect(socket: WebSocket, session: EventSubSession): void {
    if (socket !== this.activeSocket || session.reconnectUrl === undefined) {
      return;
    }
    if (this.pendingReconnectSocket !== undefined) {
      return;
    }

    log("info", "twitch_eventsub_reconnect_requested", { reconnectUrl: session.reconnectUrl });
    this.emitConnection("reconnecting");
    this.openSocket(session.reconnectUrl, true);
  }

  private handleNotification(envelope: EventSubEnvelope): void {
    if (envelope.metadata.subscriptionType !== CHAT_SUBSCRIPTION_TYPE) {
      log("debug", "twitch_eventsub_notification_ignored", {
        messageId: envelope.metadata.messageId,
        subscriptionType: envelope.metadata.subscriptionType,
      });
      return;
    }

    const chatMessage = parseChatMessage(envelope);
    if (this.isDuplicate(chatMessage.messageId)) {
      log("debug", "twitch_chat_message_duplicate", {
        messageId: chatMessage.messageId,
        deliveryMessageId: chatMessage.deliveryMessageId,
      });
      return;
    }

    if (!chatMessage.text.trimStart().startsWith("!")) {
      return;
    }

    log("info", "twitch_chat_command_received", {
      messageId: chatMessage.messageId,
      chatterUserId: chatMessage.chatterUserId,
      chatterUserLogin: chatMessage.chatterUserLogin,
      chatterUserName: chatMessage.chatterUserName,
      text: chatMessage.text,
    });
    this.events.emit("command", chatMessage);
  }

  private handleRevocation(payload: Record<string, unknown>): void {
    const revocation = parseRevocation(payload);
    log("error", "twitch_subscription_revoked", {
      ...revocation,
      message: "Twitch revoked the chat subscription. Check authorization and run 'npm run auth' if needed.",
    });
    this.events.emit("revocation", revocation);
  }

  private async createChatSubscription(sessionId: string): Promise<void> {
    const authorization = await this.authorizationProvider();
    const response = await this.fetchFn(CREATE_SUBSCRIPTION_URL, {
      method: "POST",
      headers: {
        Authorization: `Bearer ${authorization.accessToken}`,
        "Client-Id": this.clientId,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        type: CHAT_SUBSCRIPTION_TYPE,
        version: CHAT_SUBSCRIPTION_VERSION,
        condition: {
          broadcaster_user_id: authorization.channelUserId,
          user_id: authorization.authorizedUserId,
        },
        transport: {
          method: "websocket",
          session_id: sessionId,
        },
      }),
      signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS),
    });
    const body = await responseBody(response);
    if (response.status !== 202) {
      throw responseError(response, body);
    }

    const root = requiredRecord(body, "subscription response");
    if (!Array.isArray(root.data) || !isRecord(root.data[0])) {
      throw new Error("Twitch subscription response does not contain a subscription.");
    }
    const subscription = root.data[0];
    log("info", "twitch_chat_subscription_created", {
      subscriptionId: requiredString(subscription.id, "data[0].id"),
      status: requiredString(subscription.status, "data[0].status"),
      broadcasterUserId: authorization.channelUserId,
      authorizedUserId: authorization.authorizedUserId,
    });
  }

  private handleClose(socket: WebSocket, isServerReconnect: boolean, code: number, reason: string): void {
    log("warn", "twitch_eventsub_socket_closed", { code, reason, reconnect: isServerReconnect });
    if (!this.running) {
      return;
    }

    if (socket === this.pendingReconnectSocket) {
      this.pendingReconnectSocket = undefined;
      const reconnectUrl = this.pendingReconnectUrl;
      this.pendingReconnectUrl = undefined;
      if (reconnectUrl !== undefined && this.activeSocket?.readyState === WebSocket.OPEN) {
        this.scheduleReconnect(reconnectUrl, true);
      } else {
        this.scheduleReconnect(this.eventSubUrl, false);
      }
      return;
    }

    if (socket !== this.activeSocket) {
      return;
    }

    this.activeSocket = undefined;
    this.clearKeepaliveTimer();
    if (this.pendingReconnectSocket === undefined) {
      this.scheduleReconnect(this.eventSubUrl, false);
    }
  }

  private scheduleReconnect(url: string, isServerReconnect: boolean): void {
    if (!this.running || this.reconnectTimer !== undefined) {
      return;
    }

    const lastDelay = this.reconnectDelaysMs[this.reconnectDelaysMs.length - 1] ?? 30_000;
    const delayMilliseconds = this.reconnectDelaysMs[
      Math.min(this.reconnectAttempt, Math.max(0, this.reconnectDelaysMs.length - 1))
    ] ?? lastDelay;
    this.reconnectAttempt += 1;
    this.emitConnection("reconnecting");
    log("warn", "twitch_eventsub_reconnect_scheduled", {
      delayMilliseconds,
      attempt: this.reconnectAttempt,
      serverRequested: isServerReconnect,
    });
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = undefined;
      this.openSocket(url, isServerReconnect);
    }, delayMilliseconds);
    this.reconnectTimer.unref();
  }

  private armKeepaliveTimer(timeoutSeconds: number): void {
    this.clearKeepaliveTimer();
    this.keepaliveTimer = setTimeout(() => {
      log("warn", "twitch_eventsub_keepalive_timeout", { timeoutSeconds });
      this.activeSocket?.terminate();
    }, timeoutSeconds * 1_000 + 1_000);
    this.keepaliveTimer.unref();
  }

  private resetKeepaliveTimer(): void {
    if (this.keepaliveTimer === undefined) {
      return;
    }
    this.keepaliveTimer.refresh();
  }

  private clearKeepaliveTimer(): void {
    if (this.keepaliveTimer !== undefined) {
      clearTimeout(this.keepaliveTimer);
      this.keepaliveTimer = undefined;
    }
  }

  private clearTimers(): void {
    this.clearKeepaliveTimer();
    if (this.reconnectTimer !== undefined) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = undefined;
    }
  }

  private isDuplicate(messageId: string): boolean {
    const now = this.now();
    for (const [knownMessageId, seenAt] of this.recentMessageIds) {
      if (now - seenAt <= MESSAGE_ID_TTL_MS) {
        break;
      }
      this.recentMessageIds.delete(knownMessageId);
    }

    if (this.recentMessageIds.has(messageId)) {
      return true;
    }
    this.recentMessageIds.set(messageId, now);
    while (this.recentMessageIds.size > MAX_RECENT_MESSAGE_IDS) {
      const oldestMessageId = this.recentMessageIds.keys().next().value as string | undefined;
      if (oldestMessageId === undefined) {
        break;
      }
      this.recentMessageIds.delete(oldestMessageId);
    }
    return false;
  }

  private emitConnection(state: TwitchEventSubConnectionState): void {
    this.events.emit("connection", state);
  }

  private resolveReady(): void {
    this.readyResolve?.();
    this.readyResolve = undefined;
    this.readyReject = undefined;
  }

  private rejectReady(error: Error): void {
    this.readyReject?.(error);
    this.readyResolve = undefined;
    this.readyReject = undefined;
  }
}
