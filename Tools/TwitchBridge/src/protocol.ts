export const ARENA_PROTOCOL_VERSION = 1 as const;

export interface ArenaCommand {
  version: typeof ARENA_PROTOCOL_VERSION;
  requestId: string;
  actorId: string;
  command: string;
  parameters: Record<string, unknown>;
}

export type ArenaCommandStatusName =
  | "received"
  | "accepted"
  | "started"
  | "completed"
  | "rejected"
  | "failed"
  | "cancelled";

export interface ArenaCommandStatus {
  version: number;
  requestId: string;
  status: ArenaCommandStatusName;
  errorCode: string | null;
  message: string;
}

const STATUS_NAMES = new Set<ArenaCommandStatusName>([
  "received",
  "accepted",
  "started",
  "completed",
  "rejected",
  "failed",
  "cancelled",
]);

export const TERMINAL_STATUS_NAMES = new Set<ArenaCommandStatusName>([
  "completed",
  "rejected",
  "failed",
  "cancelled",
]);

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

export function parseArenaCommand(value: unknown): ArenaCommand {
  if (!isRecord(value)) {
    throw new Error("Command must be a JSON object.");
  }

  if (value.version !== ARENA_PROTOCOL_VERSION) {
    throw new Error(`Command version must be ${ARENA_PROTOCOL_VERSION}.`);
  }

  if (typeof value.requestId !== "string" || value.requestId.trim() === "") {
    throw new Error("Command requestId must be a non-empty string.");
  }

  if (typeof value.actorId !== "string" || value.actorId.trim() === "") {
    throw new Error("Command actorId must be a non-empty string.");
  }

  if (typeof value.command !== "string" || value.command.trim() === "") {
    throw new Error("Command name must be a non-empty string.");
  }

  if (!isRecord(value.parameters)) {
    throw new Error("Command parameters must be a JSON object.");
  }

  return {
    version: ARENA_PROTOCOL_VERSION,
    requestId: value.requestId.trim(),
    actorId: value.actorId.trim(),
    command: value.command.trim(),
    parameters: value.parameters,
  };
}

export function parseArenaStatus(text: string): ArenaCommandStatus {
  const value: unknown = JSON.parse(text);
  if (!isRecord(value)) {
    throw new Error("Status must be a JSON object.");
  }

  if (!Number.isInteger(value.version)) {
    throw new Error("Status version must be an integer.");
  }

  if (typeof value.requestId !== "string" || value.requestId.trim() === "") {
    throw new Error("Status requestId must be a non-empty string.");
  }

  if (typeof value.status !== "string" || !STATUS_NAMES.has(value.status as ArenaCommandStatusName)) {
    throw new Error("Status contains an unknown state.");
  }

  if (value.errorCode !== null && typeof value.errorCode !== "string") {
    throw new Error("Status errorCode must be a string or null.");
  }

  if (typeof value.message !== "string") {
    throw new Error("Status message must be a string.");
  }

  return {
    version: value.version as number,
    requestId: value.requestId,
    status: value.status as ArenaCommandStatusName,
    errorCode: value.errorCode as string | null,
    message: value.message,
  };
}
