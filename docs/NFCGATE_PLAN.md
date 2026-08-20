# Plan de implementación — Compatibilidad con NFCGate (relay)

> Estado: **documento vivo** · Creado: 2026-08-12 · Última actualización: 2026-08-17
> Alcance elegido: **relay** (lector ⇄ emulador) · protocolo nfcgate **en firmware vía WiFi (ESP32)** ·
> `core/` **de nueva creación** (migración de host/client relay queda para después).
>
> **Resumen del estado actual (2026-08-17):** Fases 1–6 **completadas** (protobuf embebido, librería
> `core/`, `NfcGateLink`, ambos roles del relay, plano de control firmware `SerialControl` + CLI de
> Python). Fase 7 **completada (compat validada)**: Camino A (dos BomberCat vía `nfcgate-server`) y
> Camino B (app NFCGate Android, **ambas variantes B1 y B2**) **validados en hardware** (§15).
> **Latencia por transacción: bajada de ~12–15 s a ~4.5 s** (Fases D–I en `firmware/LATENCIA_OPTIMIZACION.md`;
> el piso restante es arquitectónico, §17). **Fase 8 (captura pcap /
> Wireshark, la pieza del diseño original, decisión D5/§6): COMPLETADA** — tap `:apdu` en firmware + `bombercat capture` en el host, verificada contra
> tshark (§16 el diseño, §18 el estado) **y validada en HW real** (transacción EMV del Camino A
> capturada a `CapturaWireshark.pcapng`).

---

## 1. Contexto y arquitectura

BomberCat = RP2040 + **PN7150** (NFC, lib `Electroniccats_PN7150`) + **ESP32-WROOM** (WiFiNINA) + TC4424.

El modo relay de NFCGate empareja dos endpoints —uno en modo **lector** (lee una tarjeta física) y otro
en **emulación/HCE** (se presenta como tarjeta a un terminal)— unidos por un servidor `nfcgate-server`
que reenvía mensajes **protobuf con prefijo de longitud (4 bytes big-endian) sobre TCP**.

Esto mapea 1:1 con los sketches actuales:
- `firmware/host_Relay_NFC` → lado **lector** (`setReaderWriterMode` + `cardModeSend/Receive`).
- `firmware/client_Relay_NFC` → lado **emulación** (`setEmulationMode`).

### Decisión de arquitectura
- El **firmware** habla nfcgate directamente: WiFi (WiFiNINA) → TCP → `nfcgate-server`, con protobuf
  embebido (**nanopb**). El BomberCat es autónomo (no necesita PC para el relay).
- El **CLI de Python** (`tools/`) es **plano de control** sobre USB-serial: configura credenciales WiFi,
  host/puerto del servidor, id de sesión y rol; arranca/detiene el relay; monitorea estado y APDUs.
- El enlace serial **NO** transporta APDUs (esa ruta va por WiFi), por lo que basta un protocolo de
  control de texto por líneas (se reutiliza el patrón `SerialCommand` ya existente).

```
[ Tarjeta física ] --RF--> [ BomberCat READER ] --WiFi/TCP+protobuf--> [ nfcgate-server ]
                                                                              |
[ Terminal/PoS  ] <--RF--- [ BomberCat CARD/HCE ] <--WiFi/TCP+protobuf-------+
        ^
        |  USB-serial (control): CLI Python configura y monitorea cada BomberCat
```

---

## 2. `firmware/core/` — biblioteca reutilizable (fundación)

Crear `firmware/core/` como **librería Arduino local** (`library.properties` + `src/`) para poder
`#include` desde cualquier sketch y, a futuro, migrar host/client encima.

Módulos (`src/`):
| Módulo | Responsabilidad |
|---|---|
| `NfcController` | Envoltura de `Electroniccats_PN7150`: `beginReaderMode()`, `beginEmulationMode()`, `exchangeReader(in,out)`, `exchangeCard(cmd,resp)`, reset/discovery. Absorbe `resetMode()` y el flujo `cardModeSend/Receive` duplicado en host/client. |
| `NfcGateLink` | WiFiNINA `WiFiClient` + framing por longitud + codificación/decodificación **nanopb** de `Wrapper`/`NFCData`/`Status`/`Anticol`. Empieza en **TCP plano**; TLS (`WiFiSSLClient`) como fase posterior. |
| `RelayEngine` | Une `NfcController` + `NfcGateLink`: bucle de rol lector y bucle de rol card. Manejo de sesión (join/leave), keepalive y errores. |
| `ConfigStore` | Envoltura de `TDBStore`/FlashIAP: SSID/pass, servidor, puerto, sesión, rol. (Refactor del bloque duplicado en host/client). |
| `SerialControl` | Parser de comandos de texto para el CLI (set/get params, start/stop, status). |
| `HexUtils` / `Log` | `getHexRepresentation`, `printData`, logging con nivel de debug. |

Diseñar las interfaces de `ConfigStore` y `NfcController` pensando en que host/client relay puedan
migrar encima sin cambios de comportamiento (paso posterior, fuera de este alcance).

---

## 3. Sketch `firmware/NFCGate/` (encima de `core/`)

Un solo sketch, rol seleccionable (`reader` | `card`) por comando serial / config persistida.

- **setup**: init `NfcController`, cargar `ConfigStore`, conectar WiFi, conectar a `nfcgate-server`,
  hacer *join* de sesión, enviar `Status`/`Anticol` inicial.
- **loop rol READER**: descubre tarjeta → por cada `NFCData(source=CARD_TERMINAL)` que llega por TCP,
  `exchangeReader()` → responde `NFCData(source=CARD)`.
- **loop rol CARD/HCE**: `beginEmulationMode()` → por cada APDU del terminal, enviarlo por TCP y esperar
  la respuesta para inyectarla con `cardModeSend()`.
- **control**: `SerialControl` atiende al CLI mientras no hay sesión activa (o en canal aparte).

Dependencias: `WiFiNINA`, `nanopb`, `Electroniccats_PN7150`, `TDBStore`/`FlashIAPBlockDevice`.

---

## 4. Protobuf embebido (nanopb)

1. **Fijar** la versión de `nfcgate-server` a soportar y **vendorizar** sus `.proto`
   (`metaMessage.proto`, `c2c.proto`, `c2s.proto`) en `firmware/core/proto/`.
2. Añadir un paso de generación con **nanopb** (`*.pb.c`/`*.pb.h`) + archivo `.options`
   con tamaños máximos (p. ej. `NFCData.data` ≥ 512 B) para acotar RAM.
3. Documentar el comando de generación (script `tools/gen_proto.sh` o Makefile) para que sea reproducible.

> A confirmar contra el `.proto`/código del servidor: opcodes de *join/leave* de sesión, orden de bytes
> del prefijo de longitud y campos exactos de `NFCData`/`Anticol`.

---

## 5. CLI de Python en `tools/` (plano de control)

> La limpieza que este plan pedía (esqueleto portado de *catnip* con símbolos inexistentes:
> `__version__`, `Wireshark`, `..utils.output`) **ya está hecha**: `modules/core/cli.py` es un
> grupo `click` real con `rich`, `__version__` sale de `modules/utils/_version.py` y
> `modules/utils/output.py` existe. Lo que queda pendiente es el **control serial del dispositivo**.

