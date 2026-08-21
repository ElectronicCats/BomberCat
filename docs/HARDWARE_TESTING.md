# Prueba en hardware — NFCGate relay (Fase 7)

Guía para probar el relay NFCGate en placa real, **desde cero**: cómo flashear con
[`scripts/flash_bombercat.sh`](../scripts/flash_bombercat.sh) y luego dos caminos
completos, explícitos y paso a paso:

- **Camino A —** relay con **dos BomberCat**.
- **Camino B —** relay con **una BomberCat + la app Android NFCGate** como el otro extremo.

> Estado (2026-08-17): **probado en hardware real (Camino A, dos BomberCat), con
> transacciones consecutivas.** El relay cursa transacciones EMV completas de
> extremo a extremo, una tras otra sin colgarse (~12 s cada una; mantén el terminal
> sobre el `card` ese tiempo). Débito Mastercard real sobre el `reader`, teléfono
> con app lectora EMV sobre el `card`. Dos bugs de la ruta RF ya se corrigieron: la
> activación/re-arme de la emulación
> (resuelto) y el enlace TCP half-open que colgaba la 2ª transacción
> (corregido — reflashea el firmware para tenerlo). **Reflashea siempre la última
> versión** antes de probar. Si algo no cuadra, captura la salida de
> `bombercat monitor` para depurar. *(Camino B contra la app NFCGate Android:
> **variante B2** —BomberCat `card` + móvil lector— **probada OK**; **variante B1**
> —BomberCat `reader` + móvil emulador— **probada OK con un móvil ROOTEADO**
> (2026-08-19, Motorola Edge 20 Pro): imposible en móvil stock, requiere root +
> Xposed + hook nativo de NFCGate..)*

---

## Parte 0 — Flashear el firmware (con `flash_bombercat.sh`)

Un solo sketch sirve para **ambos roles**: **`firmware/NFCGate/`**. El rol (reader
o card) se elige luego por CLI, **no recompilando**. Flashea el mismo firmware en
cada BomberCat que uses (en el Camino B, solo en tu única placa).

El script [`scripts/flash_bombercat.sh`](../scripts/flash_bombercat.sh) hace todo el
trabajo pesado: instala `arduino-cli` si falta, el core
`electroniccats:mbed_rp2040`, las librerías necesarias, compila y sube el
firmware (por puerto serie o copiando el `.uf2` si la placa está en modo
bootloader).

### 0.1 — Primera vez: preparar el toolchain

Desde la **raíz del repo**:

```sh
chmod +x scripts/flash_bombercat.sh     # solo la primera vez, si hace falta
./scripts/flash_bombercat.sh --setup    # instala core + librerías y termina
```

En **Linux**, el `--setup` te imprimirá un comando `sudo …/post_install.sh` para
las reglas udev del bootloader RP2040. Ejecútalo si aún no lo has hecho. Además,
tu usuario debe estar en el grupo `dialout` para abrir el puerto serie sin sudo:

```sh
sudo usermod -aG dialout $USER   # luego cierra sesión y vuelve a entrar
```

### 0.2 — Flashear la placa

Conecta **una** BomberCat por USB (flashea de una en una) y ejecuta:

```sh
./scripts/flash_bombercat.sh -f NFCGate          # compila y flashea NFCGate
./scripts/flash_bombercat.sh -f NFCGate -m       # …y abre el monitor serie al terminar
```

El script detecta la placa solo:

- Si aparece como puerto serie normal (`/dev/ttyACM0`), hace el reset a 1200 bps y sube el binario.
- Si la pones en **modo bootloader** (doble reset → aparece la unidad `RPI-RP2`), copia el `.uf2`.
- ¿No la detecta? Fuerza el puerto: `./scripts/flash_bombercat.sh -f NFCGate -p /dev/ttyACM0`.

Comandos útiles del script:

```sh
./scripts/flash_bombercat.sh -l          # lista los firmwares disponibles
./scripts/flash_bombercat.sh -c -f NFCGate   # solo compila (no sube)
./scripts/flash_bombercat.sh -h          # ayuda completa
```

> **Camino A (dos placas):** repite el paso 0.2 con cada BomberCat, una a la vez.
> Si conectas ambas al mismo tiempo, pasa el puerto explícito con `-p` para no
> flashear la equivocada.

> El control corre a **115200 baud** y un solo programa puede tener el puerto
> abierto. **Cierra el Serial Monitor del IDE** (y el `-m` del script) mientras
> uses el CLI, o dará "port busy".

