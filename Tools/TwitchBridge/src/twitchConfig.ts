import { homedir } from "node:os";
import { resolve } from "node:path";

export const DEFAULT_TWITCH_CLIENT_ID = "egf7brtj87mffvnv4gka0m3ccad1pm";
export const TWITCH_READ_CHAT_SCOPE = "user:read:chat";

export interface TwitchConfig {
  clientId: string;
  scopes: readonly string[];
  channelLogin: string | undefined;
  tokenFilePath: string;
}

function normalizeChannelLogin(rawValue: string | undefined): string | undefined {
  const value = rawValue?.trim().toLowerCase().replace(/^#/, "");
  if (value === undefined || value === "") {
    return undefined;
  }

  if (!/^[a-z0-9_]{1,25}$/.test(value)) {
    throw new Error("TWITCH_CHANNEL_LOGIN must contain only letters, digits, or underscores.");
  }

  return value;
}

export function loadTwitchConfig(
  environment: NodeJS.ProcessEnv = process.env,
  homeDirectory: string = homedir(),
): TwitchConfig {
  return {
    clientId: environment.TWITCH_CLIENT_ID?.trim() || DEFAULT_TWITCH_CLIENT_ID,
    scopes: [TWITCH_READ_CHAT_SCOPE],
    channelLogin: normalizeChannelLogin(environment.TWITCH_CHANNEL_LOGIN),
    tokenFilePath: resolve(homeDirectory, ".arena-twitch-bridge", "twitch-auth.json"),
  };
}