### Estructura actual (real)
El CLI se organiza por **paquetes de módulo**; cada feature expone su propio grupo `click` en un
`cli.py` y se registra en el grupo raíz con `cli.add_command(...)`.
```
tools/
  bombercat.py                 # entrypoint: inyecta vendor/ en sys.path y llama a modules.core.cli:main_cli
  requirements.txt             # click, rich (+ deps transitivas fijadas: markdown-it-py, mdurl, Pygments)
  gen_proto.sh                 # (Fase 1) regeneración nanopb
  testserver/                  # (Fase 1) fixtures dev-only del nfcgate-server
  modules/
    __init__.py
    core/
      cli.py                   # grupo raíz `bombercat` + cabecera ASCII (rich); registra proto + testserver
      bombercat.py             # (WIP) control del dispositivo por serial — hoy stub
      usb_connection.py        # (WIP) descubrimiento de puerto/serial (pyserial; VID/PID por definir)
    proto/
      cli.py                   # `bombercat proto gen` → envuelve tools/gen_proto.sh
    testserver/
      cli.py                   # `bombercat testserver run|smoke` → envuelve run.sh + relay_smoketest.py
    utils/
      output.py                # consola rich + helpers print_* (STYLES, print_success/error/…)
      _version.py              # __version__ (lee tools/VERSION o metadata del paquete)
```

### Comandos ya implementados (dev tooling)
- `bombercat proto gen` — regenera `firmware/core/src/proto/*.pb.{c,h}` (envuelve `gen_proto.sh`).
- `bombercat testserver run [-p PORT]` — levanta el `nfcgate-server` local en Docker.
- `bombercat testserver smoke [HOST] [PORT]` — corre el relay smoke test contra el servidor.

### Comandos del plano de control del dispositivo — implementados (Fase 6)
Cada feature vive en su paquete (`modules/device/`, `modules/nfcgate/`) con su grupo `click`,
registrado con `cli.add_command`. Se apoyan en `core/usb_connection.py` (puerto/handshake) y en
`DeviceLink` (`core/bombercat.py`), cliente del protocolo de control de texto por líneas
(`SerialControl` del firmware). `pyserial==3.5` ya está en `requirements.txt`.
- `bombercat device list` / `device info` — enumerar puertos, handshake, versión de fw.
- `bombercat identify` — parpadea el LED para casar un `-d <id>` con una placa física.
- `bombercat config wifi --ssid --pass [--save/--no-save]`
- `bombercat config nfcgate --server <host[:port]> --session <id> --role reader|card`
- `bombercat config show` — vuelca la config persistida del dispositivo.
- `bombercat run` / `stop` / `status` — ciclo de vida del relay + estado (link/peer/relayed).
- `bombercat monitor` — stream del serial en vivo (logs + volcado hex de APDUs del `RelayEngine`).
- `bombercat completion install` — tab-completion de shell (bash/zsh/fish).

(Opcional, aún **no** implementado) `bombercat flash` para subir firmware vía `arduino-cli`/`bossac`.
El comando `capture` / escritura pcapng del diseño v0.1 **tampoco** está — es la Fase 8 (§16).

---

## 6. Pruebas y documentación

- **Servidor**: levantar `nfcgate-server` local (Docker) para las pruebas.
- **Prueba de compatibilidad fuerte**: BomberCat en rol `reader` (o `card`) contra la **app Android
  NFCGate** como el otro endpoint → valida compatibilidad real del protocolo.
- **Prueba end-to-end propia**: dos BomberCat (reader + card) vía `nfcgate-server`.
- **Mock**: pruebas de `NfcGateLink` con un socket TCP de prueba sin RF.
- **Docs**: `firmware/NFCGate/README.md` (cableado, generación nanopb, flasheo) y `tools/README.md`
  (uso del CLI) + tabla de mapeo de mensajes nfcgate ↔ estados del firmware.

---

## 7. Fases y orden sugerido

1. **Groundwork protobuf**: vendorizar `.proto`, configurar nanopb, levantar `nfcgate-server` local.
2. **`core/` esqueleto**: `library.properties`, `NfcController`, `ConfigStore`, `HexUtils/Log`.
3. **`NfcGateLink`**: WiFi + TCP plano + framing + encode/decode `Wrapper` (nanopb), probado contra el servidor.
4. **Sketch NFCGate rol READER** + `RelayEngine`: relay de respuestas de tarjeta.
5. **Rol CARD/HCE**: emulación + inyección de respuestas.
6. **CLI Python de control**: limpiar `cli.py`, subcomandos config/run/status, `requirements.txt`.
7. **E2E + docs**: contra NFCGate Android y contra dos BomberCat.
8. *(Posterior)* TLS (`WiFiSSLClient`); migrar host/client relay a `core/`.

---

## 8. Riesgos a validar temprano

- **TLS/certificados** en WiFiNINA para el modo TLS de nfcgate → empezar en TCP plano.
- **Tamaño de mensajes** protobuf (APDU grandes) → acotar bien con nanopb `.options` (RAM del RP2040: 264 KB).
- **Latencia** WiFi + PN7150 dentro de los timeouts EMV (FWT/WTX) → puede requerir manejo de WTX.
- **Fidelidad de anticolisión/UID** que exige NFCGate para el emparejamiento.
- **Detalles de protocolo** (opcodes de sesión, byte order del prefijo) → confirmar contra el upstream fijado.

---

## 9. Estado — Fase 1 completada (2026-08-12)

**Groundwork protobuf + servidor local: hecho y verificado.**

### Corrección importante al plan (protocolo real del upstream fijado)
El upstream fijado (`nfcgate v2.6.1` / `protocol@804fa9a` / `server@4d32cc1`) usa
un protocolo **más simple** que el descrito en §4 y en el `README`/`test.py` del
upstream (esos son de una versión antigua ya obsoleta):
- **No existen** `metaMessage.proto`, `Wrapper`, `Session` (create/join),
  `Anticol` ni `Status`. Solo hay **`NFCData`** (c2c) y **`ServerData`** (c2s).
- **Framing asimétrico**:
  `cliente→servidor = [4B len BE][1B sesión][payload]`,
  `servidor→cliente = [4B len BE][payload]`.
- La **sesión** es ese byte del header (sin handshake). `payload = ServerData{`
  `opcode, data = NFCData{...} serializado }`.
- Detalle completo y tabla de mapeo mensaje↔rol en
  [`firmware/core/proto/UPSTREAM.md`](../firmware/core/proto/UPSTREAM.md).

Consecuencia: §3/§4 (encode/decode `Wrapper`/`Status`/`Anticol`) se simplifican a
solo `ServerData`+`NFCData`, y el "join de sesión" es fijar el byte de sesión.

### Entregables
- `firmware/core/proto/` — `c2c.proto`, `c2s.proto` vendorizados + `c2c.options`,
  `c2s.options` (nanopb: `NFCData.data≤512`, `ServerData.data≤600`) + `UPSTREAM.md`.
- `firmware/core/src/proto/` — `c2c.pb.{c,h}`, `c2s.pb.{c,h}` generados (committeados;
  el build de firmware **no** necesita Python/protoc). Solo requieren la lib Arduino
  **Nanopb** en tiempo de compilación (runtime `pb_encode/pb_decode/pb_common`).
