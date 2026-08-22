import { ArenaBridgeServer } from "./arenaBridgeServer.js";
import { loadConfig } from "./config.js";
import { errorFields, log } from "./logger.js";
import { runSmokeMode, runStdinMode } from "./testMode.js";
import { TwitchAuthClient, TwitchReauthorizationRequiredError } from "./twitchAuth.js";
import { loadTwitchConfig } from "./twitchConfig.js";

async function checkTwitchAuthorization(): Promise<void> {
  try {
    const identity = await new TwitchAuthClient(loadTwitchConfig()).loadAuthorization();
    log("info", "twitch_authorization_ready", {
      authorizedUserId: identity.authorizedUserId,
      authorizedUserLogin: identity.authorizedUserLogin,
      channelUserId: identity.channelUserId,
      channelLogin: identity.channelLogin,
      scopes: identity.scopes,
      expiresIn: identity.expiresIn,
    });
  } catch (error) {
    if (error instanceof TwitchReauthorizationRequiredError) {
      log("warn", "twitch_reauthorization_required", { message: error.message });
      return;
    }

    log("warn", "twitch_authorization_check_failed", {
      message: "Twitch could not be validated. Manual and smoke modes remain available.",
      ...errorFields(error),
    });
  }
}

async function main(): Promise<void> {
  const config = loadConfig();
  await checkTwitchAuthorization();
  const server = new ArenaBridgeServer(config);
  let shutdownRequested = false;

  const shutdown = async (signal: NodeJS.Signals): Promise<void> => {
    if (shutdownRequested) {
      return;
    }

    shutdownRequested = true;
    log("info", "shutdown_requested", { signal });
    await server.stop();
  };

  process.once("SIGINT", () => void shutdown("SIGINT"));
  process.once("SIGTERM", () => void shutdown("SIGTERM"));

  await server.start();
  if (config.mode === "smoke") {
    await runSmokeMode(server, config.statusTimeoutMs);
    log("info", "smoke_mode_idle", {
      message: "The bridge remains active. Press Ctrl+C to stop.",
    });
    return;
  }

  await runStdinMode(server);
  if (!shutdownRequested) {
    await server.stop();
  }
}

main().catch((error: unknown) => {
  log("error", "bridge_failed", errorFields(error));
  process.exitCode = 1;
});
