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
import type { TwitchChatMessage } from "./twitchEventSub.js";

export interface ArenaCommandSink {
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
  private readonly removeStatusListener: () => void;
  private readonly removeConnectionListener: () => void;
  private readonly removeExpiredListener: () => void;

  public constructor(
    private readonly arena: ArenaCommandSink,
    private readonly limits: Readonly<TwitchChatCommandLimits> = DEFAULT_TWITCH_CHAT_COMMAND_LIMITS,
    private readonly parser = new TwitchChatCommandParser(),
    private readonly now: () => number = Date.now,
  ) {
    this.removeStatusListener = arena.onStatus((status) => this.handleArenaStatus(status));
    this.removeConnectionListener = arena.onConnectionChange((connected) => {
      if (!connected) {
        this.participantStates.clear();
        this.pendingLifecycleCommands.clear();
        this.clearPendingCommands();
      }
    });
    this.removeExpiredListener = arena.onCommandExpired((command) => {
      const pending = this.pendingLifecycleCommands.get(command.requestId);
      if (pending?.chatCommand === "join") {
        this.participantStates.delete(pending.actorId);
      }
      this.pendingLifecycleCommands.delete(command.requestId);
      this.releasePendingCommand(command.requestId);
    });
  }

  public handle(message: TwitchChatMessage): void {
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
      return;
    }

    if (chatCommand === "join") {
      const existingState = this.participantStates.get(arenaCommand.actorId);
      if (existingState !== undefined) {
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

    try {
      const disposition = this.arena.send(arenaCommand);
      if (!bypassLimits) {
        this.recordAcceptedCommand(message.chatterUserId, acceptedAt);
      }
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
      this.logRejection(message, "arena_send_failed", "Could not send the command to the arena.", {
        ...errorFields(error),
      });
    }
  }

  public dispose(): void {
    this.removeStatusListener();
    this.removeConnectionListener();
    this.removeExpiredListener();
    this.participantStates.clear();
    this.pendingLifecycleCommands.clear();
    this.clearPendingCommands();
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
    log("warn", "twitch_chat_command_rejected", {
      twitchMessageId: message.messageId,
      chatterUserId: message.chatterUserId,
      chatterUserLogin: message.chatterUserLogin,
      errorCode,
      message: rejectionMessage,
      ...details,
    });
  }
}
