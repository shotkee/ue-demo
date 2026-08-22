import { randomUUID } from "node:crypto";
import { chmod, mkdir, readFile, rename, unlink, writeFile } from "node:fs/promises";
import { dirname } from "node:path";
import { setTimeout as delay } from "node:timers/promises";

import type { TwitchConfig } from "./twitchConfig.js";

const TWITCH_DEVICE_URL = "https://id.twitch.tv/oauth2/device";
const TWITCH_TOKEN_URL = "https://id.twitch.tv/oauth2/token";
const TWITCH_VALIDATE_URL = "https://id.twitch.tv/oauth2/validate";
const TWITCH_USERS_URL = "https://api.twitch.tv/helix/users";
const TOKEN_FILE_VERSION = 1;
const REQUEST_TIMEOUT_MS = 15_000;
const REFRESH_BEFORE_EXPIRY_SECONDS = 300;

interface StoredTwitchTokens {
  version: typeof TOKEN_FILE_VERSION;
  clientId: string;
  accessToken: string;
  refreshToken: string;
  tokenType: string;
  scopes: string[];
  obtainedAt: string;
}

interface DeviceAuthorizationResponse {
  deviceCode: string;
  userCode: string;
  verificationUri: string;
  expiresIn: number;
  interval: number;
}

interface TokenResponse {
  accessToken: string;
  refreshToken: string;
  tokenType: string;
  scopes: string[];
}

interface ValidationResponse {
  clientId: string;
  login: string;
  scopes: string[];
  userId: string;
  expiresIn: number;
}

interface TwitchUser {
  id: string;
  login: string;
  displayName: string;
}

export interface TwitchAuthorizationIdentity {
  accessToken: string;
  authorizedUserId: string;
  authorizedUserLogin: string;
  channelUserId: string;
  channelLogin: string;
  channelDisplayName: string;
  scopes: readonly string[];
  expiresIn: number;
}

export interface TwitchDeviceAuthorizationPrompt {
  verificationUri: string;
  userCode: string;
  expiresIn: number;
}

interface TwitchAuthDependencies {
  fetchFn?: typeof fetch;
  sleep?: (milliseconds: number) => Promise<void>;
  now?: () => number;
}

