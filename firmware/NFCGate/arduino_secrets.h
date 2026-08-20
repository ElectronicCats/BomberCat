// Compile-time relay configuration fallback for the NFCGate sketch.
//
// These values are used ONLY when there is no configuration persisted in flash
// (ConfigStore). Once the control CLI (docs/NFCGATE_PLAN.md Fase 6) writes a config,
// the persisted values win and this file is ignored. Filling it in lets the
// READER role be tested before the CLI exists.

// --- WiFi ---
#define SECRET_SSID ""
#define SECRET_PASS ""

// --- nfcgate-server ---
// Host/IP running nfcgate-server (see tools/testserver/) and its TCP port.
#define RELAY_SERVER ""
#define RELAY_PORT 5566

// --- Session ---
// 1..255. Both relay peers (reader + card) must use the SAME session byte.
#define RELAY_SESSION 42

// --- Role ---
// 0 = READER (reads a physical card), 1 = CARD/HCE (emulates to a terminal).
// Both roles are implemented end to end (Fase 4 + Fase 5).
#define RELAY_ROLE 0

// --- Autostart ---
// 1 = start the relay on boot from the persisted/secrets config (standalone).
// 0 = boot into the SerialControl REPL only and wait for the CLI's `run`.
//
// IMPORTANT: autostart runs a BLOCKING WiFi/TCP bring-up (connectWiFi ->
// engine.begin) inside setup(), before loop() starts. If a non-empty SSID is
// configured (persisted in flash or set below), that bring-up blocks setup()
// for up to ~20 s — or hangs on the NINA SPI handshake — and the SerialControl
// REPL never starts servicing USB, so the CLI's `ping` handshake times out and
// `bombercat run` reports "no BomberCat found". Keep this 0 for CLI-driven use;
// only set 1 for a truly standalone device you will not talk to over USB.
#define RELAY_AUTOSTART 0