### 0.3 — Comprobar que firmware y CLI se hablan

Con tu venv de Python activado:

```sh
cd tools
pip install -r requirements.txt        # trae pyserial (solo la primera vez)
python3 bombercat.py device list       # debe aparecer tu puerto con ✓
python3 bombercat.py device info       # -> fw 0.7.0, role reader, state idle
```

Si ves `fw 0.7.0` y `state idle`, el plano de control funciona. (Puedes usar el
atajo `bombercat …` en vez de `python3 bombercat.py …` si lo tienes instalado.)

---

## Parte 1 — Requisitos comunes a ambos caminos

Da igual el camino que elijas: siempre necesitas **un servidor NFCGate** y que
ambos extremos compartan **servidor + sesión**.

### 1.1 — Levantar el servidor NFCGate

En una máquina de tu red (puede ser tu propia PC):

```sh
cd tools
python3 bombercat.py testserver run    # nfcgate-server local en :5566 (Docker)
```

El servidor escucha en el puerto **5566** de **esa** máquina. La IP que configures
luego con `--server` **debe ser la IP real de la máquina donde corriste
`testserver run`** — no `localhost`, no `127.0.0.1`: la placa se conecta por WiFi
y necesita una IP alcanzable en la LAN.

Averigua esa IP en la misma máquina donde levantaste el servidor:

```sh
hostname -I            # Linux -> p. ej. "192.168.1.5 172.17.0.1"
```

`hostname -I` puede imprimir **varias** direcciones. Quédate con la de tu red
local (la del mismo rango que tu router, típicamente `192.168.x.x` o `10.x.x.x`);
ignora las de interfaces virtuales como `172.17.x.x` (Docker) o `127.0.0.1`.

Con esa IP construyes el valor de `--server` — y **el mismo valor** va en la app
Android del Camino B:

```sh
--server 192.168.1.5:5566      # <IP de hostname -I>:5566
```

> Si la máquina cambia de red (o el router le da otra IP por DHCP), **vuelve a
> correr `hostname -I`** y reconfigura las placas con `config nfcgate --server …`:
> una IP vieja es la causa más común de que `run` no conecte.

### 1.2 — Reglas de oro del emparejamiento

- **WiFi 2.4 GHz obligatorio** — el módulo NINA-W102 de la BomberCat **solo opera
  en 2.4 GHz**; no soporta 5 GHz (ni 6 GHz). Si tu router publica una única red
  con "band steering" (2.4 y 5 GHz bajo el mismo SSID), separa las bandas en el
  router o crea un SSID exclusivo de 2.4 GHz para las placas. Un SSID de 5 GHz
  simplemente **no aparecerá** para la placa y `run` fallará con timeout de WiFi.
- **`--server` = IP de `hostname -I`** en la máquina del `testserver run` (Paso
  1.1), nunca `localhost`/`127.0.0.1`.
- **Mismo `--server`** (host:puerto) en los dos extremos.
- **Mismo `--session`** (1..255) en los dos extremos — si difiere, nunca hará `peer=yes`.
- **Roles opuestos**: un extremo `reader`, el otro `card`.

---

## Camino A — Relay con DOS BomberCat

Dos placas ya flasheadas (Parte 0) y el servidor arriba (Parte 1). Una placa hará
de **reader** (con una tarjeta física en su campo NFC) y la otra de **card**
(frente a un terminal/PoS).

Haz cada bloque de config **en la placa correspondiente** (conecta una a la vez, o
usa `--port` para dirigirte a cada una).

### A.1 — Configurar la placa READER

```sh
cd tools
python3 bombercat.py config wifi    --ssid "TuWiFi" --pass "TuClave"
python3 bombercat.py config nfcgate --server 192.168.1.5:5566 --session 42 --role reader
python3 bombercat.py config show    # verifica que quedó guardado en flash
```

### A.2 — Configurar la placa CARD

Mismo servidor y **misma sesión (42)**, rol opuesto:

```sh
python3 bombercat.py config wifi    --ssid "TuWiFi" --pass "TuClave"
python3 bombercat.py config nfcgate --server 192.168.1.5:5566 --session 42 --role card
python3 bombercat.py config show
```

### A.3 — Arrancar ambos extremos

En **cada** placa:

```sh
python3 bombercat.py run        # asocia WiFi, conecta al server, hace OP_SYN
python3 bombercat.py status     # busca: state=relaying, link connected=yes, peer=yes
```

