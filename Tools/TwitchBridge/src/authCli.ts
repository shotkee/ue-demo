import { loadTwitchConfig } from "./twitchConfig.js";
import {
  TwitchAuthClient,
  TwitchReauthorizationRequiredError,
  type TwitchAuthorizationIdentity,
} from "./twitchAuth.js";
import { errorFields, log } from "./logger.js";

function logIdentity(identity: TwitchAuthorizationIdentity): void {
  log("info", "twitch_authorization_ready", {
    authorizedUserId: identity.authorizedUserId,
    authorizedUserLogin: identity.authorizedUserLogin,
    channelUserId: identity.channelUserId,
    channelLogin: identity.channelLogin,
    channelDisplayName: identity.channelDisplayName,
    scopes: identity.scopes,
    expiresIn: identity.expiresIn,
  });
}

async function main(): Promise<void> {
  const command = process.argv[2] ?? "authorize";
  if (command !== "authorize" && command !== "status") {
    throw new Error("Usage: authCli [authorize|status]");
  }

  const client = new TwitchAuthClient(loadTwitchConfig());
  if (command === "status") {
    logIdentity(await client.loadAuthorization());
    return;
  }

  const identity = await client.authorize((prompt) => {
    console.log("\nOpen this Twitch page in a browser:");
    console.log(prompt.verificationUri);
    console.log(`\nEnter this code if Twitch asks for it: ${prompt.userCode}`);
    console.log("Approve read-only access to chat. Waiting for authorization...\n");
  });
  logIdentity(identity);
  console.log("\nTwitch authorization completed successfully.");
}

main().catch((error: unknown) => {
  if (error instanceof TwitchReauthorizationRequiredError) {
    log("error", "twitch_reauthorization_required", { message: error.message });
  } else {
    log("error", "twitch_authorization_failed", errorFields(error));
  }
  process.exitCode = 1;
});
