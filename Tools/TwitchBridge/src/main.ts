import { ArenaBridgeServer } from "./arenaBridgeServer.js";
import { loadConfig } from "./config.js";
import { errorFields, log } from "./logger.js";
import { runSmokeMode, runStdinMode } from "./testMode.js";

async function main(): Promise<void> {
  const config = loadConfig();
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
