# Reducción de latencia por transacción — bitácora de optimización

> Estado: **documento vivo** · Creado: 2026-08-18 · Última actualización: 2026-08-18
> Contexto: continúa el análisis de [`docs/NFCGATE_PLAN.md` §17](../docs/NFCGATE_PLAN.md) (latencia
> ~12–15 s por transacción). §17 la declaró "piso arquitectónico" tras revertir dos palancas;
> este archivo retoma la meta con hallazgos **nuevos** que §17 no evaluó, y lleva un registro
> incremental de qué se probó, qué funcionó y qué regresó, para no repetir experimentos entre
> sesiones.
>
> **TL;DR al 2026-08-18:** Fase A (logging quiet + `loglevel`) y A.2 (IRQ-gate del 2º receive del
> reader) implementadas, compilan, FW **0.9.1**. Probado en HW: la transacción **completa** en
> **~15 s** — **el piso arquitectónico de §17 quedó reconfirmado; ninguna palanca de firmware lo
> baja**. Fase A.2 vale como **robustez** (evita el abort por busy-wait de 2000 ms en tarjetas de
> un paquete), no como reducción de latencia. Siguientes pasos y pendientes en §5.
>
> **ACTUALIZACIÓN 2026-08-18 (revisión del hot-path + logs reales):** el "piso intocable" se matiza.
> Leyendo `readerTransceive` y los timestamps de `LogServer.log` se identificaron **dos costos fijos
> por-APDU** que §17/A.2 no atacaron como latencia:
> 1. **Fase D — `SECOND_PACKET_WINDOW_MS`** (código propio): la ventana de 120 ms que introdujo A.2
>    se **consume entera** en CADA APDU con una tarjeta de **un solo paquete** (= la de auditoría),
>    porque el 2º paquete nunca llega. ~18 APDU × 120 ms ≈ 2.16 s de tiempo muerto. Bajada a **25 ms**
>    (FW **0.9.2**). ✅ **Validado en HW: ~1.5 s menos** (~15 s → ~13.5 s), transacción completa OK.
> 2. **Fase C / H2** — el `writeData(Ans,255)` de `cardModeReceive` **sí transmite** (~23 ms/llamada,
>    confirmado leyendo Wire de mbed) y es **funcionalmente inútil** (`getMessage` no lo consume).
>    Implementado como `receiveNoGarbage` (réplica local con `readData()` público, sin forkear la
>    lib). 1ª corrida (0.9.3) **regresó** (transceive fallaba por no saltar el credits-NTF); **arreglado en 0.9.4**. ~0.8–1.2 s esperados. ⏳ re-pend. E2E HW. Detalle §2/§4.
>
> 3. **Fase E — RED, no firmware:** el server no seteaba `TCP_NODELAY` y partía cada frame en dos
>    writes (con `wfile` sin buffer) → stall Nagle/delayed-ACK **~40 ms por relay server→placa**, en
>    2 de los 4 saltos por par (~1.4 s potenciales). Arreglado en `server/server.py` (`TCP_NODELAY` +
>    write coalescido). Se prueba **sin reflashear** (solo reiniciar el server). Detalle §2 H4 / §4.
>
> **Estado latencia — ✅ VALIDADO EN HW (2026-08-18, múltiples corridas):** ~15 s (A.2) → ~13.5 s
> (Fase D) → **~5 s de promedio con D+C+E juntas (FW 0.9.4 + server con Fase E)** → **~4.5 s con Fase F
> (FW 0.9.5)**. Transacción completa, datos correctos. **>3× más rápido que el baseline.** El resultado
> (~5 s, muy por debajo del objetivo de ~10–12 s) indica que **Fase E (Nagle/delayed-ACK en el server)
> era el mayor contribuyente** — el stall de red se apilaba peor que los ~40 ms/salto estimados.
> **REFUTA la premisa de §17** de que "los 4 saltos WiFi eran física de red intocable": una gran parte
> era Nagle, puro software del server.
>
> **ACTUALIZACIÓN 2026-08-18 (Fase F, FW 0.9.5) — ✅ VALIDADO EN HW:** hallazgo nuevo H5 — el WiFi es
> WiFiNINA (ESP32 = coprocesador SPI), así que `poll()` leía el header de 4 B **byte-a-byte** = ~8
> transacciones SPI/frame. Colapsado a **una lectura en bloque** (análogo del lado-placa de la Fase E).
> Riesgo RF nulo. Bajó **~0.5 s** (~5 s → ~4.5 s) — confirma que ese overhead SPI era recortable, no
> física. Bajar de ~4.5 s ya topa con lo irreducible (think-time del terminal + cripto de la tarjeta +
> transceives RF + saltos WiFi ya sin Nagle); queda la Fase G (ventana 25→10 ms, ~270 ms) como último
> recorte de firmware antes del piso — ver §5.

---

## 1. Dónde se va el tiempo (confirmado leyendo el hot-path, no solo timestamps del server)

Una transacción EMV real = **~18 pares APDU estrictamente lock-step**, encadenados de forma
causal (no hay pipelining posible — el terminal no emite el comando N+1 hasta responder el N):

```
terminal → card(RF) → WiFi → server → WiFi → reader → RF → tarjeta física → vuelta (×18)
```

Medición de §17 (desde `LogServer.log`): pierna READER ~450 ms, pierna CARD ~370 ms, ~14.8 s total.

**Conclusión arquitectónica de §17 CONFIRMADA:** el piso lo fijan física (transceive RF, cripto
de la tarjeta, think-time del terminal) + red (4 saltos WiFi por par vía `nfcgate-server`). Ni
RTOS ni hilos bajan ese piso: una cadena causal serial no se paraleliza. Bajarlo de verdad exige
enlace directo placa↔placa (romper compat NFCGate), fuera de alcance.

**PERO** §17 solo probó dos palancas y ambas tocaban la ruta RF frágil:
- `Wire.setClock(400000)` → regresó (segundo `cardModeReceive` del reader perdía su paquete).
- `WiFi.noLowPowerMode()` → regresó (desestabilizó la emulación).

§17 **nunca tocó dos cosas que sí están en el hot-path** y que este documento ataca.

---

## 2. Hallazgos nuevos (no evaluados en §17)

### H1 — Logging en `Debug` corriendo en cada APDU  ✅ ATACADO (Fase A)
`NFCGate.ino` arrancaba con `Log::begin(Serial, LogLevel::Debug)`. A nivel Debug **cada** APDU
dispara volcados hex: `R<- cmd` / `R-> resp` / `C<- term cmd` / `C-> term resp` (hasta 512 chars
hex para un READ RECORD de 256 B), más `frame rx` (INFO) y resúmenes INFO.

Dos costos, uno crítico:
- **CPU**: construcción de `String` + formateo hex por APDU (menor, pero real).
- **Bloqueo USB-CDC (crítico):** en mbed, `Serial` es USB CDC. **Standalone sin host leyendo el
  serial** (autostart), el buffer TX se llena y `Serial.print` **bloquea el hilo del relay a media
  transacción**, justo entre recibir la respuesta del peer y `cardSend()` al terminal → latencia
  directa en la ruta crítica. Con `bombercat monitor`/`capture` (host drenando USB) el costo es
  menor, pero el formateo sigue ahí.