Cuando `status` muestre **`peer=yes` en ambas**, los dos extremos se emparejaron.

### A.4 — Pasar tarjeta y observar el relay

1. Presenta la **tarjeta física** al campo NFC de la placa **reader**.
2. Acerca la placa **card** al **terminal/PoS**.
3. Observa el cruce de APDUs en vivo:

```sh
python3 bombercat.py monitor    # cmd:/resp: al pasar tarjeta ↔ terminal
```

El contador `relayed` de `status` debe subir y `monitor` mostrará los APDUs
cruzando entre los dos extremos.

---

## Camino B — Relay con UNA BomberCat + la app NFCGate (Android)

Aquí tu **única** BomberCat es un extremo del relay y la **app NFCGate** en un
móvil Android es el otro. Es la prueba de compatibilidad "fuerte": valida que
nuestro firmware habla el mismo protocolo que el cliente oficial de NFCGate.

Requisitos extra: un teléfono **Android con la app NFCGate** instalada y con NFC,
en la **misma red** que el servidor.

### Dos variantes según quién emula

| Variante | BomberCat | Móvil | Requiere root en el móvil |
|---|---|---|---|
| **B2** | `card` (emula frente al terminal) | **lector** de la tarjeta física | **No** — el modo lector no usa HCE |
| **B1** | `reader` (lee la tarjeta física) | **emulador** frente al terminal | **Sí** — ver B.0 |

> **¿Por qué B1 necesita root?** La HCE de Android solo entrega los APDU a una app
> que haya **registrado el AID** que pide el terminal, y la app NFCGate solo
> registra un AID de prueba (`F0010203040506`), **no** el PPSE ni AIDs de pago. Por
> eso en un móvil **stock** el `SELECT PPSE` del terminal nunca llega a NFCGate y no
> fluye nada (empareja pero cero APDU). Para emular AIDs arbitrarios la app usa su
> camino nativo (`DaemonManager`), que exige **root + Xposed + hook `nfcd`
> parcheado**..

### B.0 — Preparar el móvil rooteado (SOLO variante B1)

Salta este paso si haces B2. Para B1:

1. Root en el teléfono (p. ej. Magisk).
2. Instala un framework **Xposed**: **LSPosed** (con Zygisk o Riru).
3. En LSPosed, **activa el módulo NFCGate** (`nfcd`) y dale *scope* al **servicio NFC
   del sistema** (`com.android.nfc`) además de la app. **Reinicia** el teléfono.
4. Abre NFCGate → **Status** y confirma **`Native Hook Enabled`** y que el chipset
   es NCI (NXP/Broadcom/ST). Si el hook no queda activo, B1 no funcionará.

> **Probado OK (2026-08-19):** Motorola Edge 20 Pro rooteado, con `Native Hook
> Enabled`, emulando frente al terminal mientras la BomberCat `reader` leía una
> tarjeta EMV real. La transacción cruzó completa. El firmware `reader` emite una
> trama **INITIAL** con la config de la tarjeta (UID/SAK/ATQA/ATS) para que el móvil
> configure su HCE nativa y presente el tag.

### B.1 — Configurar la BomberCat

Elige el rol de la placa según qué quieras que capture. Ejemplo con la placa como
**reader** (captura la tarjeta física) y el móvil como el extremo emulador:

```sh
cd tools
python3 bombercat.py config wifi    --ssid "TuWiFi" --pass "TuClave"
python3 bombercat.py config nfcgate --server 192.168.1.5:5566 --session 42 --role reader
python3 bombercat.py config show
```

> Si prefieres que el **móvil** capture la tarjeta y la **placa** emule frente al
> terminal, pon `--role card` aquí y elige el rol opuesto en la app.

### B.2 — Configurar la app NFCGate

En la app Android:

1. Ajustes → **servidor**: la **misma IP y puerto** del Paso 1.1 (`192.168.1.5:5566`).
2. **Sesión**: el **mismo número** que la placa (`42`).
3. Modo/rol: el **opuesto** al de la placa. Si la placa es `reader`, el móvil hace
   de extremo de **emulación/relay** (el que presenta al terminal); si la placa es
   `card`, el móvil hace de **captura de tarjeta**.

### B.3 — Arrancar la BomberCat y unir el móvil a la sesión

En la placa:

```sh
python3 bombercat.py run
python3 bombercat.py status     # espera: link connected=yes
```

Luego, en la app, **inicia la sesión** con ese mismo número. Cuando ambos extremos
estén en la sesión, la placa reportará **`peer=yes`**:

