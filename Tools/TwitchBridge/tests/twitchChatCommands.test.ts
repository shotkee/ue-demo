import assert from "node:assert/strict";
import test from "node:test";

import type { SendDisposition } from "../src/arenaBridgeServer.js";
import type { ArenaCommand, ArenaCommandStatus } from "../src/protocol.js";
import { TwitchChatCommandProcessor, type ArenaCommandSink } from "../src/twitchChatCommandProcessor.js";
import {
  TwitchChatCommandError,
  TwitchChatCommandParser,
  type TwitchChatCommandErrorCode,
} from "../src/twitchChatCommands.js";
import type { TwitchChatMessage } from "../src/twitchEventSub.js";

function chatMessage(
  userId: string,
  login: string,
  text: string,
  messageId: string,
  displayName = login,
): TwitchChatMessage {
  return {
    deliveryMessageId: `delivery-${messageId}`,
    messageId,
    broadcasterUserId: "9000",
    broadcasterUserLogin: "broadcaster",
    broadcasterUserName: "Broadcaster",
    chatterUserId: userId,
    chatterUserLogin: login,
    chatterUserName: displayName,
    text,
    receivedAt: "2026-08-22T12:00:00.000000000Z",
  };
}

function assertChatError(
  parser: TwitchChatCommandParser,
  message: TwitchChatMessage,
  errorCode: TwitchChatCommandErrorCode,
): void {
  assert.throws(
    () => parser.parse(message),
    (error: unknown) => error instanceof TwitchChatCommandError && error.code === errorCode,
  );
}

class FakeArena implements ArenaCommandSink {
  public readonly commands: ArenaCommand[] = [];
  private readonly statusListeners = new Set<(status: ArenaCommandStatus) => void>();
  private readonly connectionListeners = new Set<(connected: boolean) => void>();
  private readonly expiredListeners = new Set<(command: ArenaCommand) => void>();

  public send(command: ArenaCommand): SendDisposition {
    this.commands.push(command);
    return "sent";
  }

  public onStatus(listener: (status: ArenaCommandStatus) => void): () => void {
    this.statusListeners.add(listener);
    return () => this.statusListeners.delete(listener);
  }

  public onConnectionChange(listener: (connected: boolean) => void): () => void {
    this.connectionListeners.add(listener);
    return () => this.connectionListeners.delete(listener);
  }

  public onCommandExpired(listener: (command: ArenaCommand) => void): () => void {
    this.expiredListeners.add(listener);
    return () => this.expiredListeners.delete(listener);
  }

  public emitStatus(status: ArenaCommandStatus): void {
    for (const listener of this.statusListeners) {
      listener(status);
    }
  }

  public emitConnection(connected: boolean): void {
    for (const listener of this.connectionListeners) {
      listener(connected);
    }
  }

  public emitExpired(command: ArenaCommand): void {
    for (const listener of this.expiredListeners) {
      listener(command);
    }
  }
}

test("translates the fixed chat syntax into the existing arena JSON protocol", () => {
  const parser = new TwitchChatCommandParser();
  const aliceJoin = parser.parse(chatMessage("1001", "alice", "!JOIN", "join-1", "Alice"));
  assert.deepEqual(aliceJoin, {
    chatCommand: "join",
    arenaCommand: {
      version: 1,
      requestId: "twitch:join-1",
      actorId: "twitch:1001",
      command: "spawn",
      parameters: { displayName: "Alice" },
    },
  });

  const cases = [
    {
      message: chatMessage("2002", "bob", "!GoTo CENTER", "goto-1", "Bob"),
      chatCommand: "goto",
      command: "move_to_point",
      parameters: { targetId: "center", movementMode: "walk" },
    },
    {
      message: chatMessage("2002", "bob", "!RUN @ALICE", "run-1", "Bob"),
      chatCommand: "run",
      command: "move_to_actor",
      parameters: { targetId: "twitch:1001", movementMode: "run" },
    },
    {
      message: chatMessage("2002", "bob", "!hit @1001", "hit-1", "Bob"),
      chatCommand: "hit",
      command: "play_action",
      parameters: { actionId: "punch", targetType: "participant", targetId: "twitch:1001" },
    },
    {
      message: chatMessage("2002", "bob", "!go Crate_1", "go-1", "Bob"),
      chatCommand: "go",
      command: "approach_object",
      parameters: { targetId: "crate_1", movementMode: "walk" },
    },
    {
      message: chatMessage("2002", "bob", "!stop", "stop-1", "Bob"),
      chatCommand: "stop",
      command: "stop",
      parameters: {},
    },
    {
      message: chatMessage("2002", "bob", "!leave", "leave-1", "Bob"),
      chatCommand: "leave",
      command: "leave",
      parameters: {},
    },
  ] as const;

  for (const expected of cases) {
    const translation = parser.parse(expected.message);
    assert.equal(translation.chatCommand, expected.chatCommand);
    assert.deepEqual(translation.arenaCommand, {
      version: 1,
      requestId: `twitch:${expected.message.messageId}`,
      actorId: "twitch:2002",
      command: expected.command,
      parameters: expected.parameters,
    });
  }
});

