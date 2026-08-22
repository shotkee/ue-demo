export type BridgeMode = "stdin" | "smoke" | "twitch";

export interface BridgeConfig {
  host: string;
  port: number;
  allowPrivateNetworkConnections: boolean;
  queueLimit: number;
  queueTtlMs: number;
  statusTimeoutMs: number;
  mode: BridgeMode;
}

const LOOPBACK_HOSTS = new Set(["127.0.0.1", "localhost", "::1"]);

function readBoolean(name: string, rawValue: string | undefined, defaultValue: boolean): boolean {
  if (rawValue === undefined || rawValue.trim() === "") {
    return defaultValue;
  }

  const normalizedValue = rawValue.trim().toLowerCase();
  if (normalizedValue === "true" || normalizedValue === "1") {
    return true;
  }

  if (normalizedValue === "false" || normalizedValue === "0") {
    return false;
  }

  throw new Error(`${name} must be true or false.`);
}

function isPrivateIpv4Address(host: string): boolean {
  const parts = host.split(".");
  if (parts.length !== 4) {
    return false;
  }

  const octets = parts.map((part) => {
    if (!/^\d{1,3}$/.test(part)) {
      return Number.NaN;
    }
    return Number(part);
  });
  if (octets.some((octet) => !Number.isInteger(octet) || octet < 0 || octet > 255)) {
    return false;
  }

  const firstOctet = octets[0];
  const secondOctet = octets[1];
  return firstOctet === 10
    || (firstOctet === 172 && secondOctet !== undefined && secondOctet >= 16 && secondOctet <= 31)
    || (firstOctet === 192 && secondOctet === 168);
}

function readInteger(
  name: string,
  rawValue: string | undefined,
  defaultValue: number,
  minimum: number,
  maximum: number,
): number {
  if (rawValue === undefined || rawValue.trim() === "") {
    return defaultValue;
  }

  const value = Number(rawValue);
  if (!Number.isSafeInteger(value) || value < minimum || value > maximum) {
    throw new Error(`${name} must be an integer between ${minimum} and ${maximum}.`);
  }

  return value;
}

function readMode(args: readonly string[], environment: NodeJS.ProcessEnv): BridgeMode {
  const explicitModes = args.filter(
    (argument) => argument === "--stdin" || argument === "--smoke" || argument === "--twitch",
  );
  if (explicitModes.length > 1) {
    throw new Error("Use only one bridge mode: --stdin, --smoke, or --twitch.");
  }

  if (explicitModes[0] === "--twitch") {
    return "twitch";
  }

  if (explicitModes[0] === "--smoke") {
    return "smoke";
  }

  if (explicitModes[0] === "--stdin") {
    return "stdin";
  }

  const configuredMode = environment.ARENA_BRIDGE_MODE?.trim().toLowerCase() ?? "stdin";
  if (configuredMode !== "stdin" && configuredMode !== "smoke" && configuredMode !== "twitch") {
    throw new Error("ARENA_BRIDGE_MODE must be 'stdin', 'smoke', or 'twitch'.");
  }

  return configuredMode;
}

export function loadConfig(
  args: readonly string[] = process.argv.slice(2),
  environment: NodeJS.ProcessEnv = process.env,
): BridgeConfig {
  const unknownArguments = args.filter(
    (argument) => argument !== "--stdin" && argument !== "--smoke" && argument !== "--twitch",
  );
  if (unknownArguments.length > 0) {
    throw new Error(`Unknown argument: ${unknownArguments[0]}`);
  }

  const host = environment.ARENA_BRIDGE_HOST?.trim().toLowerCase() || "127.0.0.1";
  const allowPrivateNetworkConnections = readBoolean(
    "ARENA_BRIDGE_ALLOW_PRIVATE_NETWORK_CONNECTIONS",
    environment.ARENA_BRIDGE_ALLOW_PRIVATE_NETWORK_CONNECTIONS,
    false,
  );
  if (!LOOPBACK_HOSTS.has(host)
    && !(allowPrivateNetworkConnections && isPrivateIpv4Address(host))) {
    throw new Error(
      "ARENA_BRIDGE_HOST must be loopback, or a private IPv4 address with "
        + "ARENA_BRIDGE_ALLOW_PRIVATE_NETWORK_CONNECTIONS=true.",
    );
  }

  return {
    host,
    allowPrivateNetworkConnections,
    port: readInteger("ARENA_BRIDGE_PORT", environment.ARENA_BRIDGE_PORT, 8080, 1, 65535),
    queueLimit: readInteger(
      "ARENA_BRIDGE_QUEUE_LIMIT",
      environment.ARENA_BRIDGE_QUEUE_LIMIT,
      100,
      1,
      10_000,
    ),
    queueTtlMs: readInteger(
      "ARENA_BRIDGE_QUEUE_TTL_MS",
      environment.ARENA_BRIDGE_QUEUE_TTL_MS,
      15_000,
      100,
      300_000,
    ),
    statusTimeoutMs: readInteger(
      "ARENA_BRIDGE_STATUS_TIMEOUT_MS",
      environment.ARENA_BRIDGE_STATUS_TIMEOUT_MS,
      30_000,
      1_000,
      300_000,
    ),
    mode: readMode(args, environment),
  };
}
