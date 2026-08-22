import assert from "node:assert/strict";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";

import { TwitchAuthClient, TwitchReauthorizationRequiredError } from "../src/twitchAuth.js";
import type { TwitchConfig } from "../src/twitchConfig.js";

function jsonResponse(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}

function createConfig(tokenFilePath: string): TwitchConfig {
  return {
    clientId: "public-client-id",
    scopes: ["user:read:chat"],
    channelLogin: undefined,
    tokenFilePath,
  };
}

test("device authorization stores tokens and resolves both Twitch user IDs", async () => {
  const directory = await mkdtemp(join(tmpdir(), "arena-twitch-auth-"));
  const tokenFilePath = join(directory, ".local", "twitch-auth.json");
  const requests: Array<{ url: string; body: string }> = [];
  let tokenPollCount = 0;

  const fetchFn = (async (input: string | URL | Request, init?: RequestInit): Promise<Response> => {
    const url = String(input);
    requests.push({ url, body: String(init?.body ?? "") });
    if (url.endsWith("/device")) {
      return jsonResponse({
        device_code: "device-code",
        user_code: "ABCDEFGH",
        verification_uri: "https://www.twitch.tv/activate",
        expires_in: 600,
        interval: 1,
      });
    }
    if (url.endsWith("/token")) {
      tokenPollCount += 1;
      if (tokenPollCount === 1) {
        return jsonResponse({ status: 400, message: "authorization_pending" }, 400);
      }
      return jsonResponse({
        access_token: "access-token",
        refresh_token: "refresh-token",
        token_type: "bearer",
        scope: ["user:read:chat"],
      });
    }
    if (url.endsWith("/validate")) {
      return jsonResponse({
        client_id: "public-client-id",
        login: "alice",
        scopes: ["user:read:chat"],
        user_id: "1001",
        expires_in: 14_000,
      });
    }
    if (url.startsWith("https://api.twitch.tv/helix/users")) {
      return jsonResponse({
        data: [{ id: "1001", login: "alice", display_name: "Alice" }],
      });
    }
    throw new Error(`Unexpected request: ${url}`);
  }) as typeof fetch;

  try {
    let shownCode = "";
    const identity = await new TwitchAuthClient(createConfig(tokenFilePath), {
      fetchFn,
      sleep: async () => undefined,
      now: () => 1_700_000_000_000,
    }).authorize((prompt) => {
      shownCode = prompt.userCode;
    });

    assert.equal(shownCode, "ABCDEFGH");
    assert.equal(identity.authorizedUserId, "1001");
    assert.equal(identity.channelUserId, "1001");
    assert.equal(identity.channelLogin, "alice");
    const stored = JSON.parse(await readFile(tokenFilePath, "utf8")) as Record<string, unknown>;
    assert.equal(stored.accessToken, "access-token");
    assert.equal(stored.refreshToken, "refresh-token");
    assert.ok(requests[0]?.body.includes("scopes=user%3Aread%3Achat"));
    assert.ok(requests.every((request) => !request.body.includes("client_secret")));
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});

test("an invalid access token is refreshed and the rotated refresh token is saved", async () => {
  const directory = await mkdtemp(join(tmpdir(), "arena-twitch-refresh-"));
  const tokenFilePath = join(directory, "twitch-auth.json");
  await writeFile(tokenFilePath, JSON.stringify({
    version: 1,
    clientId: "public-client-id",
    accessToken: "expired-access-token",
    refreshToken: "old-refresh-token",
    tokenType: "bearer",
    scopes: ["user:read:chat"],
    obtainedAt: "2023-01-01T00:00:00.000Z",
  }));
  let validationCount = 0;

  const fetchFn = (async (input: string | URL | Request): Promise<Response> => {
    const url = String(input);
    if (url.endsWith("/validate")) {
      validationCount += 1;
      if (validationCount === 1) {
        return jsonResponse({ status: 401, message: "invalid access token" }, 401);
      }
      return jsonResponse({
        client_id: "public-client-id",
        login: "alice",
        scopes: ["user:read:chat"],
        user_id: "1001",
        expires_in: 14_000,
      });
    }
    if (url.endsWith("/token")) {
      return jsonResponse({
        access_token: "new-access-token",
        refresh_token: "new-refresh-token",
        token_type: "bearer",
        scope: ["user:read:chat"],
      });
    }
    if (url.startsWith("https://api.twitch.tv/helix/users")) {
      return jsonResponse({
        data: [{ id: "1001", login: "alice", display_name: "Alice" }],
      });
    }
    throw new Error(`Unexpected request: ${url}`);
  }) as typeof fetch;

  try {
    const identity = await new TwitchAuthClient(createConfig(tokenFilePath), { fetchFn }).loadAuthorization();
    assert.equal(identity.accessToken, "new-access-token");
    const stored = JSON.parse(await readFile(tokenFilePath, "utf8")) as Record<string, unknown>;
    assert.equal(stored.refreshToken, "new-refresh-token");
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});

test("a missing local token file gives a clear reauthorization instruction", async () => {
  const directory = await mkdtemp(join(tmpdir(), "arena-twitch-missing-"));
  try {
    const client = new TwitchAuthClient(createConfig(join(directory, "missing.json")));
    await assert.rejects(
      () => client.loadAuthorization(),
      (error: unknown) => error instanceof TwitchReauthorizationRequiredError
        && /npm run auth/.test(error.message),
    );
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});