- `tools/gen_proto.sh` — regeneración reproducible (venv con `nanopb==0.4.9.1` +
  `grpcio-tools==1.68.1`). Verificado desde cero.
- `tools/testserver/` — `fetch_server.sh` (clona `ElectronicCats/nfcgate-server@fc9103d` bajo
  demanda en `./server`, **gitignored**; no vendorizado ni submódulo, es fixture
  dev-only), `Dockerfile` + `run.sh` (server local en `:5566`, plugin `log`),
  `relay_smoketest.py` (relay sin RF), `requirements.txt`, `README.md`.

### Verificación
- `tools/gen_proto.sh` regenera los 4 archivos byte-idénticos, con los tamaños de
  `.options` aplicados (`NFCData_size=530`, `ServerData_size=605`).
- Reproducible desde checkout limpio: `fetch_server.sh` → `run.sh` →
  `relay_smoketest.py`. Intercambio bidireccional reader↔card con blobs idénticos
  → **RELAY SMOKE TEST PASSED**; el plugin `log` decodifica los APDUs
  (`OP_PSH R:`/`OP_PSH C:`).

### Siguiente (Fase 2)
`firmware/core/` esqueleto: `library.properties`, `NfcController`, `ConfigStore`,
`HexUtils/Log` — reutilizando `NFCData`/`ServerData` ya generados.

---

## 10. Estado — Fase 2 completada (2026-08-12)

**`firmware/core/` esqueleto: librería Arduino local creada.**

### Entregables
- `firmware/core/library.properties` — librería `BomberCatCore` v0.1.0,
  `architectures=mbed_rp2040`, `depends=Electronic Cats PN7150`.
- **Runtime nanopb vendorizado** en `firmware/core/src/` (`pb.h`,
  `pb_common/pb_encode/pb_decode` .{h,c}, v0.4.9.1, zlib; `NANOPB_LICENSE.txt`):
  nanopb **no** está publicado en el Library Manager de Arduino (solo aparece
  como *nombre de dependencia* irresoluble), así que se vendoriza plano en
  `src/` para que `proto/*.pb.h` resuelva `#include <pb.h>`.
- `firmware/core/src/`:
  - `Log.{h,cpp}` — logging con nivel (`LogLevel` NONE/ERROR/WARN/INFO/DEBUG),
    macros `LOG_*`, sink nulo antes de `begin()`. Reemplaza los `if(debug)`.
  - `HexUtils.{h,cpp}` — `toString()` / `print()` (refactor de
    `getHexRepresentation`/`printData`; `"null"` si len==0).
  - `NfcController.{h,cpp}` — envoltura de `Electroniccats_PN7150`:
    `beginReaderMode()`, `beginEmulationMode()`, `reset()`, `waitForTag()`,
    `readerTransceive()`, `cardReceive()`/`cardSend()`, `raw()`. Los fallos de
    bring-up **retornan false** (antes `while(1)`); convención normalizada
    "true = éxito".
  - `ConfigStore.{h,cpp}` — config persistente (`RelayConfig`: ssid/pass/server/
    port/session/role; `RelayRole`) sobre `TDBStore`/`FlashIAP`. Clave
    `"relaycfg"`; `load()` cae a `defaults()` si no existe o el layout cambió.
  - `FlashIAPLimits.h` — `mbed::getFlashIAPLimits()` vendorizado (para que
    host/client dejen su copia local al migrar).
  - `BomberCatCore.h` — header paraguas (incluye módulos + proto).
- `firmware/core/examples/CoreSelfTest/CoreSelfTest.ino` — sketch de humo que
  ejercita todos los símbolos públicos (compile-check, no hace relay real).
- `firmware/core/README.md` — módulos, dependencias, cómo compilar el ejemplo,
  y las 2 desviaciones deliberadas respecto a los sketches legacy.

### Notas de diseño / fidelidad
- `NfcController` replica las secuencias `cardModeSend/Receive` de host/client,
  con 2 diferencias intencionales: (1) bring-up devuelve `false` en vez de
  colgar; (2) `readerTransceive()` hace send + **un** receive con timeout (no
  reproduce el doble `cardModeReceive` de `seekTrack2()`). `raw()` permite la
  secuencia legacy exacta durante la migración.
- **Protocolo NFCGate ≠ RF**: `NfcController` opera sobre APDUs en crudo
  (`uint8_t*`), desacoplado de `NFCData`/`ServerData`. La codificación protobuf
  vive en `NfcGateLink` (Fase 3).

### Verificación — compila (2026-08-12)
- `CoreSelfTest` **compila limpio** con `arduino-cli` contra
  `electroniccats:mbed_rp2040:bombercat` (core 2.0.0) + `Electronic Cats PN7150`
  3.1.1: ~112 KB flash (5%) / 45 KB RAM (17%). FQBN:
  `electroniccats:mbed_rp2040:bombercat`.
- Bug atrapado al compilar: la lib PN7150 hace `#define ERROR 1`, que mutaba
  `LogLevel::ERROR` → `LogLevel::1`. Corregido usando PascalCase en el enum
  (`LogLevel::None/Error/Warn/Info/Debug`); los macros son MAYÚSCULAS y no
  colisionan con nombres capitalizados.

### Siguiente (Fase 3)
`NfcGateLink`: WiFi (WiFiNINA) + TCP plano + framing asimétrico
(`[4B len BE][1B sesión][payload]` c→s) + encode/decode `ServerData`+`NFCData`
(nanopb), probado contra `nfcgate-server` local (`tools/testserver/`).

---

## 11. Estado — Fase 3 completada (2026-08-12)

**`NfcGateLink` (WiFi/TCP + framing + protobuf): hecho, códec verificado
contra el servidor real sin RF.**

### Decisión de diseño: códec separado del transporte
Se partió la Fase 3 en dos módulos en vez de uno, para poder **testear el
protocolo en host** (sin placa ni RF):
- `NfcGateCodec.{h,cpp}` — **sin `<Arduino.h>`**: solo bytes ↔ protobuf. Aliases
  cortos (`NfcData`, `ServerData`, `NfcSource`, `NfcType`, `NfcOpcode`) sobre los
  nombres nanopb larguísimos. Funciones `makeNfcData()`, `encodeFrame()`
  (arma el frame c→s completo `[4B len BE][1B sesión][payload]`),
  `decodeServerData()` (decodifica un payload s→c ya des-enmarcado).
- `NfcGateLink.{h,cpp}` — transporte sobre `Client&` de Arduino (no `WiFiClient`
  concreto, para desacoplar de WiFiNINA y permitir un doble de prueba):
  `connect(host,port,session)`, `send()`, y **`poll()` no bloqueante** (máquina
  de estados: header de 4B big-endian → payload; devuelve `1`/`0`/`-1`).
  Buffers estáticos dimensionados con `NFCGATE_MAX_PAYLOAD`/`NFCGATE_MAX_FRAME`.
  La **asociación WiFi** (`WiFi.begin`) queda en el sketch/RelayEngine, no aquí.

