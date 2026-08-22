import { createInterface } from "node:readline";
import { setTimeout as delay } from "node:timers/promises";

import type { ArenaBridgeServer } from "./arenaBridgeServer.js";
import { errorFields, log } from "./logger.js";
import {
  ARENA_PROTOCOL_VERSION,
  parseArenaCommand,
  TERMINAL_STATUS_NAMES,
  type ArenaCommand,
  type ArenaCommandStatus,
} from "./protocol.js";

function waitForTerminalStatus(
  server: ArenaBridgeServer,
  requestId: string,
  timeoutMs: number,
): { promise: Promise<ArenaCommandStatus>; cancel: () => void } {
  let removeListener = (): void => undefined;
  let timeout: NodeJS.Timeout | undefined;

  const promise = new Promise<ArenaCommandStatus>((resolve, reject) => {
    removeListener = server.onStatus((status) => {
      if (status.requestId !== requestId || !TERMINAL_STATUS_NAMES.has(status.status)) {
        return;
      }

      if (timeout !== undefined) {
        clearTimeout(timeout);
      }
      removeListener();
      resolve(status);
    });

    timeout = setTimeout(() => {
      removeListener();
      reject(new Error(`Command '${requestId}' did not reach a terminal status within ${timeoutMs} ms.`));
    }, timeoutMs);
  });

  return {
    promise,
    cancel: () => {
      if (timeout !== undefined) {
        clearTimeout(timeout);
      }
      removeListener();
    },
  };
}

async function sendAndExpectCompleted(
  server: ArenaBridgeServer,
  command: ArenaCommand,
  timeoutMs: number,
): Promise<void> {
  const terminalStatus = waitForTerminalStatus(server, command.requestId, timeoutMs);
  try {
    server.send(command);
    const status = await terminalStatus.promise;
    if (status.status !== "completed") {
      throw new Error(
        `Command '${command.requestId}' ended with ${status.status}/${status.errorCode ?? "none"}: ${status.message}`,
      );
    }
  } catch (error) {
    terminalStatus.cancel();
    throw error;
  }
}

export async function runSmokeMode(server: ArenaBridgeServer, statusTimeoutMs: number): Promise<void> {
  log("info", "smoke_waiting_for_unreal");
  await server.waitForConnection(0);

  const suffix = Date.now().toString();
  const actorId = `bridge-test-${suffix}`;
  const spawnCommand: ArenaCommand = {
    version: ARENA_PROTOCOL_VERSION,
    requestId: `bridge-spawn-${suffix}`,
    actorId,
    command: "spawn",
    parameters: { displayName: "Twitch Bridge Test" },
  };
  const moveCommand: ArenaCommand = {
    version: ARENA_PROTOCOL_VERSION,
    requestId: `bridge-move-${suffix}`,
    actorId,
    command: "move_to_point",
    parameters: { targetId: "center", movementMode: "walk" },
  };

  await sendAndExpectCompleted(server, spawnCommand, statusTimeoutMs);
  log("info", "smoke_movement_delay", {
    message: "The mannequin will start walking in 1.5 seconds.",
  });
  await delay(1_500);
  await sendAndExpectCompleted(server, moveCommand, statusTimeoutMs);
  log("info", "smoke_completed", {
    message: "Twitch bridge smoke test completed successfully.",
    actorId,
  });
}

export async function runStdinMode(server: ArenaBridgeServer): Promise<void> {
  log("info", "stdin_mode_ready", {
    message: "Enter one arena JSON command per line. Press Ctrl+C to stop.",
  });

  const input = createInterface({
    input: process.stdin,
    crlfDelay: Number.POSITIVE_INFINITY,
  });

  for await (const line of input) {
    const trimmedLine = line.trim();
    if (trimmedLine === "") {
      continue;
    }

    try {
      const value: unknown = JSON.parse(trimmedLine);
      const command = parseArenaCommand(value);
      const disposition = server.send(command);
      log("info", "stdin_command_accepted", {
        requestId: command.requestId,
        disposition,
      });
    } catch (error) {
      log("warn", "stdin_command_rejected", errorFields(error));
    }
  }
}
