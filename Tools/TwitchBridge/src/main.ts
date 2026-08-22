import { ArenaBridgeServer } from "./arenaBridgeServer.js";
import { loadConfig } from "./config.js";
import { errorFields, log } from "./logger.js";
import { runSmokeMode, runStdinMode } from "./testMode.js";
import {
  TwitchAuthClient,
  TwitchReauthorizationRequiredError,
  type TwitchAuthorizationIdentity,
} from "./twitchAuth.js";
import { loadTwitchConfig } from "./twitchConfig.js";
import { TwitchChatCommandProcessor } from "./twitchChatCommandProcessor.js";
import { TwitchEventSubClient } from "./twitchEventSub.js";

const TOKEN_VALIDATION_INTERVAL_MS = 60 * 60 * 1_000;
const TOKEN_VALIDATION_CACHE_MS = 55 * 60 * 1_000;

function logAuthorization(identity: TwitchAuthorizationIdentity): void {
  log("info", "twitch_authorization_ready", {
    authorizedUserId: identity.authorizedUserId,
    authorizedUserLogin: identity.authorizedUserLogin,
    channelUserId: identity.channelUserId,
    channelLogin: identity.channelLogin,
    scopes: identity.scopes,
    expiresIn: identity.expiresIn,
  });
}

async function loadAuthorization(
  client: TwitchAuthClient,
  required: boolean,
): Promise<TwitchAuthorizationIdentity | undefined> {
  try {
    const identity = await client.loadAuthorization();
    logAuthorization(identity);
    return identity;
  } catch (error) {
    if (error instanceof TwitchReauthorizationRequiredError) {
      log(required ? "error" : "warn", "twitch_reauthorization_required", { message: error.message });
      if (required) {
        throw error;
      }
      return undefined;
    }

    log(required ? "error" : "warn", "twitch_authorization_check_failed", {
      message: required
        ? "Twitch mode cannot start until authorization is validated."
        : "Twitch could not be validated. Manual and smoke modes remain available.",
      ...errorFields(error),
    });
    if (required) {
      throw error;
    }
    return undefined;
  }
}

async function main(): Promise<void> {
  const config = loadConfig();
  const twitchConfig = loadTwitchConfig();
  const authClient = new TwitchAuthClient(twitchConfig);
  let authorization = await loadAuthorization(authClient, config.mode === "twitch");
  let authorizationValidatedAt = Date.now();
  const server = new ArenaBridgeServer(config);
  let eventSubClient: TwitchEventSubClient | undefined;
  let chatCommandProcessor: TwitchChatCommandProcessor | undefined;
  let tokenValidationTimer: NodeJS.Timeout | undefined;
  let shutdownRequested = false;

  const refreshAuthorization = async (force: boolean): Promise<TwitchAuthorizationIdentity> => {
    if (!force
      && authorization !== undefined
      && Date.now() - authorizationValidatedAt < TOKEN_VALIDATION_CACHE_MS) {
      return authorization;
    }

    authorization = await authClient.loadAuthorization();
    authorizationValidatedAt = Date.now();
    logAuthorization(authorization);
    return authorization;
  };

  const shutdown = async (reason: string): Promise<void> => {
    if (shutdownRequested) {
      return;
    }

    shutdownRequested = true;
    log("info", "shutdown_requested", { reason });
    if (tokenValidationTimer !== undefined) {
      clearInterval(tokenValidationTimer);
      tokenValidationTimer = undefined;
    }
    chatCommandProcessor?.dispose();
    chatCommandProcessor = undefined;
    await eventSubClient?.stop();
    await server.stop();
  };

  process.once("SIGINT", () => void shutdown("SIGINT"));
  process.once("SIGTERM", () => void shutdown("SIGTERM"));

  try {
    await server.start();
    if (config.mode === "smoke") {
      await runSmokeMode(server, config.statusTimeoutMs);
      log("info", "smoke_mode_idle", {
        message: "The bridge remains active. Press Ctrl+C to stop.",
      });
      return;
    }

    if (config.mode === "twitch") {
      if (authorization === undefined) {
        throw new TwitchReauthorizationRequiredError("Twitch authorization is missing.");
      }

      eventSubClient = new TwitchEventSubClient(twitchConfig.clientId, () => refreshAuthorization(false));
      chatCommandProcessor = new TwitchChatCommandProcessor(server);
      eventSubClient.onCommand((message) => chatCommandProcessor?.handle(message));
      eventSubClient.onRevocation((revocation) => {
        process.exitCode = 1;
        void shutdown(`twitch_subscription_revoked:${revocation.status}`);
      });
      await eventSubClient.start();
      tokenValidationTimer = setInterval(() => {
        void refreshAuthorization(true).catch(async (error: unknown) => {
          if (error instanceof TwitchReauthorizationRequiredError) {
            log("error", "twitch_reauthorization_required", { message: error.message });
            process.exitCode = 1;
            await shutdown("twitch_reauthorization_required");
            return;
          }

          log("warn", "twitch_periodic_validation_failed", {
            message: "The existing EventSub connection remains active; token validation will be retried.",
            ...errorFields(error),
          });
        });
      }, TOKEN_VALIDATION_INTERVAL_MS);
      tokenValidationTimer.unref();
      log("info", "twitch_mode_ready", {
        channelUserId: authorization.channelUserId,
        channelLogin: authorization.channelLogin,
        message: "Listening for chat messages that start with '!'. Press Ctrl+C to stop.",
      });
      return;
    }

    await runStdinMode(server);
    if (!shutdownRequested) {
      await shutdown("stdin_closed");
    }
  } catch (error) {
    await shutdown("startup_failed");
    throw error;
  }
}

main().catch((error: unknown) => {
  log("error", "bridge_failed", errorFields(error));
  process.exitCode = 1;
});
