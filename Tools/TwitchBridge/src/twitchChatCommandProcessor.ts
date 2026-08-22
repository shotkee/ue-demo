import type { SendDisposition } from "./arenaBridgeServer.js";
import { errorFields, log } from "./logger.js";
import {
  TERMINAL_STATUS_NAMES,
  type ArenaCommand,
  type ArenaCommandStatus,
} from "./protocol.js";
import {
  TwitchChatCommandError,
  TwitchChatCommandParser,
  type TwitchChatCommandName,
} from "./twitchChatCommands.js";
import type {
  TwitchChatMessage,
  TwitchEventSubConnectionState,
} from "./twitchEventSub.js";

export interface ArenaCommandSink {
  readonly connected: boolean;
  send(command: ArenaCommand): SendDisposition;
  onStatus(listener: (status: ArenaCommandStatus) => void): () => void;
  onConnectionChange(listener: (connected: boolean) => void): () => void;
  onCommandExpired(listener: (command: ArenaCommand) => void): () => void;
}

export interface TwitchChatCommandLimits {
  userCooldownMs: number;
  globalCommandsPerSecond: number;
  userQueueLimit: number;
  maxParticipants: number;
  maxMessageLength: number;
}

export const DEFAULT_TWITCH_CHAT_COMMAND_LIMITS: Readonly<TwitchChatCommandLimits> = Object.freeze({
  userCooldownMs: 750,
  globalCommandsPerSecond: 20,
  userQueueLimit: 4,
  maxParticipants: 20,
  maxMessageLength: 200,
});

export interface TwitchBridgeMetricsSnapshot {
  uptimeMs: number;
  messagesReceived: number;
  commandsSubmitted: number;
  commandsRejected: number;
  bridgeRejectedCommands: number;
  unrealRejectedCommands: number;
  commandsIgnored: number;
  unrealReceivedStatuses: number;
  unrealAcceptedStatuses: number;
  unrealStartedStatuses: number;
  commandsCompleted: number;
  commandsFailed: number;
  commandsCancelled: number;
  commandsExpired: number;
  commandsLostOnDisconnect: number;
  reconnections: number;
  arenaReconnects: number;
  twitchReconnects: number;
  activeCommands: number;
  trackedParticipants: number;
  arenaConnectionState: "connected" | "disconnected";
  twitchConnectionState: TwitchEventSubConnectionState;
}

interface TwitchBridgeMetricCounters {
  messagesReceived: number;
  commandsSubmitted: number;
  bridgeRejectedCommands: number;
  unrealRejectedCommands: number;
  commandsIgnored: number;
  unrealReceivedStatuses: number;
  unrealAcceptedStatuses: number;
  unrealStartedStatuses: number;
  commandsCompleted: number;
  commandsFailed: number;
  commandsCancelled: number;
  commandsExpired: number;
  commandsLostOnDisconnect: number;
  arenaReconnects: number;
  twitchReconnects: number;
}

interface CommandCorrelationContext {
  twitchMessageId: string;
  twitchDeliveryMessageId: string;
  chatterUserId: string;
  chatterUserLogin: string;
  chatterUserName: string;
  chatCommand: TwitchChatCommandName;
  arenaCommand: ArenaCommand;
  submittedAtMs: number;
  disposition?: SendDisposition;
}

interface PendingLifecycleCommand {
  actorId: string;
  chatCommand: "join" | "leave";
}

interface LimitRejection {
  errorCode: "user_cooldown" | "global_rate_limit" | "user_queue_full";
  message: string;
  details: Record<string, unknown>;
}

type ParticipantState = "joining" | "joined";

export class TwitchChatCommandProcessor {
  private readonly participantStates = new Map<string, ParticipantState>();
  private readonly pendingLifecycleCommands = new Map<string, PendingLifecycleCommand>();
  private readonly pendingActorByRequestId = new Map<string, string>();
  private readonly pendingCommandCountByActor = new Map<string, number>();
  private readonly lastAcceptedAtByUser = new Map<string, number>();
  private readonly acceptedCommandTimestamps: number[] = [];
  private readonly commandContexts = new Map<string, CommandCorrelationContext>();
  private readonly metrics: TwitchBridgeMetricCounters = {
    messagesReceived: 0,
    commandsSubmitted: 0,
    bridgeRejectedCommands: 0,
    unrealRejectedCommands: 0,
    commandsIgnored: 0,
    unrealReceivedStatuses: 0,
    unrealAcceptedStatuses: 0,
    unrealStartedStatuses: 0,
    commandsCompleted: 0,
    commandsFailed: 0,
    commandsCancelled: 0,
    commandsExpired: 0,
    commandsLostOnDisconnect: 0,
    arenaReconnects: 0,
    twitchReconnects: 0,
  };
  private readonly startedAtMs: number;
  private arenaConnectionState: "connected" | "disconnected";
  private twitchConnectionState: TwitchEventSubConnectionState = "disconnected";
  private hasArenaConnected: boolean;
  private hasTwitchConnected = false;
  private readonly removeStatusListener: () => void;
  private readonly removeConnectionListener: () => void;
  private readonly removeExpiredListener: () => void;