### Entregables
- `firmware/core/src/NfcGateCodec.{h,cpp}`, `NfcGateLink.{h,cpp}`.
- `firmware/core/src/BomberCatCore.h` — incluye `NfcGateLink.h` (y por transitividad
  el códec + los tipos proto).
- `firmware/core/examples/CoreSelfTest/CoreSelfTest.ino` — extendido: round-trip
  del códec on-device + instancia `NfcGateLink` sobre un `WiFiClient`.
- `tools/testserver/codec_hosttest/` — `hosttest.cpp` + `build_and_run.sh`:
  compila el **códec real del firmware** + nanopb con g++ y hace el loopback
  reader↔card contra `nfcgate-server` por socket POSIX.
- `firmware/core/README.md` — módulos Fase 3 + cómo correr el host test.

### Verificación
- **Códec contra el servidor real (sin RF):** `build_and_run.sh` compila limpio
  (`g++ -std=c++11 -Wall -Wextra`) y pasa: `reader→card` (SELECT PPSE) y
  `card→reader` (FCI) con APDUs byte-idénticos → **CODEC HOST TEST PASSED**. El
  server confirma `joined session 42` y el plugin `log` decodifica `OP_PSH R:` /
  `OP_PSH C:` con los hex correctos.
- **Compile-check del firmware (`arduino-cli`): OK** — `CoreSelfTest` compila
  limpio contra `electroniccats:mbed_rp2040:bombercat` (core 2.0.0) + PN7150
  3.1.1 + **WiFiNINA 2.1.1** → **126 KB flash (6%) / 46 KB RAM (17%)** (subió
  desde 112 KB/45 KB en Fase 2, por `NfcGateLink` + `WiFiClient`). Requiere
  instalar WiFiNINA (`arduino-cli lib install WiFiNINA`); las advertencias de
  arquitectura de WiFiNINA/Arduino_SpiNINA son esperadas (los sketches
  host/client legacy la usan igual sobre el módulo NINA del BomberCat).
  ```sh
  arduino-cli compile -b electroniccats:mbed_rp2040:bombercat \
    --library firmware/core firmware/core/examples/CoreSelfTest
  ```

### Siguiente (Fase 4)
Sketch `firmware/NFCGate/` rol **READER** + `RelayEngine`: unir `NfcController`
+ `NfcGateLink` (descubrir tarjeta → `NFCData(source=CARD_TERMINAL)` que llega
por TCP → `readerTransceive()` → responder `NFCData(source=CARD)`).

---

## 12. Estado — Fase 4 completada (2026-08-12)

**Sketch `firmware/NFCGate/` rol READER + `RelayEngine`: hecho; handshake y
wire-format verificados contra el servidor real sin RF.**

### Corrección al plan (protocolo de sesión real de la app)
La app NFCGate (`NetworkManager.java`) **sí** tiene handshake de sesión, pero es
mínimo y va sobre `ServerData.opcode` (no los `Session`/`Wrapper` obsoletos):
- Al conectar envía **`OP_SYN`** (sin datos); responde **`OP_ACK`** al recibir un
  SYN del peer; `OP_FIN` al salir; `OP_PSH` lleva el `NFCData`.
- Enviar cualquier frame es lo que **registra** al cliente en la sesión del
  servidor (`server.py:75-80`); un oyente puro nunca recibe. Por eso el rol
  READER manda `OP_SYN` al arrancar (anuncio + registro en uno).
- `data_source` describe el **contenido**: `READER` = comando (terminal→tarjeta),
  `CARD` = respuesta (tarjeta→terminal). El rol READER **consume** frames
  `READER` y **produce** `CARD` (coincide con §3, cuyo "source=CARD_TERMINAL" era
  redacción imprecisa: no existe tal enum, es un `NFCData` tag `READER`).

### Entregables
- `firmware/core/src/RelayEngine.{h,cpp}` — une `NfcController` + `NfcGateLink`:
  `begin()` (bring-up NFC según rol + `connect()` + `OP_SYN`), `loop()` no
  bloqueante (drena frames del servidor: SYN→ACK, ACK/FIN, y en rol READER
  PSH→`readerTransceive()`→`send(source=CARD)`), `stop()` (`OP_FIN`). Sin
  dependencia de WiFiNINA (el WiFi vive en el sketch, igual que `NfcGateLink`).
  Activa la tarjeta de forma perezosa (`waitForTag`) y la re-activa si un
  transceive falla. CARD/HCE queda como stub documentado (Fase 5).
- `firmware/core/src/NfcGateCodec.{h,cpp}` — nuevo `encodeControlFrame()`
  (`ServerData{opcode}` **sin** `NFCData`, como `sendServer(op,null)` de la app;
  rechaza `OP_PSH` porque serializaría a 0 bytes = desconexión en el servidor).
- `firmware/core/src/NfcGateLink.{h,cpp}` — nuevo `sendControl(op)` (SYN/ACK/FIN).
- `firmware/core/src/BomberCatCore.h` — incluye `RelayEngine.h`.
- `firmware/NFCGate/NFCGate.ino` + `arduino_secrets.h` — sketch rol-seleccionable:
  carga `ConfigStore` (fallback a `arduino_secrets.h` hasta el CLI de Fase 6),
  asocia WiFi, arranca `RelayEngine`, reintenta en `State::Error`.
- `firmware/NFCGate/README.md` — cableado, config, build/flash, prueba sin HW.
- `tools/testserver/codec_hosttest/hosttest.cpp` — extendido con round-trip del
  handshake `OP_SYN`/`OP_ACK` (valida `encodeControlFrame` en el cable) antes del
  loopback PSH.

### Verificación
- **Compila (`arduino-cli`):** `firmware/NFCGate` limpio contra
  `electroniccats:mbed_rp2040:bombercat` (core 2.0.0) + PN7150 3.1.1 +
  WiFiNINA 2.1.1 → **129 KB flash (6%) / 46.9 KB RAM (17%)** (sube desde 126 KB
  de Fase 3 por `RelayEngine` + sketch). `CoreSelfTest` sigue compilando (126 KB)
  tras el cambio del header paraguas.
- **Handshake + wire-format contra el servidor real (sin RF):** el host test
  pasa `OP_SYN`(reader recibe SYN del peer) → `OP_ACK`(card recibe ACK) →
  `OP_PSH R:`(SELECT PPSE) → `OP_PSH C:`(FCI) con APDUs byte-idénticos →
  **CODEC HOST TEST PASSED**. El plugin `log` del servidor decodifica
  `OP_SYN`/`OP_ACK`/`OP_PSH R:`/`OP_PSH C:` con los hex correctos.

### Siguiente (Fase 5)
Rol **CARD/HCE** en `RelayEngine`: `beginEmulationMode()` → por cada APDU del
terminal (`cardReceive`) enviar `send(source=READER)`, y por cada `NFCData`
`CARD` que llega por TCP inyectarlo con `cardSend()`. La estructura (switch de
rol en `handleFrame`, stub avisando) ya está en su sitio.

---

## 13. Estado — Fase 5 completada (2026-08-13)

**Rol CARD/HCE en `RelayEngine`: emulación + inyección de respuestas — hecho;
compila limpio contra la placa.**