test("validates arguments and never accepts an actor ID from chat text", () => {
  const parser = new TwitchChatCommandParser();
  const invalidCases: Array<[string, TwitchChatCommandErrorCode]> = [
    ["!", "missing_command"],
    ["!dance", "unknown_command"],
    ["!join another-user", "invalid_arguments"],
    ["!goto", "invalid_arguments"],
    ["!goto ../secret", "invalid_identifier"],
    ["!run alice", "invalid_target"],
    ["!run @unknown", "unknown_target"],
    ["!stop twitch:someone-else", "invalid_arguments"],
  ];

  invalidCases.forEach(([text, errorCode], index) => {
    assertChatError(parser, chatMessage("2002", "bob", text, `invalid-${index}`), errorCode);
  });
});

test("resolves participant targets by current login or numeric Twitch user ID", () => {
  const parser = new TwitchChatCommandParser();
  parser.parse(chatMessage("1001", "alice", "!join", "alice-join", "Alice"));

  const byLogin = parser.parse(chatMessage("2002", "bob", "!run @ALICE", "by-login"));
  assert.equal(byLogin.arenaCommand.parameters.targetId, "twitch:1001");

  const byId = parser.parse(chatMessage("2002", "bob", "!run @1001", "by-id"));
  assert.equal(byId.arenaCommand.parameters.targetId, "twitch:1001");

  parser.parse(chatMessage("1001", "alice_new", "!stop", "alice-renamed", "AliceNew"));
  assertChatError(
    parser,
    chatMessage("2002", "bob", "!run @alice", "old-login"),
    "unknown_target",
  );
  const byCurrentLogin = parser.parse(
    chatMessage("2002", "bob", "!run @ALICE_NEW", "current-login"),
  );
  assert.equal(byCurrentLogin.arenaCommand.parameters.targetId, "twitch:1001");
});

test("keeps join idempotent and resets participant state after leave or Unreal reconnect", () => {
  const arena = new FakeArena();
  const processor = new TwitchChatCommandProcessor(arena);

  try {
    processor.handle(chatMessage("1001", "alice", "!join", "join-1", "Alice"));
    processor.handle(chatMessage("1001", "alice", "!join", "join-pending", "Alice"));
    assert.equal(arena.commands.length, 1);

    arena.emitStatus({
      version: 1,
      requestId: "twitch:join-1",
      status: "completed",
      errorCode: null,
      message: "Participant was spawned.",
    });
    processor.handle(chatMessage("1001", "alice", "!join", "join-completed", "Alice"));
    assert.equal(arena.commands.length, 1);

    processor.handle(chatMessage("1001", "alice", "!leave", "leave-1", "Alice"));
    assert.equal(arena.commands.length, 2);
    arena.emitStatus({
      version: 1,
      requestId: "twitch:leave-1",
      status: "completed",
      errorCode: null,
      message: "Participant left the arena.",
    });
    processor.handle(chatMessage("1001", "alice", "!join", "join-2", "Alice"));
    assert.equal(arena.commands.length, 3);

    arena.emitStatus({
      version: 1,
      requestId: "twitch:join-2",
      status: "rejected",
      errorCode: "duplicate_participant",
      message: "Participant already exists.",
    });
    processor.handle(chatMessage("1001", "alice", "!join", "join-after-duplicate", "Alice"));
    assert.equal(arena.commands.length, 3);

    arena.emitConnection(false);
    processor.handle(chatMessage("1001", "alice", "!join", "join-after-reconnect", "Alice"));
    assert.equal(arena.commands.length, 4);

    const expiredJoin = arena.commands[3];
    assert.ok(expiredJoin !== undefined);
    arena.emitExpired(expiredJoin);
    processor.handle(chatMessage("1001", "alice", "!join", "join-after-expiry", "Alice"));
    assert.equal(arena.commands.length, 5);

    processor.handle(chatMessage("1001", "alice", "!goto", "invalid-command", "Alice"));
    assert.equal(arena.commands.length, 5);
  } finally {
    processor.dispose();
  }
});