  public constructor(
    private readonly arena: ArenaCommandSink,
    private readonly limits: Readonly<TwitchChatCommandLimits> = DEFAULT_TWITCH_CHAT_COMMAND_LIMITS,
    private readonly parser = new TwitchChatCommandParser(),
    private readonly now: () => number = Date.now,
  ) {
    this.startedAtMs = this.now();
    this.arenaConnectionState = arena.connected ? "connected" : "disconnected";
    this.hasArenaConnected = arena.connected;
    this.removeStatusListener = arena.onStatus((status) => this.handleArenaStatus(status));
    this.removeConnectionListener = arena.onConnectionChange((connected) => {
      this.handleArenaConnectionChange(connected);
    });
    this.removeExpiredListener = arena.onCommandExpired((command) => {
      const context = this.commandContexts.get(command.requestId);
      if (context !== undefined) {
        this.metrics.commandsExpired += 1;
        this.logCommandLifecycle(context, "expired", {
          errorCode: "arena_queue_expired",
          message: "Command expired while waiting for Unreal Engine to reconnect.",
        });
        this.commandContexts.delete(command.requestId);
      }
      const pending = this.pendingLifecycleCommands.get(command.requestId);
      if (pending?.chatCommand === "join") {
        this.participantStates.delete(pending.actorId);
      }
      this.pendingLifecycleCommands.delete(command.requestId);
      this.releasePendingCommand(command.requestId);
    });
    this.logConnectionState("initialized");
  }