Con esto **ambos roles del relay están implementados de punta a punta**; el
firmware NFCGate es funcionalmente completo (falta E2E con HW real y el CLI de
control, fases 6–7).

### Diseño (simétrico al rol READER de Fase 4)
El rol CARD es el espejo del READER: consume el terminal por RF y produce
comandos `READER`-tagged; consume las respuestas `CARD`-tagged por TCP y las
inyecta al terminal. Ambos usan la misma máquina cooperativa no bloqueante:
- **`loop()`** drena primero las tramas del servidor (`handleFrame`) y, solo en
  rol CARD, además llama a `cardPollTerminal()` (el rol READER es 100 %
  frame-driven; el CARD sí necesita sondear activamente el lado RF).
- **`cardPollTerminal()`** — request/response estricto con flag
  `_awaitingResponse`: solo pide un comando nuevo al terminal (`cardReceive`)
  cuando la respuesta anterior ya se inyectó (el terminal EMV tampoco emite el
  siguiente comando hasta ser respondido → mantiene lock-step y evita reenviar
  tramas fuera de orden). Al recibir un comando lo reenvía con
  `send(source=READER)`.
- **`handleFrame` rama CARD (`OP_PSH`)** — una PSH `CARD`-tagged es la respuesta
  de la tarjeta física relayada por el peer READER → `cardHandleResponse()` la
  copia a un scratch local (bytes `const`) y la inyecta con `cardSend()`, limpia
  `_awaitingResponse` e incrementa `relayedCount()`. Una PSH `READER`-tagged se
  ignora (es lo que el propio rol CARD produce).

### Entregables
- `firmware/core/src/RelayEngine.{h,cpp}` — nuevos `cardPollTerminal()` y
  `cardHandleResponse()`; `loop()` sondea el terminal en rol CARD; rama CARD de
  `handleFrame` implementada (ya no es stub). Nuevo estado `_awaitingResponse`
  (reemplaza `_cardStubWarned`). Constante `READER_MAX_APDU` → `RELAY_MAX_APDU`
  (255 B, compartida por ambos caminos; `NfcController` usa longitudes
  `uint8_t`, igual que los sketches legacy).
- `firmware/NFCGate/NFCGate.ino` — cabecera actualizada: ambos roles completos.
- Fidelidad al `client_Relay_NFC` legacy: el flujo send-respuesta →
  recibe-siguiente-comando de `visamsd()` se reordena a un request/response
  limpio (recibe-comando → reenvía → espera-respuesta → inyecta), equivalente
  para una transacción ISO-DEP/EMV que es estrictamente síncrona. La emulación
  no necesita `waitForTag` (el `startDiscovery` de `beginEmulationMode()` basta,
  como en el legacy).

### Verificación — compila (2026-08-13)
- `firmware/NFCGate` compila limpio (`arduino-cli` 1.4.1) contra
  `electroniccats:mbed_rp2040:bombercat` (core 2.0.0) + PN7150 3.1.1 +
  WiFiNINA 2.1.1 → **129535 B flash (6%) / 46888 B RAM (17%)**, sin cambio
  medible respecto a Fase 4 (el rol CARD reutiliza buffers/rutas del READER).
  `CoreSelfTest` sigue compilando (126370 B).
  ```sh
  arduino-cli compile -b electroniccats:mbed_rp2040:bombercat \
    --library firmware/core firmware/NFCGate
  ```
- Sin nueva superficie de códec: el rol CARD emite `send(source=READER)` y
  consume PSH `CARD`-tagged, ambas ya validadas en el cable contra el
  `nfcgate-server` real por el host test de Fase 3/4 (son las mismas tramas,
  vistas desde el otro extremo). La ruta RF del `RelayEngine` no es testeable
  fuera de la placa sin RF (nota de §6/§9).

### Siguiente (Fase 6)
CLI de Python de control (`tools/`): `modules/device/` + `modules/nfcgate/` con
sus grupos `click`, apoyados en `core/usb_connection.py` y un `SerialControl` en
el firmware para config (wifi/nfcgate), `run`/`status`/`monitor`. Añadir
`pyserial` a `requirements.txt`.

---

## 14. Estado — Fase 6 completada (2026-08-13)

**Plano de control (firmware `SerialControl` + CLI de Python): hecho; firmware
compila, protocolo del CLI verificado con un dispositivo simulado (pty).**

Con esto el flujo completo está en pie: configurar el BomberCat por USB-serial,
arrancar/parar el relay y monitorearlo, sin que ningún APDU viaje por el serial
(los APDUs van por WiFi/TCP). Falta solo la Fase 7 (E2E con HW real).

### Protocolo de control (texto, una línea por comando, `\n`)
`SerialControl` corre un REPL mínimo sobre `Serial`. Las **respuestas llevan un
marcador inicial** para que el CLI las distinga del log humano (que comparte el
mismo `Serial`):
- `:<clave> <valor>` — un dato (campos de `info`/`status`).
- `+OK [texto]` — éxito (termina la respuesta).
- `-ERR <texto>` — fallo (termina la respuesta).
- Cualquier línea sin `:`/`+`/`-` es ruido de log y el CLI la ignora. Cada
  comando produce exactamente una línea terminadora `+OK`/`-ERR`.

Comandos: `ping` (handshake → `+OK bombercat`), `info`, `get <clave>`,
`set <clave> <valor…>` (claves `ssid pass server port session role`; el valor es
el resto de la línea → SSID/pass con espacios OK; `port`/`session` numéricos,
`role`=`reader|card`), `save`, `load`, `clear`, `run`, `stop`, `status`,
`reboot`. Las acciones que tocan WiFi/MCU (`run`/`stop`/`reboot`) las inyecta el
sketch como **callbacks**, así `core/` sigue libre de WiFiNINA.

### Entregables — firmware
- `firmware/core/src/SerialControl.{h,cpp}` — el REPL. Parser por líneas no
  bloqueante (`poll()`), buffer de 160 B con manejo de desbordamiento, helpers
  `ok()/err()/kv()`. Depende solo de `ConfigStore` + `RelayEngine` (para
  estado/contadores) + `Stream`.
- `firmware/core/src/BomberCatCore.h` — incluye `SerialControl.h`.
- `firmware/NFCGate/NFCGate.ino` — reescrito: **baud 9600 → 115200**, arranca en
  modo control (`control.begin()` imprime `+OK bombercat ready`), callbacks
  `runRelay`/`stopRelay`/`rebootMcu` (`NVIC_SystemReset`), **retry no bloqueante**
  en `State::Error` (sin `delay(3000)`, para no congelar el REPL) y `control.poll()`
  en cada `loop()`. `#define BOMBERCAT_FW_VERSION "0.7.0"`.
- `firmware/NFCGate/arduino_secrets.h` — nuevo `RELAY_AUTOSTART` (1=arranca
  standalone desde config; con SSID vacío hace no-op y espera al CLI).
- `firmware/core/examples/CoreSelfTest/CoreSelfTest.ino` — ejercita
  `SerialControl` (símbolos).

### Entregables — CLI Python (`tools/`)
- `modules/core/usb_connection.py` — reescrito (era stub roto): enumeración de
  puertos (`list_ports_info`, filtra UART internas/BT), `open_serial`,
  `PortInfo`. VID/PID del BomberCat sin asignar → descubrimiento por handshake.