```sh
python3 bombercat.py status     # peer=yes cuando el móvil se unió
python3 bombercat.py monitor    # stream en vivo de APDUs
```

### B.4 — Pasar tarjeta y observar

- Presenta la tarjeta física al extremo **reader** (la placa o el móvil, según lo configurado).
- Acerca el extremo **card/emulador** al terminal/PoS.
- En `monitor` verás los APDUs cruzando (`cmd:` / `resp:`) y `relayed` subiendo.

> **En B1 (placa `reader`)**, con la tarjeta ya sobre la placa, el monitor debe
> pasar de `reader: ...enviando trama INITIAL (coloca la tarjeta...)` a
> **`reader: trama INITIAL (config de tag) enviada al peer`**. Esa línea es la señal
> de que el móvil recibió la config y empezará a presentar el tag al terminal. Si se
> queda en `enviando trama INITIAL`, no hay tarjeta acoplada al `reader`.

---

## Qué observar / posibles tropiezos

| Síntoma | Causa habitual |
|---|---|
| El script no detecta la placa al flashear | Ponla en modo bootloader (doble reset → unidad `RPI-RP2`) o pasa `-p /dev/ttyACM0`. |
| `run` falla con timeout de WiFi | La red es de **5 GHz**: el NINA solo hace 2.4 GHz. Usa un SSID de 2.4 GHz. También revisa SSID/clave con `config show`. |
| `run` conecta al WiFi pero no al servidor | `--server` apunta a una IP equivocada. Corre `hostname -I` en la máquina del `testserver run` y reconfigura con esa IP:5566 (nunca `localhost`). |
| `run` falla | WiFi (SSID/clave, red 2.4 GHz) o `--server` inalcanzable. Revisa `config show`; `monitor` muestra el motivo (timeout WiFi, connect failed). |
| `peer=no` persistente | Los dos extremos no comparten `--session` o no apuntan al mismo servidor. En el Camino B, revisa que la app use la misma IP:puerto y sesión. |
| **B1: `peer=yes` pero cero APDU** (empareja y no fluye nada) | El móvil no está emulando el AID de pago. En **stock es imposible** (usa B2). Con móvil rooteado, revisa B.0: `Native Hook Enabled` en la página Status, módulo NFCGate activo en LSPosed con scope al servicio NFC, y reinicio tras activarlo. |
| **B1: monitor atascado en `enviando trama INITIAL`** | No hay tarjeta física acoplada al `reader`, o mal posicionada. Colócala y mantenla sobre la placa. |
| "port busy" / no abre | El Serial Monitor del IDE (o el `-m` del script) tiene el puerto. Ciérralo. |
| No detecta la placa (CLI) | Pasa el puerto explícito: `--port /dev/ttyACM0`. En Linux, tu usuario debe estar en el grupo `dialout` (ver Paso 0.1). |

**Autostart:** con SSID configurado en flash, la placa arranca el relay sola al
bootear (`RELAY_AUTOSTART=1`). Si quieres que espere al CLI, haz `bombercat stop`
o pon `RELAY_AUTOSTART 0` en `arduino_secrets.h` antes de flashear.

---

## Referencia rápida

**Flasheo (`scripts/flash_bombercat.sh`):**

```sh
./scripts/flash_bombercat.sh --setup            # preparar toolchain (1ª vez)
./scripts/flash_bombercat.sh -l                 # listar firmwares
./scripts/flash_bombercat.sh -f NFCGate [-m]    # flashear NFCGate (+monitor)
./scripts/flash_bombercat.sh -f NFCGate -p /dev/ttyACM0   # forzar puerto
```

**CLI de control (`tools/bombercat.py`):**

```sh
bombercat device list | info                 # descubrir / inspeccionar
bombercat config wifi    --ssid --pass       # credenciales WiFi
bombercat config nfcgate --server host[:port] --session N --role reader|card
bombercat config show                        # config actual
bombercat testserver run                     # nfcgate-server local (Docker)
bombercat run | stop | status | monitor      # arrancar / parar / estado / stream
```

Todos aceptan `--port/-p` (autodetección por handshake si se omite) y
`--save/--no-save` en los `config` (por defecto persiste en flash).

Ver también: [`tools/README.md`](../tools/README.md),
[`firmware/NFCGate/README.md`](../firmware/NFCGate/README.md),
[`NFCGATE_PLAN.md`](NFCGATE_PLAN.md).