  public handle(message: TwitchChatMessage): void {
    this.metrics.messagesReceived += 1;
    const messageLength = Array.from(message.text).length;
    if (messageLength > this.limits.maxMessageLength) {
      this.logRejection(message, "message_too_long", "Chat command exceeds the configured length limit.", {
        messageLength,
        maximumMessageLength: this.limits.maxMessageLength,
      });
      return;
    }

    let chatCommand: TwitchChatCommandName;
    let arenaCommand: ArenaCommand;
    try {
      const translation = this.parser.parse(message);
      chatCommand = translation.chatCommand;
      arenaCommand = translation.arenaCommand;
    } catch (error) {
      if (error instanceof TwitchChatCommandError) {
        this.logRejection(message, error.code, error.message, { text: message.text });
        return;
      }

      log("error", "twitch_chat_command_rejected", {
        twitchMessageId: message.messageId,
        chatterUserId: message.chatterUserId,
        chatterUserLogin: message.chatterUserLogin,
        errorCode: "translation_failed",
        ...errorFields(error),
      });
      this.metrics.bridgeRejectedCommands += 1;
      return;
    }

    if (chatCommand === "join") {
      const existingState = this.participantStates.get(arenaCommand.actorId);
      if (existingState !== undefined) {
        this.metrics.commandsIgnored += 1;
        log("info", "twitch_chat_command_ignored", {
          twitchMessageId: message.messageId,
          chatterUserId: message.chatterUserId,
          chatterUserLogin: message.chatterUserLogin,
          chatCommand,
          reason: existingState === "joining" ? "join_pending" : "already_joined",
        });
        return;
      }

      if (this.participantStates.size >= this.limits.maxParticipants) {
        this.logRejection(
          message,
          "participant_limit_reached",
          "The Twitch participant limit has been reached.",
          { maximumParticipants: this.limits.maxParticipants },
        );
        return;
      }
    }

    const bypassLimits = chatCommand === "stop";
    const acceptedAt = this.now();
    if (!bypassLimits) {
      const rejection = this.checkLimits(message, arenaCommand, acceptedAt);
      if (rejection !== undefined) {
        this.logRejection(message, rejection.errorCode, rejection.message, {
          chatCommand,
          ...rejection.details,
        });
        return;
      }
    }

    if (chatCommand === "join") {
      this.participantStates.set(arenaCommand.actorId, "joining");
    }

    if (chatCommand === "join" || chatCommand === "leave") {
      this.pendingLifecycleCommands.set(arenaCommand.requestId, {
        actorId: arenaCommand.actorId,
        chatCommand,
      });
    }
    if (!bypassLimits) {
      this.trackPendingCommand(arenaCommand);
    }

    const context: CommandCorrelationContext = {
      twitchMessageId: message.messageId,
      twitchDeliveryMessageId: message.deliveryMessageId,
      chatterUserId: message.chatterUserId,
      chatterUserLogin: message.chatterUserLogin,
      chatterUserName: message.chatterUserName,
      chatCommand,
      arenaCommand,
      submittedAtMs: acceptedAt,
    };
    this.commandContexts.set(arenaCommand.requestId, context);

    try {
      const disposition = this.arena.send(arenaCommand);
      context.disposition = disposition;
      this.metrics.commandsSubmitted += 1;
      if (!bypassLimits) {
        this.recordAcceptedCommand(message.chatterUserId, acceptedAt);
      }
      this.logCommandLifecycle(context, "submitted", {
        disposition,
        arenaRequest: arenaCommand,
      });
      log("info", "twitch_chat_command_translated", {
        twitchMessageId: message.messageId,
        chatterUserId: message.chatterUserId,
        chatterUserLogin: message.chatterUserLogin,
        isBroadcaster: message.isBroadcaster,
        isModerator: message.isModerator,
        chatCommand,
        requestId: arenaCommand.requestId,
        actorId: arenaCommand.actorId,
        arenaCommand: arenaCommand.command,
        parameters: arenaCommand.parameters,
        disposition,
      });
    } catch (error) {
      if (chatCommand === "join") {
        this.participantStates.delete(arenaCommand.actorId);
      }
      this.pendingLifecycleCommands.delete(arenaCommand.requestId);
      this.releasePendingCommand(arenaCommand.requestId);
      this.logCommandLifecycle(context, "submission_failed", {
        errorCode: "arena_send_failed",
        ...errorFields(error),
      });
      this.commandContexts.delete(arenaCommand.requestId);
      this.logRejection(message, "arena_send_failed", "Could not send the command to the arena.", {
        requestId: arenaCommand.requestId,
        actorId: arenaCommand.actorId,
        chatCommand,
        ...errorFields(error),
      });
    }
  }

  public handleTwitchConnectionState(state: TwitchEventSubConnectionState): void {
    const previousState = this.twitchConnectionState;
    if (state === "connected") {
      if (this.hasTwitchConnected && previousState !== "connected") {
        this.metrics.twitchReconnects += 1;
      }
      this.hasTwitchConnected = true;
    }
    this.twitchConnectionState = state;
    if (state !== previousState) {
      this.logConnectionState("twitch_state_changed");
    }
  }

  public getMetricsSnapshot(): TwitchBridgeMetricsSnapshot {
    return {
      uptimeMs: Math.max(0, this.now() - this.startedAtMs),
      messagesReceived: this.metrics.messagesReceived,
      commandsSubmitted: this.metrics.commandsSubmitted,
      commandsRejected: this.metrics.bridgeRejectedCommands + this.metrics.unrealRejectedCommands,
      bridgeRejectedCommands: this.metrics.bridgeRejectedCommands,
      unrealRejectedCommands: this.metrics.unrealRejectedCommands,
      commandsIgnored: this.metrics.commandsIgnored,
      unrealReceivedStatuses: this.metrics.unrealReceivedStatuses,
      unrealAcceptedStatuses: this.metrics.unrealAcceptedStatuses,
      unrealStartedStatuses: this.metrics.unrealStartedStatuses,
      commandsCompleted: this.metrics.commandsCompleted,
      commandsFailed: this.metrics.commandsFailed,
      commandsCancelled: this.metrics.commandsCancelled,
      commandsExpired: this.metrics.commandsExpired,
      commandsLostOnDisconnect: this.metrics.commandsLostOnDisconnect,
      reconnections: this.metrics.arenaReconnects + this.metrics.twitchReconnects,
      arenaReconnects: this.metrics.arenaReconnects,
      twitchReconnects: this.metrics.twitchReconnects,
      activeCommands: this.commandContexts.size,
      trackedParticipants: this.participantStates.size,
      arenaConnectionState: this.arenaConnectionState,
      twitchConnectionState: this.twitchConnectionState,
    };
  }