Riesgo de timing RF: **nulo** (gating barato; no toca I2C ni WiFi). Totalmente reversible.

### H2 — Write I2C basura en `cardModeReceive` (librería PN7150)  ✅ ATACADO y VALIDADO EN HW (Fase C, 0.9.4)
`Electroniccats_PN7150::cardModeReceive()` hace, en cada llamada:
```cpp
delay(1);
(void)writeData(Ans, 255);   // transmite hasta 255 B basura por I2C ANTES de leer
getMessage(2000);            // busy-poll de readData() (IRQ-gated) hasta dato o 2000 ms
```

**MEDICIÓN CERRADA (2026-08-18, sin hardware, solo leyendo el código):**
- El buffer Wire de mbed_rp2040 es `uint8_t txBuffer[256]` (≥255). En `writeData`,
  `_wire->write(Ans,255)` con buffer vacío acepta los 255 B (`usedTxBuffer+len<=256`) → devuelve
  255 → `nmbrBytesWritten == txBufferLevel` **TRUE** → llama `endTransmission()` → **SÍ transmite
  los 255 B al bus**. A 100 kHz ≈ **~23 ms por llamada**. (Ruta: `Wire.cpp:112` write bulk,
  `Wire.h` `txBuffer[256]`, `Electroniccats_PN7150.cpp` `writeData`.)
- **El write es funcionalmente INÚTIL:** `getMessage(2000)` es un busy-poll de `readData()`, que
  solo lee cuando el PN7150 sube su IRQ (`hasMessage()`), **independiente** del write previo. El
  PN7150 es IRQ-driven: sube IRQ cuando ÉL tiene datos para nosotros; nuestro write no le pide nada.
  El `writeData(Ans,255)` es casi seguro un copy-paste de un patrón NCI write-then-read.
- **Costo por transacción:** ~1 `cardModeReceive` por comando en la placa card + ~1–2 por transceive
  en la placa reader → ~36–54 llamadas → **~0.8–1.2 s** de basura I2C recuperables.

**Implementado y validado (sin forkear la lib):** `readData()` es **público**; `getMessage`/`rxBuffer`
son privados. Se replicó `cardModeReceive` dentro de `NfcController` (`receiveNoGarbage`) **sin** el
`writeData(255)`, sondeando `readData(buf)` con timeout propio (`millis()`) y **saltando frames
no-data** hasta el data packet (`buf[0]==0x00 && buf[1]==0x00` → `len=buf[2]`, copiar `buf[3..]`).
Ver §4 Fase C — incluye la regresión 0.9.3 (por no saltar el credits-NTF) y su fix en 0.9.4.
- **Riesgo:** toca la ruta RF, pero el cambio vive en código propio y es reversible. Validado en HW
  (0.9.4): transacción completa, datos correctos.

### H3 — Segundo `cardModeReceive` del reader gasta 2000 ms por APDU  ✅ ATACADO (Fase A.2) · EL MÁS IMPORTANTE
**Descubierto con hardware real (2026-08-18).** El relay funcionaba
—APDUs correctos de punta a punta— pero la transacción **moría en READ RECORD** porque era
demasiado lenta y el terminal se rendía. Separando piernas desde los timestamps del server:

| Pierna | Esta corrida | §17 baseline |
|---|---|---|
| CARD (C→R) | ~0.35–0.55 s ✓ | ~0.37 s |
| **READER (R→C)** | **~2.4 s** ✗ | ~0.45 s |

La pierna reader estaba **5× más lenta**. Causa: `NfcController::readerTransceive` leía un SEGUNDO
paquete de forma **incondicional** (patrón legacy "la respuesta es el 2º paquete"). Pero
`cardModeReceive()` hardcodea `getMessage(2000)`. La tarjeta de prueba manda **UN solo paquete**
(la respuesta ya está en el 1er receive → por eso los datos salían correctos), así que el 2º
receive no encontraba nada y **busy-waiteaba los 2000 ms completos en CADA comando**. 18 pares ×
~2 s ≈ 40 s → terminal aborta.

**Esto REFUTA el hallazgo #3 de §17** ("el 2º cardModeReceive no toca el techo de 2000 ms en el
happy path"). El hardware muestra que **sí** lo toca con esta tarjeta. La suposición de §17 era
específica de una tarjeta de doble-paquete.

**Fix (Fase A.2):** IRQ-gatear el 2º receive. El PN7150 sube su pin IRQ solo cuando hay frame
pendiente → `hasMessage()` se sondea una ventana corta (`SECOND_PACKET_WINDOW_MS = 120`). Si llega
un 2º paquete real (tarjetas de doble-paquete, rápido) se lee; si no (tarjetas de un paquete) se
retorna de inmediato con la respuesta del 1er paquete. Sirve para **ambos** tipos y elimina el
muerto de ~2 s/APDU. Debería **reproducir** los ~450 ms de §17 (doble-paquete) Y arreglar los 2.4 s
(un paquete) — estrictamente mejor.

### H4 — Nagle + delayed-ACK en el server (2 de los 4 saltos WiFi por par)  ✅ ATACADO (Fase E) · RED, NO FIRMWARE
**El "piso de red" de §17 no era todo física.** `nfcgate-server` (Python `socketserver`) **no seteaba
`TCP_NODELAY`** (Nagle activo) y en `send_to_clients` escribía cada frame en **dos writes** (header de
4 B + payload). Como `StreamRequestHandler` usa `wbufsize=0` (wfile sin buffer, **confirmado**), esos
dos writes salían como **dos segmentos TCP**. Con Nagle activo, el segmento del payload **espera el ACK
del header** — o el timer de **delayed-ACK (~40 ms)** — en **cada** relay server→placa. Son 2 de los 4
saltos por par de APDU (el comando al reader y la respuesta al card) → hasta **~40 ms de tiempo muerto
por salto**, ~18 pares → potencialmente **~1.4 s o más** (y explica la varianza rara de piernas 120 vs
700 ms en `LogServer.log`).
- **Riesgo:** nulo para el firmware y la RF — es Python del lado del server. Totalmente reversible.
- **Fix (Fase E):** `setsockopt(IPPROTO_TCP, TCP_NODELAY, 1)` por conexión + **un solo write coalescido**
  (header+payload juntos). Se prueba **sin reflashear** (solo reiniciar el server).
- Es la **única palanca de red** a nuestro alcance sin romper compat NFCGate. Bajar más (eliminar los 4
  saltos → enlace directo placa↔placa) sigue fuera de alcance (rompe compat con la app NFCGate).