- `modules/core/bombercat.py` — reescrito (era stub roto): clase **`DeviceLink`**
  (cliente del protocolo: `command()` con filtrado de marcadores, `ping/info/
  status/set/save/run/stop/stream`), `Response`, y `discover_devices()` /
  `resolve_port()` (autodetección: 1 placa → esa; 0/varias → error pidiendo
  `--port`).
- `modules/device/cli.py` — grupo `device`: `list` (tabla de puertos, marca los
  que responden al handshake) e `info`.
- `modules/nfcgate/cli.py` — grupo `config` (`wifi --ssid --pass`,
  `nfcgate --server host[:port] --session --role`, `show`; `--save/--no-save`) +
  comandos sueltos `run`, `stop`, `status`, `monitor` (stream del serial en vivo,
  resalta los volcados de APDU del `RelayEngine`).
- `modules/core/cli.py` — registra los grupos nuevos con `cli.add_command`.
- `requirements.txt` — `pyserial==3.5`.
- `tools/tests/serialctl_hosttest.py` — test dev-only del parser `DeviceLink`
  contra un dispositivo simulado por **pty** (sin HW ni servidor).
- `tools/README.md` — uso del CLI.

### Verificación
- **Firmware compila** (`arduino-cli` 1.4.1, mismo toolchain que fases previas):
  `firmware/NFCGate` → **131889 B flash (6%) / 47092 B RAM (17%)** (sube ~2.3 KB
  flash desde Fase 5 por el REPL). `CoreSelfTest` → 129018 B.
- **Protocolo del CLI** (`tools/tests/serialctl_hosttest.py`): un fake device por
  pty emula `SerialControl` (con ruido de log intercalado) y `DeviceLink` pasa
  handshake, `info`/`status` (recolección de `:datos`), filtrado de log,
  `set` con espacios y propagación de `-ERR` → **SERIALCTL PROTOCOL TEST PASSED**.
- **CLI** importa y expone todos los grupos (`bombercat --help` lista
  `device/config/run/stop/status/monitor` + `proto/testserver`); fallos sin HW
  son limpios (mensaje + exit 1, sin traceback).
- Sin HW real, la ruta serial extremo-a-extremo (placa ↔ CLI) queda para Fase 7.

### Siguiente (Fase 7)
E2E: dos BomberCat (reader+card) vía `nfcgate-server`, y compatibilidad contra
la app **NFCGate Android**. Documentar el mapeo mensaje↔estado y el flujo del
CLI en la placa real. *(Posterior: TLS `WiFiSSLClient`; migrar host/client
legacy a `core/`.)*

---

## 15. Estado — Fase 7 completada: Camino A (2026-08-17) + Camino B (2026-08-19) validados en HW

**El relay funciona en hardware real (dos BomberCat vía `nfcgate-server`).** Con la
Débito Mastercard sobre el `reader` y un Samsung S24+ (app lectora EMV) sobre el
`card`, una transacción EMV **completa** se relaya de extremo a extremo (SELECT
PPSE → FCI → SELECT AID → GPO → READ RECORD → GET DATA), con el servidor logueando
toda la conversación `OP_PSH R:` / `OP_PSH C:`.

Llegar aquí requirió corregir dos bugs de la ruta RF, **ninguno detectable sin HW**
(la Fase 5 marcó la emulación "completada" sólo compilando):