export class TwitchReauthorizationRequiredError extends Error {
  public constructor(message: string) {
    super(`${message} Run 'npm run auth' to authorize Twitch again.`);
    this.name = "TwitchReauthorizationRequiredError";
  }
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function requiredString(value: unknown, fieldName: string): string {
  if (typeof value !== "string" || value.trim() === "") {
    throw new Error(`Twitch response is missing '${fieldName}'.`);
  }
  return value;
}

function requiredInteger(value: unknown, fieldName: string): number {
  if (!Number.isInteger(value) || (value as number) < 0) {
    throw new Error(`Twitch response contains an invalid '${fieldName}'.`);
  }
  return value as number;
}

function stringArray(value: unknown, fieldName: string): string[] {
  if (!Array.isArray(value) || value.some((item) => typeof item !== "string")) {
    throw new Error(`Twitch response contains an invalid '${fieldName}'.`);
  }
  return [...value] as string[];
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

function apiError(response: Response, body: unknown): Error {
  const message = isRecord(body) && typeof body.message === "string"
    ? body.message
    : `HTTP ${response.status}`;
  return new Error(`Twitch request failed: ${message}`);
}

function parseDeviceAuthorization(value: unknown): DeviceAuthorizationResponse {
  if (!isRecord(value)) {
    throw new Error("Twitch device authorization response must be an object.");
  }

  return {
    deviceCode: requiredString(value.device_code, "device_code"),
    userCode: requiredString(value.user_code, "user_code"),
    verificationUri: requiredString(value.verification_uri, "verification_uri"),
    expiresIn: requiredInteger(value.expires_in, "expires_in"),
    interval: Math.max(1, requiredInteger(value.interval, "interval")),
  };
}

function parseTokenResponse(value: unknown): TokenResponse {
  if (!isRecord(value)) {
    throw new Error("Twitch token response must be an object.");
  }

  return {
    accessToken: requiredString(value.access_token, "access_token"),
    refreshToken: requiredString(value.refresh_token, "refresh_token"),
    tokenType: requiredString(value.token_type, "token_type"),
    scopes: stringArray(value.scope, "scope"),
  };
}

function parseValidationResponse(value: unknown): ValidationResponse {
  if (!isRecord(value)) {
    throw new Error("Twitch validation response must be an object.");
  }

  return {
    clientId: requiredString(value.client_id, "client_id"),
    login: requiredString(value.login, "login"),
    scopes: stringArray(value.scopes, "scopes"),
    userId: requiredString(value.user_id, "user_id"),
    expiresIn: requiredInteger(value.expires_in, "expires_in"),
  };
}

function parseStoredTokens(value: unknown): StoredTwitchTokens {
  if (!isRecord(value) || value.version !== TOKEN_FILE_VERSION) {
    throw new TwitchReauthorizationRequiredError("The local Twitch token file is invalid.");
  }

  return {
    version: TOKEN_FILE_VERSION,
    clientId: requiredString(value.clientId, "clientId"),
    accessToken: requiredString(value.accessToken, "accessToken"),
    refreshToken: requiredString(value.refreshToken, "refreshToken"),
    tokenType: requiredString(value.tokenType, "tokenType"),
    scopes: stringArray(value.scopes, "scopes"),
    obtainedAt: requiredString(value.obtainedAt, "obtainedAt"),
  };
}

async function readStoredTokens(path: string): Promise<StoredTwitchTokens> {
  try {
    return parseStoredTokens(JSON.parse(await readFile(path, "utf8")) as unknown);
  } catch (error) {
    if (isRecord(error) && error.code === "ENOENT") {
      throw new TwitchReauthorizationRequiredError("Twitch has not been authorized on this computer.");
    }
    if (error instanceof TwitchReauthorizationRequiredError) {
      throw error;
    }
    throw new TwitchReauthorizationRequiredError("The local Twitch token file cannot be read.");
  }
}

async function writeStoredTokens(path: string, tokens: StoredTwitchTokens): Promise<void> {
  const directory = dirname(path);
  await mkdir(directory, { recursive: true, mode: 0o700 });
  const temporaryPath = `${path}.${process.pid}.${randomUUID()}.tmp`;

  try {
    await writeFile(temporaryPath, `${JSON.stringify(tokens, null, 2)}\n`, {
      encoding: "utf8",
      flag: "wx",
      mode: 0o600,
    });
    await rename(temporaryPath, path);
    await chmod(path, 0o600).catch(() => undefined);
  } catch (error) {
    await unlink(temporaryPath).catch(() => undefined);
    throw error;
  }
}

function storedTokens(clientId: string, value: TokenResponse, now: number): StoredTwitchTokens {
  return {
    version: TOKEN_FILE_VERSION,
    clientId,
    accessToken: value.accessToken,
    refreshToken: value.refreshToken,
    tokenType: value.tokenType,
    scopes: value.scopes,
    obtainedAt: new Date(now).toISOString(),
  };
}

export class TwitchAuthClient {
  private readonly fetchFn: typeof fetch;
  private readonly sleep: (milliseconds: number) => Promise<void>;
  private readonly now: () => number;

  public constructor(
    private readonly config: TwitchConfig,
    dependencies: TwitchAuthDependencies = {},
  ) {
    this.fetchFn = dependencies.fetchFn ?? fetch;
    this.sleep = dependencies.sleep ?? ((milliseconds) => delay(milliseconds));
    this.now = dependencies.now ?? Date.now;
  }

  public async authorize(
    showPrompt: (prompt: TwitchDeviceAuthorizationPrompt) => void,
  ): Promise<TwitchAuthorizationIdentity> {
    const deviceAuthorization = await this.beginDeviceAuthorization();
    showPrompt({
      verificationUri: deviceAuthorization.verificationUri,
      userCode: deviceAuthorization.userCode,
      expiresIn: deviceAuthorization.expiresIn,
    });

    const tokenResponse = await this.pollForToken(deviceAuthorization);
    const tokens = storedTokens(this.config.clientId, tokenResponse, this.now());
    await writeStoredTokens(this.config.tokenFilePath, tokens);
    return this.identityFor(tokens);
  }

  public async loadAuthorization(): Promise<TwitchAuthorizationIdentity> {
    let tokens = await readStoredTokens(this.config.tokenFilePath);
    if (tokens.clientId !== this.config.clientId) {
      throw new TwitchReauthorizationRequiredError("The stored token belongs to a different Twitch Client ID.");
    }

    let validation = await this.validate(tokens.accessToken);
    if (validation === undefined || validation.expiresIn <= REFRESH_BEFORE_EXPIRY_SECONDS) {
      tokens = await this.refresh(tokens.refreshToken);
      validation = await this.validate(tokens.accessToken);
    }

    if (validation === undefined) {
      throw new TwitchReauthorizationRequiredError("Twitch rejected the refreshed access token.");
    }

    return this.identityFor(tokens, validation);
  }

  private async beginDeviceAuthorization(): Promise<DeviceAuthorizationResponse> {
    const response = await this.postForm(TWITCH_DEVICE_URL, {
      client_id: this.config.clientId,
      scopes: this.config.scopes.join(" "),
    });
    const body = await responseBody(response);
    if (!response.ok) {
      throw apiError(response, body);
    }
    return parseDeviceAuthorization(body);
  }

  private async pollForToken(device: DeviceAuthorizationResponse): Promise<TokenResponse> {
    const deadline = this.now() + device.expiresIn * 1_000;
    let intervalMilliseconds = device.interval * 1_000;

    while (this.now() < deadline) {
      const response = await this.postForm(TWITCH_TOKEN_URL, {
        client_id: this.config.clientId,
        scopes: this.config.scopes.join(" "),
        device_code: device.deviceCode,
        grant_type: "urn:ietf:params:oauth:grant-type:device_code",
      });
      const body = await responseBody(response);
      if (response.ok) {
        return parseTokenResponse(body);
      }

      const message = isRecord(body) && typeof body.message === "string"
        ? body.message.toLowerCase()
        : "";
      if (message === "authorization_pending") {
        await this.sleep(intervalMilliseconds);
        continue;
      }
      if (message === "slow_down") {
        intervalMilliseconds += 5_000;
        await this.sleep(intervalMilliseconds);
        continue;
      }
      if (message.includes("denied") || message.includes("expired") || message.includes("invalid device")) {
        throw new TwitchReauthorizationRequiredError(`Device authorization failed: ${message}.`);
      }
      throw apiError(response, body);
    }

    throw new TwitchReauthorizationRequiredError("The Twitch device authorization code expired.");
  }

  private async refresh(refreshToken: string): Promise<StoredTwitchTokens> {
    const response = await this.postForm(TWITCH_TOKEN_URL, {
      grant_type: "refresh_token",
      refresh_token: refreshToken,
      client_id: this.config.clientId,
    });
    const body = await responseBody(response);
    if (!response.ok) {
      throw new TwitchReauthorizationRequiredError("The Twitch refresh token is no longer valid.");
    }

    const tokens = storedTokens(this.config.clientId, parseTokenResponse(body), this.now());
    await writeStoredTokens(this.config.tokenFilePath, tokens);
    return tokens;
  }

  private async validate(accessToken: string): Promise<ValidationResponse | undefined> {
    const response = await this.fetchFn(TWITCH_VALIDATE_URL, {
      headers: { Authorization: `OAuth ${accessToken}` },
      signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS),
    });
    const body = await responseBody(response);
    if (response.status === 401) {
      return undefined;
    }
    if (!response.ok) {
      throw apiError(response, body);
    }

    const validation = parseValidationResponse(body);
    if (validation.clientId !== this.config.clientId) {
      throw new TwitchReauthorizationRequiredError("Twitch validated the token for a different Client ID.");
    }
    for (const scope of this.config.scopes) {
      if (!validation.scopes.includes(scope)) {
        throw new TwitchReauthorizationRequiredError(`The Twitch token is missing the '${scope}' scope.`);
      }
    }
    return validation;
  }

  private async identityFor(
    tokens: StoredTwitchTokens,
    existingValidation?: ValidationResponse,
  ): Promise<TwitchAuthorizationIdentity> {
    const validation = existingValidation ?? await this.validate(tokens.accessToken);
    if (validation === undefined) {
      throw new TwitchReauthorizationRequiredError("Twitch rejected the access token.");
    }

    const channel = await this.resolveChannel(tokens.accessToken, this.config.channelLogin ?? validation.login);
    return {
      accessToken: tokens.accessToken,
      authorizedUserId: validation.userId,
      authorizedUserLogin: validation.login,
      channelUserId: channel.id,
      channelLogin: channel.login,
      channelDisplayName: channel.displayName,
      scopes: validation.scopes,
      expiresIn: validation.expiresIn,
    };
  }

  private async resolveChannel(accessToken: string, channelLogin: string): Promise<TwitchUser> {
    const url = new URL(TWITCH_USERS_URL);
    url.searchParams.set("login", channelLogin);
    const response = await this.fetchFn(url, {
      headers: {
        Authorization: `Bearer ${accessToken}`,
        "Client-Id": this.config.clientId,
      },
      signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS),
    });
    const body = await responseBody(response);
    if (!response.ok) {
      throw apiError(response, body);
    }
    if (!isRecord(body) || !Array.isArray(body.data) || !isRecord(body.data[0])) {
      throw new Error(`Twitch channel '${channelLogin}' was not found.`);
    }

    const user = body.data[0];
    return {
      id: requiredString(user.id, "data[0].id"),
      login: requiredString(user.login, "data[0].login"),
      displayName: requiredString(user.display_name, "data[0].display_name"),
    };
  }

  private postForm(url: string, fields: Record<string, string>): Promise<Response> {
    return this.fetchFn(url, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: new URLSearchParams(fields),
      signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS),
    });
  }
}