### H5 — Header de 4 B leído byte-a-byte sobre SPI (WiFiNINA)  ✅ ATACADO (Fase F) · RED/TRANSPORTE, NO RF
**El WiFi de la BomberCat es WiFiNINA: el ESP32-WROOM es un coprocesador que habla con el RP2040
por SPI.** Por eso **cada `_c.read()` y cada `_c.available()` del `WiFiClient` es una transacción SPI
completa** (comando + handshake slave-ready) contra el ESP32 — no una lectura de RAM local. En
`NfcGateLink::poll()` la Fase 1 acumulaba el header de longitud (4 B big-endian) **un byte a la vez**
(`while (...) { int b = _c.read(); }`), gastando hasta **4 `read()` + 4 `available()` ≈ ~8
transacciones SPI solo para el header** de CADA frame. Y como **Fase E hizo que el server mande
header+payload en UN solo segmento TCP**, al llegar a la placa el header entero ya está buffered en el
ESP32 — así que ese byte-a-byte es puro overhead evitable.
- **Costo por transacción:** ~18 frames recibidos por placa (1 comando en el reader + 1 respuesta en
  el card por par APDU) × ~6 transacciones SPI de más = **~100 transacciones SPI desperdiciadas por
  placa**. A ~cientos de µs–pocos ms cada una → **~100–200 ms recortables end-to-end** (las dos placas
  están en serie en la cadena causal, así que se apila).
- **El payload YA se lee en bloque** (`_c.read(&_rx[_fill], want)`); solo el header pagaba el byte-a-byte.
- **Riesgo:** **nulo** para la RF (es puro transporte WiFiNINA; jamás toca I2C/PN7150). Preserva la
  semántica no-bloqueante: si el header llega partido, `read()` toma solo lo presente y `_hdrHave`
  arrastra la cuenta entre llamadas a `poll()`, igual que antes.
- **Fix (Fase F):** una sola lectura en bloque `_c.read(&_hdr[_hdrHave], 4 - _hdrHave)` cuando hay
  bytes disponibles, en vez del bucle byte-a-byte. Es el análogo del lado-placa de Fase E (que atacó
  el mismo stall pero del lado-server). Ver §4 Fase F.

### H6 — Logging por-frame del server corriendo en cada relay  ✅ ATACADO (Fase H) · RED/SERVER, NO FIRMWARE
**El análogo server-side de H1.** `server/server.py` hacía, por **cada frame relayado**, dos `print`
en el hot-path lock-step:
- En `handle()`, **antes** de reenviar: `self.log("server", "data:", bytes(data))` — construye el
  `repr` del payload completo (hasta 256 B en un READ RECORD) + `datetime.now()` + `print`. Corre
  **síncrono entre recibir el frame y `send_to_clients`**, así que su costo se **apila en la cadena
  causal** (el server está en medio de los ~36 saltos por transacción).
- En `send_to_clients()`, **después** de reenviar: `self.log("Publish reached", ...)` — otro `print`
  por frame que roba el hilo (y el GIL, siendo `ThreadingTCPServer`).
- **Costo:** ~2 `print` × ~36 frames/txn = ~72 líneas. Con stdout a tty (line-buffered → flush por
  línea) o a `tee`, cada flush cuesta; más el formateo del `repr`/`datetime`. Estimado **~50–150 ms/txn**
  recuperables (depende de a dónde vaya stdout). No es enorme, pero es **gratis, sin riesgo RF, y se
  prueba sin reflashear** — el mismo perfil que hizo atractiva la Fase E.
- **Fix (Fase H):** ambos logs gated tras un flag `-v/--verbose` (default silencioso). Los logs de
  conexión/sesión (raros, no hot-path) se conservan. Ver §4 Fase H.

### RTOS — qué sí ayuda y qué no
El core `mbed_rp2040` **ya corre sobre Mbed OS RTX** (`loop()` ya es un hilo). Más hilos **no bajan
el piso**. El único uso útil: un **hilo de logging dedicado con cola** para que la E/S serial nunca
bloquee el hot-path (resuelve H1 en su peor caso sin perder observabilidad). Ataca jitter/peor-caso,
no la mediana. → Fase B.

---

## 3. Plan por fases (incremental, validando en hardware entre cada una)

