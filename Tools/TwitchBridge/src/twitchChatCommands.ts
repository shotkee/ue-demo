import { ARENA_PROTOCOL_VERSION, type ArenaCommand } from "./protocol.js";
import type { TwitchChatMessage } from "./twitchEventSub.js";

export const TWITCH_CHAT_COMMAND_PREFIX = "!";

export type TwitchChatCommandName = "join" | "goto" | "run" | "hit" | "go" | "stop" | "leave";

export type TwitchChatCommandErrorCode =
  | "missing_command"
  | "unknown_command"
  | "invalid_arguments"
  | "invalid_identifier"
  | "invalid_target"
  | "unknown_target"
  | "permission_denied"
  | "invalid_twitch_identity";

export interface TwitchChatCommandTranslation {
  chatCommand: TwitchChatCommandName;
  arenaCommand: ArenaCommand;
}

interface KnownTwitchUser {
  userId: string;
  login: string;
  displayName: string;
}

const TWITCH_USER_ID_PATTERN = /^[0-9]{1,32}$/;
const TWITCH_LOGIN_PATTERN = /^[a-z0-9_]{1,25}$/;
const ARENA_IDENTIFIER_PATTERN = /^[a-z0-9][a-z0-9_-]{0,63}$/;
const MAXIMUM_ARENA_IDENTIFIER_LENGTH = 128;

const USAGE: Record<TwitchChatCommandName, string> = {
  join: "!join",
  goto: "!goto <point_id>",
  run: "!run @<login|twitch_user_id>",
  hit: "!hit @<login|twitch_user_id>",
  go: "!go <object_id>",
  stop: "!stop [@<login|twitch_user_id>]",
  leave: "!leave [@<login|twitch_user_id>]",
};

export class TwitchChatCommandError extends Error {
  public constructor(
    public readonly code: TwitchChatCommandErrorCode,
    message: string,
    public readonly usage?: string,
  ) {
    super(usage === undefined ? message : `${message} Usage: ${usage}`);
    this.name = "TwitchChatCommandError";
  }
}

function twitchEntityId(userId: string): string {
  const normalizedUserId = userId.trim();
  if (!TWITCH_USER_ID_PATTERN.test(normalizedUserId)) {
    throw new TwitchChatCommandError(
      "invalid_twitch_identity",
      "Twitch supplied an invalid chatter user ID.",
    );
  }

  const entityId = `twitch:${normalizedUserId}`;
  if (entityId.length > MAXIMUM_ARENA_IDENTIFIER_LENGTH) {
    throw new TwitchChatCommandError(
      "invalid_twitch_identity",
      "The Twitch chatter user ID is too long for the arena protocol.",
    );
  }
  return entityId;
}

function requestId(messageId: string): string {
  const normalizedMessageId = messageId.trim();
  const value = `twitch:${normalizedMessageId}`;
  if (normalizedMessageId === "" || value.length > MAXIMUM_ARENA_IDENTIFIER_LENGTH) {
    throw new TwitchChatCommandError(
      "invalid_twitch_identity",
      "Twitch supplied an invalid chat message ID.",
    );
  }
  return value;
}

function requireArgumentCount(
  commandName: TwitchChatCommandName,
  args: readonly string[],
  expected: number,
): void {
  if (args.length === expected) {
    return;
  }

  throw new TwitchChatCommandError(
    "invalid_arguments",
    `Command '!${commandName}' expects ${expected} argument${expected === 1 ? "" : "s"}, but received ${args.length}.`,
    USAGE[commandName],
  );
}

function arenaIdentifier(value: string, commandName: TwitchChatCommandName): string {
  const normalizedValue = value.trim().toLowerCase();
  if (!ARENA_IDENTIFIER_PATTERN.test(normalizedValue)) {
    throw new TwitchChatCommandError(
      "invalid_identifier",
      `Argument '${value}' is not a valid arena identifier. Use letters, digits, '_' or '-'.`,
      USAGE[commandName],
    );
  }
  return normalizedValue;
}

export class TwitchChatCommandParser {
  private readonly usersById = new Map<string, KnownTwitchUser>();
  private readonly userIdByLogin = new Map<string, string>();

