import assert from "node:assert/strict";
import test from "node:test";

import { loadConfig } from "../src/config.js";

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

test("always rejects public and wildcard hosts", () => {
  for (const host of ["8.8.8.8", "0.0.0.0"] as const) {
    assert.throws(() => loadConfig([], {
      ARENA_BRIDGE_HOST: host,
      ARENA_BRIDGE_ALLOW_PRIVATE_NETWORK_CONNECTIONS: "true",
    }));
  }
});