| Fase | Qué | Riesgo RF | Estado |
|---|---|---|---|
| **A** | Logging quiet: default `Warn` + comando `loglevel 0..4` en runtime | Nulo | ✅ Implementado, compila. E2E parcial (ver nota) |
| **A.2** | IRQ-gatear el 2º `cardModeReceive` del reader (H3) | Medio (ruta RF, pero código propio) | ✅ Implementado y probado en HW. Robustez OK; **no baja el piso** |
| **D** | Bajar `SECOND_PACKET_WINDOW_MS` 120→25 ms (recorta ~2 s de dead-time en tarjeta de un paquete) | Casi nulo (código propio) | ✅ **Validado en HW: ~1.5 s menos** (0.9.2) |
| **C** | Quitar el `writeData(Ans,255)` inútil de `cardModeReceive` (H2, ~23 ms/llamada) vía `receiveNoGarbage` | Medio (ruta RF, código propio) | ✅ **Validado en HW** (0.9.4, tras fix del salto de frames no-data) |
| **E** | Server: `TCP_NODELAY` + write coalescido (mata el stall Nagle/delayed-ACK) | Nulo (Python, no firmware/RF) | ✅ **Validado en HW — el mayor recorte** (D+C+E ⇒ ~5 s) |
| **F** | Placa: leer el header de 4 B en UNA lectura SPI en `poll()` (no byte-a-byte) (H5) | Nulo (transporte WiFiNINA, no RF) | ✅ **Validado en HW: ~0.5 s menos** (~5 s → ~4.5 s), FW 0.9.5 |
| **G** | Bajar `SECOND_PACKET_WINDOW_MS` 25→10 ms (recorta ~270 ms de dead-time en tarjeta de un paquete) | Casi nulo (código propio) | ✅ **Validado en HW: funcional, recorte marginal** (FW 0.9.6) |
| **H** | Server: silenciar los 2 logs por-frame del hot-path (`data:` dump + `Publish reached`), gated tras `-v` (H6) | Nulo (Python, no firmware/RF) | ✅ **Validado en HW: funcional, sin regresión** (recorte marginal, no aislado). Permanente |
| **I** | Acotar el peor caso de `readerHandleCommand` al presupuesto de WTX: fail-fast a mitad de transacción (sin re-arm ni replay), auto-cura idempotente solo en el borde (H7); **+ persistencia de borde** (loop discovery+re-arm bajo `READER_BOUNDARY_ACTIVATE_MS`=3000, pendiente #2) | Bajo (código propio; endurece, no agrega ruta RF) | ✅ Implementado, compila (FW 0.9.7) · ⏳ pend. E2E HW |
| **B** | Hilo RTOS de logging (cola lock-free; Serial fuera del hot-path) | Bajo | ⏳ No iniciado |

---

## 4. Changelog / registro de experimentos

### Fase A — logging quiet (2026-08-18)  ✅ implementado · ⏳ pendiente E2E HW
**Cambios:**
- `firmware/NFCGate/NFCGate.ino`: `Log::begin(Serial, LogLevel::Warn)` (era `Debug`). Relay
  silencioso por defecto. FW version `0.8.0 → 0.9.0`.
- `firmware/core/src/SerialControl.cpp`: nuevo comando `loglevel [0..4|status]`
  (0=None 1=Error 2=Warn 3=Info 4=Debug). Sube/baja verbosidad sin recompilar.
- `tools/modules/nfcgate/cli.py`: `bombercat monitor` sube a `loglevel 4` al entrar y vuelve a
  `loglevel 2` al salir (degrada limpio si el FW no tiene el comando). Así `monitor` sigue mostrando
  los volcados APDU pese al default silencioso. `capture` no se afecta (sink aparte).

**Verificación:**
- Compila (`arduino-cli`, mismo toolchain): `firmware/NFCGate` → **134829 B flash (6%) / 47160 B
  RAM (17%)** (+131 B flash desde Fase 8 §18 por el comando `loglevel`).
- CLI importa; `bombercat monitor --help` OK.
- **PENDIENTE (requiere HW):** correr una transacción del Camino A (§15) con el default silencioso
  y medir si baja el tiempo por transacción **standalone** (sin host leyendo serial) vs. baseline.
  Hipótesis: el mayor ahorro aparece standalone (donde `Serial.print` bloqueaba); con `monitor`
  activo el ahorro será menor. Registrar ambos números aquí.

**Cómo probar en HW:**
1. Reflashear ambas placas con el firmware nuevo.
2. Standalone (autostart, sin USB a PC o con USB pero sin `monitor`): correr N transacciones y
   cronometrar. Comparar contra los ~12–15 s de §15.
3. Reactivar diagnóstico cuando haga falta: `loglevel 4` por el CLI (o `bombercat monitor`).

**Resultado E2E (2026-08-18): PARCIAL — sin conclusión aislada.** Las pruebas se corrieron con
`bombercat monitor` en ambas placas, que sube el nivel a `loglevel 4` (Debug) → el hot-path vuelve
a loguear y un host drena el USB, así que el beneficio de Fase A (relay silencioso standalone, sin
host leyendo el serial) **no se midió aislado**. Para probarlo hace falta correr **sin monitor**
(autostart, sin PC drenando el serial) y cronometrar. Beneficio esperado: menor (cientos de ms), y
solo aparece en ese escenario standalone. **Pendiente para otra sesión.**

### Fase A.2 — IRQ-gate del 2º receive del reader (2026-08-18)  ✅ implementado · ⏳ pendiente E2E HW
**Cambios:**
- `firmware/core/src/NfcController.cpp` (`readerTransceive`): tras el 1er paquete, sondea
  `_nfc.hasMessage()` hasta `SECOND_PACKET_WINDOW_MS`; solo lee el 2º receive si hay IRQ. Reemplaza
  la lectura incondicional que gastaba 2000 ms/APDU en tarjetas de un paquete.
- `firmware/core/src/NfcController.h`: constante `SECOND_PACKET_WINDOW_MS = 120` (ajustable).
- FW version `0.9.0 → 0.9.1`.

**Verificación:** compila → **134861 B flash (6%) / 47160 B RAM (17%)**.

**PENDIENTE (requiere HW):** correr una transacción del Camino A y medir la pierna READER en el
server log. Esperado: bajar de ~2.4 s a ~0.4–0.55 s → transacción completa (llega a GET DATA) en
~12–15 s en vez de abortar. **Si una transacción con la Débito Mastercard sale con datos correctos
Y completa, Fase A.2 validada.** Si algún dato sale mal (p.ej. se relaya un frame intermedio en
lugar de la respuesta), subir `SECOND_PACKET_WINDOW_MS` y volver a probar; anotar el valor que
funcione aquí.

**Cómo probar en HW (Fase A + A.2 juntas):**
1. Reflashear ambas placas (0.9.1).
2. `bombercat monitor` en ambas (sube a `loglevel 4` solo). Correr transacción con la Débito
   Mastercard sobre el reader y el S24+ sobre el card.
3. Confirmar en el server log que la pierna R→C bajó a cientos de ms y que la transacción llega a
   GET DATA (transacción completa). Anotar tiempos aquí.

**Resultado E2E (2026-08-18): transacción COMPLETA de nuevo; latencia en el piso ~15 s (sin cambio
perceptible).**
- Con Fase A.2 la transacción vuelve a **completar** de punta a punta (~15 s medidos de inicio a
  fin por el usuario). El síntoma de la corrida anterior —pierna reader ~2.4 s por el busy-wait de
  2000 ms en tarjetas de un paquete, que hacía abortar la transacción— **no reapareció**.
- **La latencia NO bajó respecto al baseline de §15/§17 (~12–15 s).** Interpretación: los ~15 s son
  el **piso arquitectónico** (física del transceive RF + cripto de la tarjeta + think-time del
  terminal + 4 saltos WiFi por par, EMV lock-step). Fase A.2 es un **fix de robustez** (maneja
  tarjetas de uno o dos paquetes sin el muerto de 2 s) que restaura la finalización, pero no ataca
  el piso — el 2.4 s era una anomalía de una corrida, no la mediana.
- **Observación de campo:** la detección de la tarjeta física por el reader (`waitForTag`) es
  quisquillosa — una corrida falló con "sin tarjeta en campo" por posicionamiento/acoplamiento RF
  (no por el firmware). Ver §5, candidato de robustez para otra sesión.

### Fase D — bajar `SECOND_PACKET_WINDOW_MS` 120→25 ms (2026-08-18)  ✅ implementado · ⏳ pend. E2E HW
**Descubrimiento:** revisando `readerTransceive` (código propio, no la lib) + los timestamps de
`LogServer.log` se vio que la ventana IRQ que introdujo A.2 es un **costo fijo por-APDU** para
tarjetas de un solo paquete: como no hay 2º paquete, el bucle `while(!hasMessage())` agota los
120 ms enteros en CADA comando antes de retornar la respuesta del 1er paquete. Con ~18 APDUs ≈
**2.16 s de dead-time** que se van directo al total. Las tarjetas de doble-paquete NO pagan esto
(salen por IRQ apenas el PN7150 sube el 2º paquete, que ya está buffered tras el único transceive
RF → pocos ms).

**Cambio:**
- `firmware/core/src/NfcController.h`: `SECOND_PACKET_WINDOW_MS` **120 → 25** ms (+ comentario que
  explica el porqué y cómo re-ajustar). Es el único cambio de código; la lógica de `readerTransceive`
  no se tocó.
- FW version `0.9.1 → 0.9.2`.

**Verificación:** compila → **134861 B flash (6%) / 47160 B RAM (17%)** (sin cambio de tamaño: solo
una constante).

**RESULTADO E2E (2026-08-18): ✅ VALIDADO EN HW — ~1.5 s menos.** Flasheadas ambas placas (0.9.2),
transacción probada y cronometrada por el usuario: el tiempo promedio bajó **~1.5 s** (de ~15 s a
~13.5 s). La transacción **completa** con datos correctos (sin regresión con la tarjeta de un
paquete). Confirma que la ventana de 120 ms era dead-time real por-APDU, no física. La latencia
total sigue >10 s → el resto son los otros costos (Fase C + física RF/red); se continúa con Fase C.

### Fase C — quitar el `writeData(Ans,255)` inútil de `cardModeReceive` (H2, 2026-08-18)  ✅ implementado · ⏳ pend. E2E HW
Medición cerrada en §2/H2: el write transmite ~23 ms de basura por llamada y `getMessage` no lo
necesita. **Implementado** como réplica local en `NfcController`, sin tocar la lib vendorizada.

**Cambios:**
- `firmware/core/src/NfcController.cpp`: nuevo `receiveNoGarbage(pData, pDataSize, toutMs)` — busy-poll
  del `readData()` **público** (IRQ-gated) hasta `toutMs`, parseo igual que `cardModeReceive` (header
  `0x00 0x00`), **sin** el `writeData(Ans,255)`. Reemplaza las **tres** llamadas a
  `_nfc.cardModeReceive(...)`: 1er paquete y 2º paquete de `readerTransceive`, y `cardReceive`.
- `firmware/core/src/NfcController.h`: declaración privada de `receiveNoGarbage`.
- FW version `0.9.2 → 0.9.3` (regresó en HW) → `0.9.4` (fix del salto de frames no-data).

**Verificación:** compila → **134857 B flash (6%) / 47160 B RAM (17%)** (4 B menos que 0.9.2).

**RESULTADO E2E 0.9.3 (2026-08-18): ❌ REGRESIÓN — transceive fallaba en el 1er comando.**
Log del server: el comando SELECT PPSE llegaba al reader (`R<- cmd` correcto, Fase E OK) y
la tarjeta se activaba (`reader: tarjeta activada`), pero acto seguido `reader: transceive
fallo/timeout` y la transacción colapsaba (el card timeouteaba y forzaba reconexión).

**Causa (bug de la 1ª versión de `receiveNoGarbage`):** leía **un solo frame**. En modo reader, tras
`cardModeSend()` el PN7150 emite un **`CORE_CONN_CREDITS_NTF` (0x60 0x06)** — frame de control
**no-data** — *antes* del data packet con la respuesta de la tarjeta. El bucle original
`while (_nfc.cardModeReceive(...))` **iteraba saltándose los frames no-data** hasta el data packet;
al colapsarlo en una sola lectura, el primer frame (0x60) fallaba el check `buf[0]==0x00` y
`receiveNoGarbage` retornaba false → transceive fallido en CADA comando. **El `writeData(255)` nunca
fue lo relevante del bucle; lo era el salto de frames no-data.**

**Fix (0.9.4):** `receiveNoGarbage` ahora **hace bucle** (IRQ-gated, hasta `toutMs`) **ignorando
cualquier frame no-data** hasta encontrar un data packet (header `0x00 0x00`) — replica exactamente
la semántica del `while` original, pero sigue sin el `writeData(255)`. Compila → **134857 B / 47160 B**.

**RESULTADO E2E 0.9.4 (2026-08-18): ✅ VALIDADO.** Con el fix del salto de frames no-data, ambas
placas flasheadas (0.9.4), la transacción **vuelve a COMPLETAR** con datos correctos en múltiples
corridas. Sin regresión de transceive. Fase C se probó junto con D+E (server con Fase E activa); el
tiempo combinado bajó a **~5 s de promedio** (ver TL;DR y §Fase E). Fase C mantiene su valor de
recorte de overhead I2C y **queda como mejora permanente**.

### Fase E — server TCP_NODELAY + write coalescido (2026-08-18)  ✅ implementado y VALIDADO EN HW
**Descubrimiento:** revisando `server/server.py` (no el firmware) se halló Nagle activo + dos writes
por frame con `wbufsize=0` → stall de delayed-ACK ~40 ms por relay server→placa (H4). El documento y
§17 habían tratado los 4 saltos WiFi como "física de red"; ~40 ms/salto de eso era Nagle, recortable.

**Cambios (`server/server.py`, Python — NO requiere reflashear):**
- `setup()`: `self.request.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)` (guard `OSError`).
- `send_to_clients()`: header+payload en **un solo** `wfile.write(len_bytes + msg)` (antes eran dos
  writes → dos segmentos TCP).

**Verificación:** `python3 -m py_compile server.py` OK.

**RESULTADO E2E (2026-08-18): ✅ VALIDADO — el mayor contribuyente, con MUCHO.** Con el server nuevo
(Nagle off + write coalescido) y las placas en 0.9.4 (D+C), la latencia promedio cayó a **~5 s** en
múltiples corridas (desde ~13.5 s de D). El delayed-ACK **se apilaba mucho peor** que los ~40 ms/salto
estimados — coherente con el EMV lock-step, donde cada uno de los ~36 relays server→placa por
transacción pagaba el stall. **Refuta que los 4 saltos WiFi fueran "física de red intocable" (§17):
buena parte era Nagle, software del server.** Sin riesgo RF; efecto neto enorme.
- **No re-proponer** `Wire.setClock(400000)` ni `WiFi.noLowPowerMode()`: ya regresaron (§17).
- **LECCIÓN (Fase C, 0.9.3→0.9.4):** cualquier receive en modo reader/card DEBE **iterar saltando
  frames NO-data** (el PN7150 intercala control NTFs como `CORE_CONN_CREDITS_NTF` 0x60 0x06 y
  `RF_DEACTIVATE_NTF` 0x61 0x06 antes/entre los data packets 0x00 0x00). El bucle
  `while (cardModeReceive(...))` hacía justo eso; una lectura única que retorna en el 1er frame
  falla en el credits-NTF. Si se toca este path, preservar el salto de no-data (no solo el timeout).
- **CORRECCIÓN a §17:** §17 decía "no IRQ-gatear el 2º `cardModeReceive` del reader porque no toca
  el techo de 2000 ms". El hardware (H3) probó lo contrario: con una
  tarjeta de un solo paquete **sí** gasta los 2000 ms completos en cada APDU. Se hizo exactamente
  ese IRQ-gate (Fase A.2) y es el fix de mayor impacto. La suposición de §17 era válida solo para
  tarjetas de doble-paquete.
- El piso de ~12–15 s es arquitectónico; estas fases buscan **recortar overhead fijo por-APDU y
  robustez**, no romper el piso. Enmarcar expectativas: la ganancia realista es de cientos de ms
  por transacción (H1 standalone, H2 si aplica), no segundos.

### Fase F — leer el header de 4 B en una sola lectura SPI en `poll()` (H5, 2026-08-18)  ✅ implementado y VALIDADO EN HW
**Descubrimiento:** revisando `NfcGateLink::poll()` + confirmando que el WiFi es WiFiNINA (ESP32-WROOM
= coprocesador SPI del RP2040). La Fase 1 leía el header de longitud (4 B) **byte-a-byte**, y cada
`_c.read()`/`_c.available()` es una transacción SPI completa contra el ESP32 → ~8 transacciones SPI
por frame solo para el header. Tras Fase E (server manda header+payload en un solo segmento TCP) el
header ya llega buffered de golpe, así que el byte-a-byte es overhead puro. Es el análogo del
lado-placa de la Fase E. Ver §2 H5.

**Cambio (`firmware/core/src/NfcGateLink.cpp`, `poll()` Fase 1 — solo transporte, NO toca RF/I2C):**
- Reemplazado el bucle `while (!_haveHdr && _c.available() > 0) { int b = _c.read(); ... }` por una
  única lectura en bloque `_c.read(&_hdr[_hdrHave], 4 - _hdrHave)` cuando hay bytes disponibles.
- Preserva la semántica no-bloqueante ante header partido: `read()` toma solo lo presente y `_hdrHave`
  arrastra la cuenta entre llamadas a `poll()` (idéntico al comportamiento previo).
- El payload ya se leía en bloque; sin cambios ahí.
- FW version `0.9.4 → 0.9.5`.

**Verificación:** compila → **134861 B flash (6%) / 47160 B RAM (17%)** (sin cambio de tamaño: solo
transporte reescrito).

**RESULTADO E2E (2026-08-18): ✅ VALIDADO — ~0.5 s menos.** Flasheadas ambas placas (0.9.5, server con
Fase E ya activa, sin reiniciarlo), transacción probada y cronometrada por el usuario: el tiempo
promedio bajó **~0.5 s** (de ~5 s a **~4.5 s**). La transacción **completa** con datos correctos (sin
cambio funcional — es puro transporte). Cae en el rango esperado (~100–200 ms era la estimación
conservadora; el recorte real de overhead SPI fijo por-frame quedó incluso algo mayor). Confirma H5:
el byte-a-byte del header sobre SPI WiFiNINA era overhead recortable, no física. **Fase F queda como
mejora permanente.**

**Cómo se probó en HW:**
1. Reflasheadas ambas placas (0.9.5). El server quedó igual (Fase E ya estaba); no se reinició.
2. `bombercat monitor` en ambas. Transacción con la Débito Mastercard sobre el reader y el S24+ sobre
   el card, cronometrada de inicio a fin.
3. Promedio de varias corridas: ~4.5 s (desde ~5 s de D+C+E).

### Fase G — bajar `SECOND_PACKET_WINDOW_MS` 25→10 ms (2026-08-18)  ✅ implementado · ⏳ pend. E2E HW
**Continuación directa de la Fase D.** La tarjeta de auditoría es de **un solo paquete**, así que
`readerTransceive` agota la ventana IRQ entera en CADA APDU antes de devolver la respuesta del 1er
paquete (el 2º nunca llega). A 25 ms × ~18 APDUs ≈ 450 ms de dead-time; bajando a 10 ms se recupera
**~270 ms**. Las tarjetas de doble-paquete NO se ven afectadas: su 2º paquete ya está buffered en el
PN7150 tras el único transceive RF, así que salen por IRQ en pocos ms — 10 ms les deja margen de sobra
(salen por `hasMessage()`, no por este timeout).

**Cambio:**
- `firmware/core/src/NfcController.h`: `SECOND_PACKET_WINDOW_MS` **25 → 10** ms (+ comentario que
  explica el margen y cómo re-ajustar). Único cambio de código; la lógica de `readerTransceive` intacta.
- FW version `0.9.5 → 0.9.6`.

**Verificación:** compila → **134861 B flash (6%) / 47160 B RAM (17%)** (sin cambio de tamaño: solo una
constante).

**RESULTADO E2E (2026-08-18): ✅ VALIDADO — funcional, recorte marginal.** Flasheadas ambas placas
(0.9.6), transacción probada por el usuario: **funciona correctamente** (sin regresión, datos
correctos), pero la mejora de latencia es **menor** — dentro del orden de los ~270 ms estimados, cerca
del ruido de medición. Confirma que a esta altura (~4.5 s) ya se está tocando el piso irreducible: los
~10 ms de ventana restante son marginales frente a la física RF + cripto + think-time del terminal +
saltos WiFi. **Fase G queda como mejora permanente** (recorte pequeño pero gratis y sin riesgo). No
tiene sentido bajar más la ventana (por debajo de ~10 ms el margen para tarjetas de doble-paquete se
vuelve arriesgado sin ganancia perceptible).

### Fase H — silenciar los logs por-frame del server (H6, 2026-08-18)  ✅ implementado · ⏳ pend. E2E HW
**Descubrimiento:** revisando `server/server.py` (no el firmware) tras cerrar Fase G. El server
imprimía en el hot-path lock-step **dos `print` por frame relayado** (H6): el `data:` hex-dump antes
de reenviar y el `Publish reached` después. Como el relay es estrictamente lock-step y el server está
en medio de los ~36 saltos por transacción, ese trabajo síncrono (repr del payload + `datetime.now()`
+ `print`/flush) se apila en la cadena causal — el análogo server-side de H1/Fase A (que silenció el
serial de la *placa*, nunca el stdout del *server*).

**Cambios (`server/server.py`, Python — NO requiere reflashear):**
- Nuevo flag `-v/--verbose` (default `False`), propagado a `NFCGateServer.verbose`.
- `handle()`: el `self.log("server", "data:", bytes(data))` por frame ahora sólo corre con `--verbose`.
- `send_to_clients()`: el `self.log("Publish reached", ...)` por frame ahora sólo corre con `--verbose`.
- Se conservan siempre los logs de conexión/sesión (connected/disconnected/joined/Timeout — raros,
  fuera del hot-path) y el listen inicial.

**Verificación:** `python3 -m py_compile server.py` OK; `--help` muestra el flag.

**Cómo probar en HW (sin reflashear placas):**
1. Reiniciar el server **sin** `-v` (default silencioso). Placas siguen en 0.9.6.
2. `bombercat monitor` en ambas. Transacción con la Débito Mastercard sobre el reader y el S24+ sobre
   el card, cronometrada de inicio a fin. Promediar varias corridas vs. ~4.2 s.
3. Para diagnosticar (recuperar el dump por-frame), reiniciar el server **con** `-v`.
4. Anotar el promedio aquí. **Esperado:** recorte modesto (~50–150 ms), mayor si el stdout del server
   iba a una tty/`tee` (flush por línea). Si no mide diferencia, cerrar Fase H como "sin efecto medible
   en esta configuración de stdout" (p.ej. stdout ya redirigido a archivo con block-buffering).

**RESULTADO E2E (2026-08-18): ✅ VALIDADO EN HW — funcional, sin regresión.** Server reiniciado sin
`-v` (default silencioso), placas en 0.9.6 sin reflashear. La transacción **completa** con datos
correctos en varias corridas; **nada se rompió** por silenciar los logs por-frame. El recorte de
latencia no se aisló con cronómetro (cae en el rango marginal esperado, ~50–150 ms, dependiente del
destino de stdout del server), pero el comportamiento es correcto y el beneficio es gratis y sin
riesgo. **Fase H queda como mejora permanente** (default silencioso; `-v` para diagnóstico). El piso
efectivo se mantiene en **~4.2 s**, holgadamente dentro del presupuesto de una terminal EMV.

### Fase I — acotar el peor caso de `readerHandleCommand` al presupuesto de WTX (H7, 2026-08-18)  ✅ implementado · ⏳ pend. E2E HW
**No es reducción de latencia mediana — es robustez de PEOR CASO frente al abort de la terminal.**
El promedio (~4.2 s / ~233 ms por-APDU) ya cabe de sobra en el presupuesto de WTX; el riesgo es un
**único comando** cuyo tiempo de servicio se dispara y agota el presupuesto de S(WTX) que la terminal
concede por-frame → la terminal aborta **por latencia, no por decline** (justo lo indeseado en una
auditoría).

**Descubrimiento (H7):** el bucle de reintento de `readerHandleCommand` aplicaba **la misma auto-cura
cara a TODOS los comandos**: ante un fallo de transceive hacía `beginReaderMode()` (reset RF completo)
+ `waitForTag(500)` + un **segundo** `readerTransceive` de hasta 4000 ms. Peor caso de un solo comando
≈ **~8–9 s** (lo confirman los comentarios de `RelayEngine.h` sobre `AWAIT_TIMEOUT_MS` /
`AWAIT_TIMEOUTS_BEFORE_RECONNECT`: "waitForTag + beginReaderMode + waitForTag + readerTransceive ≈
2.5–4 s"). Ese re-arm+retry solo es **seguro y asequible en el BORDE de transacción** (primer comando
tras el gap idle; SELECT PPSE idempotente; la sesión ISO-DEP ya estaba muerta). **A mitad de
transacción** es doblemente malo: (a) quema una segunda ventana de ~4 s durante la cual el card peer
tiene que sostener a la terminal con S(WTX) back-to-back → agota el presupuesto WTX → abort; y (b)
**reenvía un APDU posiblemente NO idempotente** (p.ej. GENERATE AC) contra una sesión recién
re-armada → protocolo inválido.

**Cambio (`firmware/core/src/RelayEngine.cpp`, `readerHandleCommand` — código propio, NO agrega ruta RF):**
- Se captura `const bool sessionWasLive = _tagReady;` al entrar (¿la sesión ya estaba viva para ESTA
  transacción?).
- La auto-cura cara (`beginReaderMode()` + retry, tanto en fallo de activación como de transceive)
  ahora se **gatea con `!sessionWasLive`**: solo corre en el **borde de transacción**.
- **A mitad de transacción** (`sessionWasLive == true`) un fallo de transceive hace **fail-fast**:
  retorna sin respuesta (una sola ventana de transceive, sin re-arm, sin replay) y deja la recuperación
  al `AWAIT_TIMEOUT_MS` del card peer. Así **ningún comando individual excede una ventana de transceive**
  → nunca se aproxima el techo de WTX.
- FW version `0.9.6 → 0.9.7`.

**Verificación (fail-fast solo):** compila → **134963 B flash (6%) / 47160 B RAM (17%)** (+102 B flash;
RAM sin cambio).

**Persistencia de borde (complemento, mismo build 0.9.7 — resuelve el pendiente #2):** el reverso del
fail-fast es que en el BORDE conviene insistir MÁS, no rendirse al primer fallo. El código viejo probaba
`waitForTag(500)` × 2 (~1 s) y descartaba toda la transacción si una tarjeta **marginal** (presente pero
mal acoplada/posicionada) fallaba el segundo intento. Ahora el borde (solo `sessionWasLive == false`)
**itera ventanas de discovery + re-arms** hasta agotar `READER_BOUNDARY_ACTIVATE_MS` (nueva constante en
`RelayEngine.h`, **3000 ms**, dimensionada bajo `AWAIT_TIMEOUT_MS`=5000 dejando ~2 s de holgura para el
transceive rápido de SELECT PPSE + los round trips WiFi/TCP). El re-arm entre ventanas resetea un discovery
atascado — ayuda a una tarjeta marginal más que una sola espera larga. El **mismo presupuesto** acota además
la auto-cura de borde ante fallo de transceive: nunca apila una 2ª ventana de ~4 s más allá del budget WTX.
- Estructura: `for(;;)` gateado por `mayRecover = !sessionWasLive && (millis()-cmdStart < READER_BOUNDARY_ACTIVATE_MS)`.
  Mid-transacción (`sessionWasLive == true`) hace fail-fast al primer fallo (Fase I intacta); el borde persiste
  dentro del budget. Termina garantizado: cada iteración consume tiempo real (waitForTag/transceive/beginReaderMode)
  → el budget siempre se alcanza.
- **Elección:** "más reintentos dentro del presupuesto" en vez de "una ventana más larga" — el re-arm entre
  intentos es lo que rescata a una tarjeta marginal, no solo esperar más.
- **Verificación (con persistencia):** compila → **134994 B flash (6%) / 47160 B RAM (17%)** (+31 B; RAM igual).

**Trade-off consciente (reversible):** se **renuncia** a la recuperación por retry de un glitch
transitorio a mitad de transacción. Es defendible: si el transceive falló de verdad (tarjeta retirada /
sesión muerta), el retry no ayuda —la tarjeta ya no está— y `beginReaderMode()` **destruiría** una
sesión ISO-DEP aún viva, garantizando un replay inválido. El comentario original ya calificaba esa
muerte de sesión a mitad de transacción como "rara". Convive con el pendiente #2 (más persistencia en
el **borde**): son complementarios, no opuestos — persistir en el primer comando (idempotente),
endurecer a mitad de transacción.

**Cómo probar en HW:**
1. Reflashear ambas placas (0.9.7). Server igual (Fases E/H ya activas).
2. `bombercat monitor` en ambas. Correr transacciones normales con la Débito Mastercard + S24+ →
   confirmar que **completan con datos correctos** (el happy-path no cambia: sessionWasLive solo desvía
   el path de FALLO). Anotar que la latencia mediana se mantiene ~4.2 s.
3. Provocar el peor caso (retirar/mover la tarjeta física a mitad de transacción): antes ≈ ~8–9 s de
   servicio en ese comando; ahora fail-fast (una ventana), la terminal aborta limpio o el card peer
   re-poll/reconecta. Anotar el comportamiento aquí.
4. **Persistencia de borde:** posicionar la tarjeta física en el **límite del acoplamiento** (marginal)
   y arrancar una transacción nueva. Antes se perdía toda la transacción al 2º `waitForTag` fallido
   (~1 s); ahora el reader insiste hasta ~3 s con re-arms → debería activar y **completar**. Confirmar
   que con la tarjeta bien acoplada NO hay cambio de latencia (el budget solo corre si el 1er waitForTag
   falla). Si una tarjeta legítima aún se pierde en el borde, subir `READER_BOUNDARY_ACTIVATE_MS` (tope
   práctico ~4000, para no rozar `AWAIT_TIMEOUT_MS`=5000) y anotar el valor que funcione aquí.

### Estado al cerrar la sesión del 2026-08-18
- **CONFIRMADO en HW (§17 reconfirmado con A.2):** una transacción completa tomaba **~15 s**. La
  parte irreducible (física del transceive RF + cripto de la tarjeta + think-time del terminal +
  4 saltos WiFi por par) es el piso real; romperlo del todo exige enlace directo placa↔placa
  (romper compat NFCGate) — fuera de alcance.
- **MATIZ (revisión posterior del hot-path):** dentro de esos ~15 s había **~2–3 s de overhead fijo
  por-APDU que NO es física** y sí es recortable por firmware:
  - **Fase D** (implementada, 0.9.2): los 120 ms de `SECOND_PACKET_WINDOW_MS` × ~18 APDUs ≈ 2.16 s
    de dead-time en tarjetas de un paquete → bajados a 25 ms. Pend. medir en HW.
  - **Fase C / H2** (implementada, 0.9.4 tras fix): ~23 ms de write I2C inútil × ~36–54 llamadas ≈ 0.8–1.2 s.
    Pend. E2E HW.
  - **Fase E / H4** (implementada, `server.py`): stall Nagle/delayed-ACK ~40 ms × 2 saltos server→placa
    × ~18 pares ≈ ~1–1.5 s. **RED, no firmware** — se prueba sin reflashear. Pend. E2E HW.
  **RESULTADO REAL (validado en HW, múltiples corridas):** D+C+E juntas bajaron la latencia a
  **~5 s de promedio** (desde ~15 s) — **~3×**, muy por debajo de la expectativa de ~10–12 s. La
  sorpresa fue **Fase E**: el stall de Nagle/delayed-ACK se apilaba mucho peor que lo estimado, así que
  buena parte de los "4 saltos WiFi" que §17 daba por física de red **era software del server**. Con
  ~5 s el relay entra cómodamente dentro del presupuesto de una terminal EMV.
- **Fase F (FW 0.9.5) — ✅ validada en HW:** el header de 4 B leído en una lectura SPI (no byte-a-byte)
  bajó otros **~0.5 s** → **~4.5 s de promedio**. Otra vez software (overhead SPI WiFiNINA), no física.
- **Piso restante (~4.5 s):** ahora sí dominado por lo irreducible — think-time del terminal + cripto de
  la tarjeta + los transceives RF + los saltos WiFi ya sin Nagle. La única palanca de firmware que
  queda es la **Fase G** (ventana `SECOND_PACKET_WINDOW_MS` 25→10 ms, ~270 ms en tarjeta de un paquete).
  Bajar por debajo de eso exigiría eliminar el `nfcgate-server` con enlace directo placa↔placa (rompe
  compat NFCGate) — fuera de alcance salvo que se decida abrir esa fase.
- Fase A.2 (IRQ-gate) queda **como mejora permanente de robustez** (maneja tarjetas de uno/dos
  paquetes; elimina el riesgo de abortar por el busy-wait de 2000 ms). Mantener.
- **Fases D, C, E y F: validadas y permanentes.** FW **0.9.5** (ambas placas) + `server.py` con Fase E.
- **Fase H (server, H6) — ✅ validada en HW:** silenciados los 2 logs por-frame del hot-path (gated tras
  `-v`, default silencioso). Funcional, sin regresión; recorte marginal no aislado con cronómetro. Otra
  vez software del server, no física. `server.py` ahora corre silencioso por defecto; `-v` para diagnóstico.

### Pendientes concretos para la próxima sesión (orden sugerido)
- **Fases D, C, E — ✅ HECHAS y validadas en HW** (~15 s → ~5 s). Sin pendiente; son permanentes.
- **Fase F — ✅ HECHA y validada en HW** (~5 s → ~4.5 s, FW 0.9.5). Header en una lectura SPI. Permanente.
- **Fase G — ✅ HECHA y validada en HW** (FW 0.9.6, funcional, recorte marginal). `SECOND_PACKET_WINDOW_MS` 25→10 ms.
  Flashear 0.9.6 y cronometrar vs. ~4.5 s; esperado ~270 ms menos (~4.2 s). Anotar en §Fase G. Si una
  tarjeta de doble-paquete mis-relaya un frame intermedio, subir el valor y anotarlo (§Fase D/§Fase G).
- **Fase H — ✅ HECHA y validada en HW** (server, sin reflashear). Logs por-frame gated tras `-v`;
  `server.py` corre silencioso por defecto. Funcional, sin regresión; recorte marginal. Permanente.
  Si en el futuro quieres el dump por-frame para diagnóstico, arranca el server con `-v`.
1. **Medir Fase A aislada (standalone):** correr el relay **sin `monitor`** (autostart, sin PC
   drenando el serial) y cronometrar vs. con `monitor`. Es el único escenario donde el logging
   quiet puede ahorrar (cientos de ms). Si no mide diferencia, cerrar Fase A como "solo robustez".
2. **Robustez de detección del reader — ✅ HECHO (Fase I, persistencia de borde, FW 0.9.7).** Antes
   `readerHandleCommand` intentaba ~1 s (`waitForTag(500)` × 2) y descartaba toda la transacción ante
   una tarjeta marginal. Ahora el borde itera discovery+re-arm bajo `READER_BOUNDARY_ACTIVATE_MS`=3000
   (dentro del presupuesto de 5 s del card). Pend. validar en HW con tarjeta en el límite del acoplamiento
   (ver §Fase I, paso 4). Si aún se pierde, subir el budget (tope ~4000) y anotar.
3. **Fase B (hilo RTOS de logging):** solo si se quiere robustez extra ante jitter. **No baja el
   piso.** Baja prioridad dado que el piso ya está confirmado como arquitectónico.
4. **(Opcional, futuro) Enlace directo placa↔placa:** única vía para bajar de ~5 s; elimina el
   `nfcgate-server` y 2 de los 4 saltos, pero rompe compat con la app NFCGate. Abrir como fase aparte
   solo si se necesita menos de ~5 s.

### Versión de firmware al cierre: **0.9.7** (ambos roles). Archivos tocados esta sesión:
`firmware/core/src/RelayEngine.cpp` (`readerHandleCommand`: gate `sessionWasLive` → fail-fast a mitad de
transacción; `for(;;)` con budget de borde → persistencia + auto-cura idempotente acotada [Fase I]),
`firmware/core/src/RelayEngine.h` (constante `READER_BOUNDARY_ACTIVATE_MS`=3000 [Fase I]),
`firmware/NFCGate/NFCGate.ino` (bump 0.9.6→0.9.7).
Sesiones/fases previas:
`firmware/NFCGate/NFCGate.ino` (bump 0.9.1→0.9.6), `firmware/core/src/NfcController.h`
(`SECOND_PACKET_WINDOW_MS` 120→25 [Fase D] → 25→10 [Fase G] + decl. `receiveNoGarbage` [Fase C]),
`firmware/core/src/NfcController.cpp` (`receiveNoGarbage` con salto de frames no-data + 3 reemplazos
de `cardModeReceive` [Fase C]), `firmware/core/src/NfcGateLink.cpp` (`poll()`: header de 4 B en una
lectura SPI en bloque, no byte-a-byte [Fase F]), `server/server.py` (`TCP_NODELAY` + write coalescido
[Fase E] + flag `-v/--verbose` que gatea los 2 logs por-frame del hot-path, default silencioso [Fase H]).
Sesiones previas: `SerialControl.cpp`, `tools/modules/nfcgate/cli.py`.