  public parse(message: TwitchChatMessage): TwitchChatCommandTranslation {
    this.rememberUser(message);

    const words = message.text.trim().split(/\s+/u);
    const commandToken = words.shift() ?? "";
    if (!commandToken.startsWith(TWITCH_CHAT_COMMAND_PREFIX) || commandToken.length === 1) {
      throw new TwitchChatCommandError(
        "missing_command",
        `A chat command must start with '${TWITCH_CHAT_COMMAND_PREFIX}' followed by a command name.`,
      );
    }

    const commandName = commandToken.slice(TWITCH_CHAT_COMMAND_PREFIX.length).toLowerCase();
    if (!this.isSupportedCommand(commandName)) {
      throw new TwitchChatCommandError(
        "unknown_command",
        `Unknown chat command '${commandToken}'.`,
      );
    }

    const actorId = twitchEntityId(message.chatterUserId);
    const baseCommand = {
      version: ARENA_PROTOCOL_VERSION,
      requestId: requestId(message.messageId),
      actorId,
    } as const;

    switch (commandName) {
      case "join":
        requireArgumentCount(commandName, words, 0);
        return {
          chatCommand: commandName,
          arenaCommand: {
            ...baseCommand,
            command: "spawn",
            parameters: { displayName: message.chatterUserName },
          },
        };
      case "goto":
        requireArgumentCount(commandName, words, 1);
        return {
          chatCommand: commandName,
          arenaCommand: {
            ...baseCommand,
            command: "move_to_point",
            parameters: {
              targetId: arenaIdentifier(words[0] ?? "", commandName),
              movementMode: "walk",
            },
          },
        };
      case "run":
        requireArgumentCount(commandName, words, 1);
        return {
          chatCommand: commandName,
          arenaCommand: {
            ...baseCommand,
            command: "move_to_actor",
            parameters: {
              targetId: this.resolveParticipantTarget(words[0] ?? "", commandName),
              movementMode: "run",
            },
          },
        };
      case "hit":
        requireArgumentCount(commandName, words, 1);
        return {
          chatCommand: commandName,
          arenaCommand: {
            ...baseCommand,
            command: "play_action",
            parameters: {
              actionId: "punch",
              targetType: "participant",
              targetId: this.resolveParticipantTarget(words[0] ?? "", commandName),
            },
          },
        };
      case "go":
        requireArgumentCount(commandName, words, 1);
        return {
          chatCommand: commandName,
          arenaCommand: {
            ...baseCommand,
            command: "approach_object",
            parameters: {
              targetId: arenaIdentifier(words[0] ?? "", commandName),
              movementMode: "walk",
            },
          },
        };
      case "stop":
      {
        const commandActorId = this.resolveAdministrativeActor(
          message,
          words,
          commandName,
          actorId,
        );
        return {
          chatCommand: commandName,
          arenaCommand: {
            ...baseCommand,
            actorId: commandActorId,
            command: "stop",
            parameters: {},
          },
        };
      }
      case "leave":
      {
        const commandActorId = this.resolveAdministrativeActor(
          message,
          words,
          commandName,
          actorId,
        );
        return {
          chatCommand: commandName,
          arenaCommand: {
            ...baseCommand,
            actorId: commandActorId,
            command: "leave",
            parameters: {},
          },
        };
      }
    }
  }

  private rememberUser(message: TwitchChatMessage): void {
    const userId = message.chatterUserId.trim();
    twitchEntityId(userId);
    const login = message.chatterUserLogin.trim().toLowerCase();
    if (!TWITCH_LOGIN_PATTERN.test(login)) {
      throw new TwitchChatCommandError(
        "invalid_twitch_identity",
        "Twitch supplied an invalid chatter login.",
      );
    }

    const previousUser = this.usersById.get(userId);
    if (previousUser !== undefined && this.userIdByLogin.get(previousUser.login) === userId) {
      this.userIdByLogin.delete(previousUser.login);
    }

    const user: KnownTwitchUser = {
      userId,
      login,
      displayName: message.chatterUserName,
    };
    this.usersById.set(userId, user);
    this.userIdByLogin.set(login, userId);
  }

  private resolveParticipantTarget(reference: string, commandName: TwitchChatCommandName): string {
    if (!reference.startsWith("@") || reference.length === 1) {
      throw new TwitchChatCommandError(
        "invalid_target",
        `Participant target '${reference}' must start with '@'.`,
        USAGE[commandName],
      );
    }

    const target = reference.slice(1).toLowerCase();
    if (TWITCH_USER_ID_PATTERN.test(target)) {
      return twitchEntityId(target);
    }
    if (!TWITCH_LOGIN_PATTERN.test(target)) {
      throw new TwitchChatCommandError(
        "invalid_target",
        `Participant target '${reference}' is not a valid Twitch login or user ID.`,
        USAGE[commandName],
      );
    }

    const userId = this.userIdByLogin.get(target);
    if (userId === undefined) {
      throw new TwitchChatCommandError(
        "unknown_target",
        `Twitch user '${reference}' has not been seen by the bridge. Ask that user to send !join first.`,
        USAGE[commandName],
      );
    }
    return twitchEntityId(userId);
  }

  private resolveAdministrativeActor(
    message: TwitchChatMessage,
    args: readonly string[],
    commandName: "stop" | "leave",
    ownActorId: string,
  ): string {
    if (args.length === 0) {
      return ownActorId;
    }
    if (args.length !== 1) {
      throw new TwitchChatCommandError(
        "invalid_arguments",
        `Command '!${commandName}' expects no arguments or one participant target, but received ${args.length}.`,
        USAGE[commandName],
      );
    }
    if (!message.isBroadcaster && !message.isModerator) {
      throw new TwitchChatCommandError(
        "permission_denied",
        `Only the broadcaster or a moderator may use '!${commandName}' on another participant.`,
        USAGE[commandName],
      );
    }
    return this.resolveParticipantTarget(args[0] ?? "", commandName);
  }

  private isSupportedCommand(value: string): value is TwitchChatCommandName {
    return value === "join"
      || value === "goto"
      || value === "run"
      || value === "hit"
      || value === "go"
      || value === "stop"
      || value === "leave";
  }
}