1. **Activación/re-arme de la emulación** — la BomberCat `card` no presentaba un
   objetivo ISO-DEP reactivable tras la primera activación (0 APDUs / "funciona una
   vez"). Corregido con un re-arme **completo** del PN7150 en `cardReArm()` y el
   alineamiento del orden de init con el legacy (RESUELTO).
2. **Enlace TCP half-open** — tras la 1ª transacción, el socket del `card` hacia el
   servidor quedaba half-open; WiFiNINA no lo detectaba (`connected()`/`write()`
   mienten), así que la 2ª transacción se reenviaba al vacío y colgaba el relay en
   un bucle `timeout → re-poll → re-arm`. Corregido con detección a nivel de
   aplicación (timeout de respuesta → `State::Error` → reconexión vía el auto-retry
   del sketch).

**Confirmado en hardware (2026-08-17):** con ambas placas reflasheadas, se ejecutan
**transacciones de relay consecutivas** sin colgarse (~12 s cada una; el terminal
debe permanecer sobre el `card` ese tiempo). Log limpio del `reader` en
`LogReader.log`. Los dos bugs de §15 (re-arme de emulación y enlace half-open +
re-arme del `reader`) quedan **resueltos**.

### Camino B — VALIDADO en hardware (2026-08-19)
La compatibilidad contra la app **NFCGate Android** funciona en **ambas variantes**, con
el fix de la trama INITIAL / tag config (ver `RelayEngine`, Camino B1/B2) que resolvió el
"empareja SYN/ACK pero no fluye APDU" del reporte previo:
- **B1** — BomberCat `reader` + celular como `card`/HCE: **OK**.
- **B2** — BomberCat `card` + celular como `reader`: **OK**.

Con Camino A (§15) + Camino B (ambas variantes) validados, la **compatibilidad NFCGate del
relay queda demostrada de punta a punta**: la Fase 7 (compat) está **cerrada**.

### Pendiente / posterior
- **Latencia por transacción: bajada a ~4.5 s** (Fases D–I, validadas en HW; ver
  `firmware/LATENCIA_OPTIMIZACION.md`). El piso restante es **arquitectónico** (§17); la única
  vía para bajar de ~4.5 s es el **modo turbo directo placa↔placa**, opt-in y que rompe compat
  NFCGate (`firmware/REDISENO_COMUNICACION.md` §5.1) — abrir solo si se necesita.
- **Fase 8 (captura pcap)**: pendiente el E2E con HW real (capturar una transacción del Camino A).
- *(Posterior: TLS `WiFiSSLClient`; migrar host/client legacy a `core/`.)*

---

## 16. Captura pcap / Wireshark (Fase 8, feature del diseño original v0.1) — diseño

**Estado: IMPLEMENTADO (2026-08-17). El estado y las desviaciones respecto a este diseño están en
§18.** Es la pieza más importante del diseño original
(decisión **D5** y §6 "Captura pcapng")
que quedó fuera del alcance inicial de este plan (que se acotó al relay). Esta sección conserva el
diseño; §18 documenta lo construido.

### Motivación (por qué importaba en v0.1)
- **Captura en el dispositivo** (tap → el host escribe el pcapng): timestamps *ground-truth*, funciona
  **sin servidor** y **fuera del hot-path** (no añade latencia al relay; ver principios P2/P5 del v0.1).
- **Abre directo en Wireshark** para análisis/auditoría de la conversación EMV/ISO-DEP, sin herramientas
  ad-hoc. Bonus del split captura/modificación: en relay, el pcapng del lado `reader` da el APDU *antes*
  de mutar y el del lado `card` el *después*.

### Qué ya existe hoy (reutilizable)
- El `RelayEngine` **ya vuelca cada APDU** como línea de log hex (`... cmd:`/`resp:`) por el `Serial`.
- El CLI **ya tiene el canal de eventos**: `bombercat monitor` (`modules/nfcgate/cli.py`) hace stream del
  serial en vivo y **ya resalta** esas líneas `cmd:`/`resp:`. Es el "tap" del v0.1, pero **volátil**
  (solo pinta en pantalla; no persiste ni estructura nada).

### Qué falta (el trabajo de la Fase 8)
1. **Evento APDU estructurado** (firmware → host), en vez de solo log humano. Emitir por `SerialControl`
   una línea marcada, p. ej. `:apdu <dir> <ts_ms> <hex>` (`dir` = `r2c`/`c2r`), fuera del hot-path
   (el APDU de relay sigue yendo por WiFi/TCP; esto es una **copia** por el canal de eventos). Mapea al
   evento `APDU {dir, ts_ms, bytes}` del v0.1.
2. **Comando `CAPTURE <on|off>`** en `SerialControl` (firmware) para armar/desarmar el tap, expuesto como
   `bombercat capture start|stop [-o archivo.pcapng]` en el CLI (nuevo `modules/capture/cli.py`).
3. **Escritor pcapng en el host** (Python, `modules/capture/`): abre el archivo, y por cada evento `:apdu`
   escribe un Enhanced Packet Block con el timestamp del dispositivo. Sin dependencias externas
   (el formato pcapng es simple de emitir a mano) o con una lib mínima.
4. **Encapsulado que Wireshark entienda** (esquema NFCGate del v0.1): linktype **`DLT_ISO_14443`**,
   anteponiendo una cabecera I-BLOCK artificial + bit de dirección a cada APDU, para que abra directo.

### Diseño (derivado de v0.1 §6, adaptado al `core/` actual)
```
[ RelayEngine ] --(copia del APDU)--> [ SerialControl :apdu r2c <ts> <hex> ]
                                                 |  USB-serial (canal de eventos, NO hot-path)
                                                 v
                      [ bombercat capture ] --> escribe frames DLT_ISO_14443 --> archivo.pcapng --> Wireshark
```
- El tap va **en `RelayEngine`** (donde ya se loguea el APDU), detrás del flag `CAPTURE`, para no gastar
  serial cuando está apagado.
- El **timestamp lo pone el dispositivo** (`millis()` → `ts_ms`), como pide el v0.1 (ground-truth); el host
  puede anclarlo a un epoch al recibir el primer evento.
- Para el relay de dos BomberCat, cada placa captura **su** lado (reader = pre-mutación, card = post-),
  como anota el v0.1.

### Verificación propuesta
- Host test sin HW: alimentar un stream `:apdu` sintético al escritor pcapng y **abrir el resultado en
  Wireshark** (o `tshark -r`) comprobando que disecciona ISO 14443-4 / APDUs.
- E2E con HW: `capture start` durante una transacción EMV real del Camino A y confirmar que el pcapng
  contiene los ~20 pares SELECT PPSE → … → GET DATA ya validados en §15.

### Orden sugerido
1. Evento `:apdu` estructurado en firmware (barato, reutiliza el volcado existente) + `CAPTURE on|off`.
2. Escritor pcapng en el host + comando `bombercat capture` (con el host test).
3. Encapsulado `DLT_ISO_14443` afinado contra Wireshark.
4. (Opcional) Split reader/card documentado y, más adelante, la **modificación** en plugins del
   `nfcgate-server` (D6 del v0.1), que es un eje aparte de la captura.

---

## 17. Latencia por transacción — evaluada y descartada (2026-08-17)

**Conclusión: los ~12–15 s por transacción son el piso de esta arquitectura, no un tweak de
firmware. Se investigó a fondo, se intentaron dos optimizaciones "seguras" y AMBAS regresaron
el relay; se revirtieron y el baseline quedó reconfirmado funcional.** No re-proponer
micro-optimizaciones de firmware para la latencia.

### Medición (desde los timestamps del servidor)
Una transacción EMV real (Camino A) son **~18 pares de APDU** y ~14.8 s de punta a punta
(medido de `LogServer.log`). Cada par tiene dos "piernas" y el servidor las timestampea:

| Pierna | Qué mide | Media | Rango |
|---|---|---|---|
| **READER** (`R:`→`C:`) | comando→respuesta: WiFi + servidor + **transceive RF a la tarjeta física** | ~450 ms | 200–850 ms |
| **CARD** (`C:`→`R:`) | respuesta→siguiente comando: WiFi + servidor + **think-time del kernel EMV del celular** | ~370 ms | 178–654 ms |

El costo está dominado por **física** (transceive RF, cripto de la tarjeta, think-time del
terminal) y **red** (4 saltos WiFi por par a través del `nfcgate-server`). EMV contactless es
**estrictamente lock-step** → **no hay pipelining posible** (el terminal no emite el comando N+1
hasta recibir la respuesta N). El único ahorro sería recortar overhead fijo por-APDU.

### Optimizaciones probadas — ambas REGRESIONES (revertidas)
1. **`Wire.setClock(400000)`** (I2C 100→400 kHz en `NfcController::reset()`): a 400 kHz el
   **segundo `cardModeReceive` obligatorio** de `readerTransceive` dejó de ver su paquete y
   agotó el techo `getMessage(2000)` en **cada** APDU → pierna reader saltó a ~2.3 s y el
   terminal abortó a media transacción. (El card path es inmune: no hace doble-receive.)
2. **`WiFi.noLowPowerMode()`** (en `NFCGate.ino`): mantener el NINA a plena potencia
   **desestabilizó la emulación** (corriente/RF) — el terminal perdía la tarjeta emulada a
   media transacción, en puntos variables (2–4 pares). Revertido.

### Hallazgo que descarta la optimización pendiente #3
En una corrida sana el segundo `cardModeReceive` del reader **NO** toca el techo de 2000 ms
(el paquete llega rápido), así que IRQ-gatearlo con `hasMessage()` **no ahorra nada en el
happy path** — solo ayudaría en errores. Su valor era casi nulo; se abandona.

### Camino real (si algún día importa la latencia)
Bajar de ~12–15 s de forma significativa requiere un cambio **arquitectónico**: enlace directo
placa↔placa **sin el hop del servidor** (elimina 2 de los 4 saltos WiFi por par). Pero eso
**rompe la compatibilidad NFCGate**, que es el objetivo del proyecto — así que no vale la pena
mientras esa compat siga siendo la meta.

### Retomado (2026-08-18): overhead fijo por-APDU + robustez
El **piso** sigue siendo arquitectónico (arriba), pero se identificaron dos palancas que §17 **no
había evaluado** y que atacan overhead fijo por-APDU / robustez sin tocar la ruta RF frágil:
logging en `Debug` corriendo en cada APDU (con riesgo de bloqueo USB-CDC standalone) y el write I2C
basura de `cardModeReceive`. El análisis, el plan por fases y la bitácora incremental de
qué se probó / qué funcionó están en
[`firmware/LATENCIA_OPTIMIZACION.md`](../firmware/LATENCIA_OPTIMIZACION.md). **Fase A (logging quiet)
y A.2 (IRQ-gate del 2º receive del reader) implementadas y probadas en HW (FW 0.9.1): la
transacción completa en ~15 s → el piso de §17 quedó RECONFIRMADO. Fase A.2 vale como robustez
(evita abortar por el busy-wait de 2000 ms en tarjetas de un paquete), no baja el piso.**

---

## 18. Estado — Fase 8 completada (2026-08-17): Captura pcap / Wireshark

**El tap de APDUs y la captura a pcap/Wireshark están implementados y verificados sin hardware
(contra `tshark` 4.4).** Con esto la pieza D5/§6 del diseño original v0.1 (captura para Wireshark),
que había quedado fuera del alcance del relay, queda cubierta. Falta solo el E2E con HW real
(capturar una transacción EMV del Camino A).

### Cómo quedó (firmware → host)
```
[ RelayEngine ] --(copia de cada APDU)--> [ ":apdu <cmd|resp> <ts_ms> <hex>" por Serial ]
                                                    |  USB-serial (canal de eventos, NO hot-path)
                                                    v
                    [ bombercat capture ] --> frames DLT_ISO_14443 --> FIFO -> Wireshark  y/o  archivo .pcap
```
- **Firmware — tap en `RelayEngine`** (`core/src/RelayEngine.{h,cpp}`): `setCapture(Print*)` /
  `capturing()` + `emitCapture()`. Cuando hay sink, cada APDU relayado se **copia** como una línea
  `:apdu <dir> <ts_ms> <hex>` (`dir` = `cmd` = terminal→tarjeta / `resp` = tarjeta→terminal; `ts_ms`
  = `millis()` del dispositivo, timestamp *ground-truth*). Está en los 4 puntos donde ya se logueaba
  el APDU, detrás del guard del sink → **coste cero con la captura apagada** y **fuera del hot-path**
  (el APDU de relay sigue yendo por WiFi/TCP; esto es solo una copia por el canal de eventos).
- **Firmware — comando `capture <on|off>`** en `SerialControl` (`core/src/SerialControl.{h,cpp}`):
  `capture on` fija el sink al mismo `Serial` del REPL; `capture off` lo quita; `capture`/`capture
  status` responde `:capture <0|1>`. Versión de firmware → **`0.8.0`**.
- **Host — `bombercat capture`** (`tools/modules/capture/`): `start [-o FILE] [-ws/--wireshark]
  [--profile P]` arma el tap (`capture on`), parsea los eventos `:apdu`, arma frames pcap y los
  vuelca **en vivo a Wireshark por un FIFO** y/o a un **archivo `.pcap`**; Ctrl-C desarma el tap.
  Wireshark es **opt-in** con `-ws` (como en catnip); sin `-ws` la captura va solo al archivo. Si el
  usuario cierra Wireshark a media captura se detecta (poll del proceso) y se sigue con el archivo,
  o se para la captura si no había archivo.
  `stop` desarma una placa que quedó armada. El timestamp del dispositivo se ancla al reloj de pared
  del host en el primer APDU (los *deltas* son el timing real del dispositivo).
- **Transporte FIFO + lanzador de Wireshark** (`tools/modules/core/pipes.py`): `UnixPipe`/`WindowsPipe`
  + `Wireshark` (portado de catnip; nombre de pipe `fbombercat`). El escritor pcap
  (`tools/modules/capture/pcap.py`) es clásico-pcap por `struct`, sin dependencias nuevas.

### Desviaciones respecto al diseño de §16 (decididas con el usuario)
1. **pcap clásico, no pcapng.** Se eligió el formato clásico de catnip (global header + per-packet
   header por `struct`), que Wireshark abre idéntico y sirve para el FIFO en vivo y para archivo. El
   pcapng (bloques SHB/IDB/EPB) queda como mejora futura si se quiere metadata de sección o ns.
2. **Encapsulado `DLT_ISO_14443` afinado y VERIFICADO contra Wireshark** (era el paso 3 "a afinar"):
   pseudo-cabecera `version(0x00) + event(1B) + len(2B BE)` con `event` **0xFE = PCD→PICC** (comando)
   / **0xFF = PICC→PCD** (respuesta), y el APDU envuelto en un **I-block ISO 14443-4** (PCB `0x02`,
   bit de bloque alternando). Con esto `tshark` diseca cada trama como ISO 14443-4 I-block con la
   dirección correcta y **sin "unknown command" ni malformed** (el APDU crudo sin I-block salía como
   "Unknown ISO1443 command"). Los valores de `event` se **probaron empíricamente** contra `tshark 4.4`.
3. **Etiquetas de dirección `cmd`/`resp`** (autodescriptivas y mapeadas directamente al evento
   ISO 14443) en vez de `r2c`/`c2r` del v0.1 — equivalen 1:1 (cmd = r2c = PCD→PICC; resp = c2r =
   PICC→PCD).

### Verificación (2026-08-17)
- **Firmware compila** (`arduino-cli` 1.5.1, mismo toolchain): `firmware/NFCGate` →
  **134698 B flash (6%) / 47160 B RAM (17%)** (sube ~2.8 KB flash desde Fase 6 por el tap +
  comando `capture`). `CoreSelfTest` → 129362 B (ejercita `setCapture`/`capturing`).
  Bug atrapado al compilar: `HEX` es un macro de Arduino (base de `print`) → la tabla local
  `HEX[]` colisionaba; renombrada a `kHexDigits` (misma clase de bug que el `ERROR` de Fase 2).
- **Host test sin HW** (`tools/tests/capture_hosttest.py`): alimenta un transcript EMV sintético de
  eventos `:apdu` por el **mismo** regex + `PcapBuilder` que usa el CLI, escribe un `.pcap` y lo
  diseca con `tshark`: 6/6 tramas como **ISO 14443** con las direcciones correctas (`0xfe`/`0xff`) y
  **ninguna malformed** → **CAPTURE HOST TEST PASSED**. Degrada limpio si no hay `tshark` (verifica
  solo el escritor). `serialctl_hosttest.py` sigue en verde (sin regresión).
- **CLI**: `bombercat capture --help` / `start --help` / `stop` expuestos y registrados; los fallos
  sin HW/Wireshark son limpios (mensaje + exit 1, sin traceback).

### E2E con HW real — VALIDADO (2026-08-19)
`capture` armado en la BomberCat `reader` durante una transacción EMV real del Camino A, con Wireshark
abierto sobre el FIFO en vivo → `CapturaWireshark.pcapng` contiene la conversación completa
(SELECT `2PAY.SYS.DDF01`/PPSE → `Debit Mastercard` / `D MERCADOPAGO` → SELECT AID → GPO → READ RECORD),
diseccionada como ISO 14443-4 con la dirección correcta por trama. **Nota:** el archivo es **pcapng**
porque lo escribió el propio Wireshark desde el FIFO (Dumpcap 4.6.6), no el escritor pcap-clásico del
CLI (que sigue siendo el que alimenta el FIFO / el `-o archivo.pcap`). La Fase 8 queda **cerrada**.

### Pendiente / opcional en Fase 8
- *(Opcional)* pcapng con timestamps de ns; handoff automático a `iso7816`/EMV (hoy el INF del I-block
  se ve como bytes; se puede "Decode As" ISO 7816); split reader/card documentado (pre/post-mutación);
  y, aparte de la captura, la **modificación** vía plugins del `nfcgate-server` (D6 del v0.1).