  public logMetrics(reason: "startup" | "periodic" | "shutdown" | "manual"): void {
    log("info", "twitch_bridge_metrics", {
      reason,
      ...this.getMetricsSnapshot(),
    });
  }

  public dispose(): void {
    this.removeStatusListener();
    this.removeConnectionListener();
    this.removeExpiredListener();
    this.participantStates.clear();
    this.pendingLifecycleCommands.clear();
    this.clearPendingCommands();
    this.commandContexts.clear();
    this.lastAcceptedAtByUser.clear();
    this.acceptedCommandTimestamps.length = 0;
  }

  private checkLimits(
    message: TwitchChatMessage,
    arenaCommand: ArenaCommand,
    acceptedAt: number,
  ): LimitRejection | undefined {
    const lastAcceptedAt = this.lastAcceptedAtByUser.get(message.chatterUserId);
    if (this.limits.userCooldownMs > 0
      && lastAcceptedAt !== undefined
      && acceptedAt - lastAcceptedAt < this.limits.userCooldownMs) {
      const retryAfterMs = this.limits.userCooldownMs - (acceptedAt - lastAcceptedAt);
      return {
        errorCode: "user_cooldown",
        message: "The user is sending commands too quickly.",
        details: { retryAfterMs },
      };
    }

    this.removeExpiredRateSamples(acceptedAt);
    if (this.acceptedCommandTimestamps.length >= this.limits.globalCommandsPerSecond) {
      return {
        errorCode: "global_rate_limit",
        message: "The global Twitch command rate limit has been reached.",
        details: { maximumCommandsPerSecond: this.limits.globalCommandsPerSecond },
      };
    }

    const pendingCount = this.pendingCommandCountByActor.get(arenaCommand.actorId) ?? 0;
    if (pendingCount >= this.limits.userQueueLimit) {
      return {
        errorCode: "user_queue_full",
        message: "The participant already has the maximum number of active or queued commands.",
        details: {
          pendingCommandCount: pendingCount,
          maximumQueuedCommands: this.limits.userQueueLimit,
        },
      };
    }
    return undefined;
  }

  private recordAcceptedCommand(chatterUserId: string, acceptedAt: number): void {
    this.lastAcceptedAtByUser.set(chatterUserId, acceptedAt);
    this.acceptedCommandTimestamps.push(acceptedAt);
  }

  private removeExpiredRateSamples(now: number): void {
    const oldestAllowedTimestamp = now - 1_000;
    while (this.acceptedCommandTimestamps[0] !== undefined
      && (this.acceptedCommandTimestamps[0] as number) <= oldestAllowedTimestamp) {
      this.acceptedCommandTimestamps.shift();
    }
  }

  private trackPendingCommand(command: ArenaCommand): void {
    this.pendingActorByRequestId.set(command.requestId, command.actorId);
    const currentCount = this.pendingCommandCountByActor.get(command.actorId) ?? 0;
    this.pendingCommandCountByActor.set(command.actorId, currentCount + 1);
  }

  private releasePendingCommand(requestId: string): void {
    const actorId = this.pendingActorByRequestId.get(requestId);
    if (actorId === undefined) {
      return;
    }
    this.pendingActorByRequestId.delete(requestId);

    const currentCount = this.pendingCommandCountByActor.get(actorId) ?? 0;
    if (currentCount <= 1) {
      this.pendingCommandCountByActor.delete(actorId);
    } else {
      this.pendingCommandCountByActor.set(actorId, currentCount - 1);
    }
  }

  private clearPendingCommands(): void {
    this.pendingActorByRequestId.clear();
    this.pendingCommandCountByActor.clear();
  }

