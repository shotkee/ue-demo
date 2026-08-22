import assert from "node:assert/strict";
import test from "node:test";

import { loadConfig } from "../src/config.js";
import {
  DEFAULT_TWITCH_CLIENT_ID,
  TWITCH_READ_CHAT_SCOPE,
  loadTwitchConfig,
} from "../src/twitchConfig.js";

test("uses loopback networking by default", () => {
  const config = loadConfig([], {});
  assert.equal(config.host, "127.0.0.1");
  assert.equal(config.allowPrivateNetworkConnections, false);
});

test("rejects a private host unless LAN access is explicitly enabled", () => {
  assert.throws(
    () => loadConfig([], { ARENA_BRIDGE_HOST: "192.168.88.100" }),
    /ARENA_BRIDGE_ALLOW_PRIVATE_NETWORK_CONNECTIONS=true/,
  );
});

test("accepts a private host when LAN access is explicitly enabled", () => {
  const config = loadConfig([], {
    ARENA_BRIDGE_HOST: "192.168.88.100",
    ARENA_BRIDGE_ALLOW_PRIVATE_NETWORK_CONNECTIONS: "true",
  });

  assert.equal(config.host, "192.168.88.100");
  assert.equal(config.allowPrivateNetworkConnections, true);
});

test("supports Twitch chat mode from either the command line or environment", () => {
  assert.equal(loadConfig(["--twitch"], {}).mode, "twitch");
  assert.equal(loadConfig([], { ARENA_BRIDGE_MODE: "twitch" }).mode, "twitch");
  assert.throws(() => loadConfig(["--twitch", "--smoke"], {}), /only one bridge mode/);
});

test("always rejects public and wildcard hosts", () => {
  for (const host of ["8.8.8.8", "0.0.0.0"] as const) {
    assert.throws(() => loadConfig([], {
      ARENA_BRIDGE_HOST: host,
      ARENA_BRIDGE_ALLOW_PRIVATE_NETWORK_CONNECTIONS: "true",
    }));
  }
});

test("uses the shared public Twitch application without storing a secret", () => {
  const config = loadTwitchConfig({}, "/tmp/arena-twitch-bridge-test");

  assert.equal(config.clientId, DEFAULT_TWITCH_CLIENT_ID);
  assert.deepEqual(config.scopes, [TWITCH_READ_CHAT_SCOPE]);
  assert.equal(config.channelLogin, undefined);
  assert.equal(config.tokenFilePath, "/tmp/arena-twitch-bridge-test/.arena-twitch-bridge/twitch-auth.json");
});

test("normalizes an explicitly configured Twitch channel", () => {
  const config = loadTwitchConfig({ TWITCH_CHANNEL_LOGIN: "#ShotKee" }, "/tmp/test");
  assert.equal(config.channelLogin, "shotkee");
});
