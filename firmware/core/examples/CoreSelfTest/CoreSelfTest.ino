/**
 * BomberCatCore - CoreSelfTest
 *
 * Compile/smoke sketch for the Fase 2 core skeleton. It does not perform a real
 * relay; it just exercises every public symbol so the library builds cleanly on
 * the target (Arduino Mbed OS RP2040 core) and demonstrates basic usage.
 *
 * Board: Arduino Mbed OS RP2040 (BomberCat / RP2040).
 *
 * Distributed as-is; no warranty is given.
 */
#include <BomberCatCore.h>
#include <WiFiNINA.h>

NfcController nfc;
ConfigStore config;
RelayConfig cfg;

// NfcGateLink over a WiFiClient (Fase 3). Not connected here — this sketch is a
// compile/symbol check, not a live relay.
WiFiClient wifiClient;
NfcGateLink link(wifiClient);

// RelayEngine (Fase 4/5) + SerialControl (Fase 6). Exercised for symbols only.
RelayEngine engine(nfc, link, cfg);
SerialControl control(Serial, config, cfg, engine, "selftest");

void setup() {
  Serial.begin(9600);
  Log::begin(Serial, LogLevel::Debug);
  LOG_INFO("BomberCatCore self-test");

  // --- ConfigStore ---
  if (config.begin()) {
    if (!config.load(cfg)) {
      LOG_WARN("No saved config, using defaults");
      cfg = ConfigStore::defaults();
    }
    Log::out().print("session=");
    Log::out().print(cfg.session);
    Log::out().print(" role=");
    Log::out().println(cfg.roleEnum() == RelayRole::READER ? "reader" : "card");
  } else {
    LOG_ERROR("ConfigStore init failed");
  }

  // --- HexUtils / Log ---
  uint8_t ppse[] = {0x00, 0xA4, 0x04, 0x00, 0x0E};
  Log::hex(LogLevel::Debug, "SELECT PPSE:", ppse, sizeof(ppse));
  Serial.println(HexUtils::toString(ppse, sizeof(ppse)));

  // --- NfcGateCodec (offline round-trip) ---
  // Encode a frame, then decode its ServerData payload back and check the APDU
  // survives. No network involved; exercises the codec on-device.
  uint8_t frame[NFCGATE_MAX_FRAME];
  NfcData tx;
  if (NfcGateCodec::makeNfcData(tx, NfcSource::READER, NfcType::INITIAL, ppse,
                                sizeof(ppse), 0)) {
    size_t n = NfcGateCodec::encodeFrame(cfg.session, NfcOpcode::PSH, tx, frame,
                                         sizeof(frame));
    // Skip the 5-byte [len][session] header to decode the payload directly.
    ServerData sd;
    NfcData rx;
    if (n > 5 && NfcGateCodec::decodeServerData(frame + 5, n - 5, sd, rx)) {
      Log::hex(LogLevel::Debug, "codec round-trip APDU:", rx.data.bytes,
               rx.data.size);
    } else {
      LOG_ERROR("codec round-trip failed");
    }
  }
  link.setSession(cfg.session);  // reference NfcGateLink symbols

  // --- SerialControl (Fase 6) --- announce readiness; no callbacks wired here.
  SerialControl::Callbacks cb;  // all null: run/stop/reboot return -ERR/no-op
  control.setCallbacks(cb);
  control.begin();

  // --- APDU capture tap (Fase 8) --- reference the symbols.
  engine.setCapture(&Serial);
  LOG_INFO(engine.capturing() ? "capture on" : "capture off");
  engine.setCapture(nullptr);

  // --- NfcController ---
  bool ok = (cfg.roleEnum() == RelayRole::READER) ? nfc.beginReaderMode()
                                                   : nfc.beginEmulationMode();
  if (!ok) {
    LOG_ERROR("NFC bring-up failed");
    return;
  }
  LOG_INFO("NFC ready");
}

void loop() {
  control.poll();  // service the control REPL (Fase 6)

  uint8_t buf[256];
  uint8_t len = 0;

  if (cfg.roleEnum() == RelayRole::READER) {
    // Reader: (a peer would supply the terminal command; here we just probe.)
    if (nfc.waitForTag(500)) {
      LOG_DEBUG("Tag in field");
    }
  } else {
    // Card/HCE: forward any command frame from the terminal.
    if (nfc.cardReceive(buf, &len)) {
      Log::hex(LogLevel::Debug, "C->:", buf, len);
    }
  }
  delay(50);
}