  private handleArenaStatus(status: ArenaCommandStatus): void {
    const context = this.commandContexts.get(status.requestId);
    if (context !== undefined) {
      this.recordUnrealStatus(status);
      this.logCommandLifecycle(context, status.status, {
        errorCode: status.errorCode,
        message: status.message,
      });
      if (TERMINAL_STATUS_NAMES.has(status.status)) {
        this.commandContexts.delete(status.requestId);
      }
    }

    if (!TERMINAL_STATUS_NAMES.has(status.status)) {
      return;
    }
    this.releasePendingCommand(status.requestId);
    const pending = this.pendingLifecycleCommands.get(status.requestId);
    if (pending === undefined) {
      return;
    }
    this.pendingLifecycleCommands.delete(status.requestId);

    if (pending.chatCommand === "join") {
      if (status.status === "completed" || status.errorCode === "duplicate_participant") {
        this.participantStates.set(pending.actorId, "joined");
      } else {
        this.participantStates.delete(pending.actorId);
      }
      return;
    }

    if (status.status === "completed" || status.errorCode === "unknown_participant") {
      this.participantStates.delete(pending.actorId);
    }
  }

  private logRejection(
    message: TwitchChatMessage,
    errorCode: string,
    rejectionMessage: string,
    details: Record<string, unknown> = {},
  ): void {
    this.metrics.bridgeRejectedCommands += 1;
    log("warn", "twitch_chat_command_rejected", {
      twitchMessageId: message.messageId,
      chatterUserId: message.chatterUserId,
      chatterUserLogin: message.chatterUserLogin,
      errorCode,
      message: rejectionMessage,
      ...details,
    });
  }

  private handleArenaConnectionChange(connected: boolean): void {
    const previousState = this.arenaConnectionState;
    const nextState = connected ? "connected" : "disconnected";
    if (connected) {
      if (this.hasArenaConnected && previousState !== "connected") {
        this.metrics.arenaReconnects += 1;
      }
      this.hasArenaConnected = true;
    } else {
      for (const context of this.commandContexts.values()) {
        this.metrics.commandsLostOnDisconnect += 1;
        this.logCommandLifecycle(context, "connection_lost", {
          errorCode: "arena_disconnected",
          message: "Command tracking ended because the Unreal Engine connection was lost.",
        });
      }
      this.commandContexts.clear();
      this.participantStates.clear();
      this.pendingLifecycleCommands.clear();
      this.clearPendingCommands();
    }
    this.arenaConnectionState = nextState;
    if (nextState !== previousState) {
      this.logConnectionState("arena_state_changed");
    }
  }

  private recordUnrealStatus(status: ArenaCommandStatus): void {
    switch (status.status) {
      case "received":
        this.metrics.unrealReceivedStatuses += 1;
        break;
      case "accepted":
        this.metrics.unrealAcceptedStatuses += 1;
        break;
      case "started":
        this.metrics.unrealStartedStatuses += 1;
        break;
      case "completed":
        this.metrics.commandsCompleted += 1;
        break;
      case "rejected":
        this.metrics.unrealRejectedCommands += 1;
        break;
      case "failed":
        this.metrics.commandsFailed += 1;
        break;
      case "cancelled":
        this.metrics.commandsCancelled += 1;
        break;
    }
  }

  private logCommandLifecycle(
    context: CommandCorrelationContext,
    lifecycleStatus: string,
    details: Record<string, unknown> = {},
  ): void {
    const level = lifecycleStatus === "rejected"
      || lifecycleStatus === "failed"
      || lifecycleStatus === "cancelled"
      || lifecycleStatus === "expired"
      || lifecycleStatus === "connection_lost"
      || lifecycleStatus === "submission_failed"
      ? "warn"
      : "info";
    log(level, "twitch_command_lifecycle", {
      twitchMessageId: context.twitchMessageId,
      twitchDeliveryMessageId: context.twitchDeliveryMessageId,
      chatterUserId: context.chatterUserId,
      chatterUserLogin: context.chatterUserLogin,
      chatterUserName: context.chatterUserName,
      chatCommand: context.chatCommand,
      requestId: context.arenaCommand.requestId,
      actorId: context.arenaCommand.actorId,
      arenaCommand: context.arenaCommand.command,
      lifecycleStatus,
      elapsedMs: Math.max(0, this.now() - context.submittedAtMs),
      ...(context.disposition === undefined ? {} : { disposition: context.disposition }),
      ...details,
    });
  }

  private logConnectionState(reason: string): void {
    log("info", "bridge_connection_state", {
      reason,
      twitch: this.twitchConnectionState,
      unreal: this.arenaConnectionState,
      ready: this.twitchConnectionState === "connected" && this.arenaConnectionState === "connected",
      twitchReconnects: this.metrics.twitchReconnects,
      arenaReconnects: this.metrics.arenaReconnects,
    });
  }
}
