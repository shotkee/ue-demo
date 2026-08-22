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

interface PendingLifecycleCommand {
  actorId: string;
  chatCommand: "join" | "leave";
}

type ParticipantState = "joining" | "joined";

export class TwitchChatCommandProcessor {
  private readonly participantStates = new Map<string, ParticipantState>();
  private readonly pendingLifecycleCommands = new Map<string, PendingLifecycleCommand>();
  private readonly removeStatusListener: () => void;
  private readonly removeConnectionListener: () => void;
  private readonly removeExpiredListener: () => void;

  public constructor(
    private readonly arena: ArenaCommandSink,
    private readonly parser = new TwitchChatCommandParser(),
  ) {
    this.removeStatusListener = arena.onStatus((status) => this.handleArenaStatus(status));
    this.removeConnectionListener = arena.onConnectionChange((connected) => {
      if (!connected) {
        this.participantStates.clear();
        this.pendingLifecycleCommands.clear();
      }
    });
    this.removeExpiredListener = arena.onCommandExpired((command) => {
      const pending = this.pendingLifecycleCommands.get(command.requestId);
      if (pending?.chatCommand === "join") {
        this.participantStates.delete(pending.actorId);
      }
      this.pendingLifecycleCommands.delete(command.requestId);
    });
  }

  public handle(message: TwitchChatMessage): void {
    let chatCommand: TwitchChatCommandName;
    let arenaCommand: ArenaCommand;
    try {
      const translation = this.parser.parse(message);
      chatCommand = translation.chatCommand;
      arenaCommand = translation.arenaCommand;
    } catch (error) {
      if (error instanceof TwitchChatCommandError) {
        log("warn", "twitch_chat_command_rejected", {
          twitchMessageId: message.messageId,
          chatterUserId: message.chatterUserId,
          chatterUserLogin: message.chatterUserLogin,
          text: message.text,
          errorCode: error.code,
          message: error.message,
        });
        return;
      }

      log("error", "twitch_chat_command_rejected", {
        twitchMessageId: message.messageId,
        chatterUserId: message.chatterUserId,
        chatterUserLogin: message.chatterUserLogin,
        text: message.text,
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
      this.participantStates.set(arenaCommand.actorId, "joining");
    }

    if (chatCommand === "join" || chatCommand === "leave") {
      this.pendingLifecycleCommands.set(arenaCommand.requestId, {
        actorId: arenaCommand.actorId,
        chatCommand,
      });
    }

    try {
      const disposition = this.arena.send(arenaCommand);
      log("info", "twitch_chat_command_translated", {
        twitchMessageId: message.messageId,
        chatterUserId: message.chatterUserId,
        chatterUserLogin: message.chatterUserLogin,
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
      log("warn", "twitch_chat_command_rejected", {
        twitchMessageId: message.messageId,
        chatterUserId: message.chatterUserId,
        chatterUserLogin: message.chatterUserLogin,
        text: message.text,
        errorCode: "arena_send_failed",
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
  }

  private handleArenaStatus(status: ArenaCommandStatus): void {
    if (!TERMINAL_STATUS_NAMES.has(status.status)) {
      return;
    }

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
}
