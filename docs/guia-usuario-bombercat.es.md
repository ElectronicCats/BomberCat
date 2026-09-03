# Guía de Uso — BomberCat para Auditorías NFC

> **Read this in another language:** [English](guia-usuario-bombercat.en.md)

---

## A. Introducción y Advertencia Legal

### ¿Qué es BomberCat?

**BomberCat** es una herramienta de seguridad que trabaja con tarjetas sin
contacto (**NFC** — las mismas de pagos, control de acceso o identificación) y
con banda magnética. Con ella un auditor puede **leer, emular o retransmitir
(_relay_)** una tarjeta para comprobar si un sistema es vulnerable.

Esta guía cubre en concreto la función de **relay**: hacer que una tarjeta que
está en un lugar "aparezca" frente a un lector que está en otro, uniendo los dos
extremos por WiFi. Es la prueba clásica para demostrar si un lector acepta una
tarjeta que en realidad no está presente.

> [!WARNING]
> ## ⚠️ AVISO DE RESPONSABILIDAD — LÉELO ANTES DE USAR
>
> **BomberCat está diseñada EXCLUSIVAMENTE para auditorías de seguridad
> autorizadas y pruebas en entornos controlados.**
>
> - Úsala **solo** sobre sistemas, tarjetas y terminales que **te pertenecen** o
>   para los que tienes **permiso explícito y por escrito** del propietario.
> - Usar esta herramienta sobre tarjetas, cuentas, accesos o terminales de
>   terceros sin autorización es, en la mayoría de los países, un **delito**.
>
> > **El usuario es el único responsable del uso que dé al dispositivo y de las
> > consecuencias legales, éticas o de cualquier otra índole que pudieran
> > derivarse de su uso indebido.**
>
> Electronic Cats y los autores de este firmware **no se hacen responsables** de
> ningún uso no autorizado ni de los daños que de él se deriven. El dispositivo
> **no** te autoriza a usar tarjetas de crédito ni a realizar transacciones
> financieras no autorizadas legalmente.

**Antes de cada prueba, asegúrate de tener:**

- [ ] Autorización por escrito del propietario del sistema o de la tarjeta.
- [ ] Un alcance definido (qué se prueba, cuándo y dónde).
- [ ] Un entorno controlado, sin afectar a personas o servicios de terceros.

---

## B. Especificaciones y Modos de Uso

### ¿Qué hace falta siempre? (para los tres modos)

El relay siempre une **dos extremos** a través de un pequeño programa
intermediario llamado **servidor NFCGate**. Uno de los extremos actúa como
**lector** (lee una tarjeta real) y el otro como **tarjeta** (se presenta ante un
terminal). Da igual qué modo elijas: siempre necesitas estas tres piezas.

| Pieza | Para qué sirve |
|---|---|
| **Servidor NFCGate** | Un programa que corre en una PC y reenvía los datos entre los dos extremos. |
| **Red WiFi** | Los dos extremos y la PC del servidor deben estar en la **misma red WiFi**. |
| **Dos extremos** | Uno en modo *lector* y otro en modo *tarjeta* (según el modo elegido). |

**Instalación única del software de control (en tu PC, una sola vez):**

```sh
cd tools
python3 -m pip install -r requirements.txt   # instala las dependencias
python3 bombercat.py --help                  # comprueba que funciona
```

> En Linux, para que la PC "vea" la BomberCat por USB puede que necesites añadir
> tu usuario al grupo `dialout`:
> `sudo usermod -aG dialout $USER` y volver a iniciar sesión.

**Arrancar el servidor NFCGate (en la PC, antes de cualquier prueba):**

```sh
bombercat testserver run              # arranca el servidor, escucha en el puerto 5566
```

Déjalo corriendo en su propia ventana de terminal. Anota la **dirección IP** de
esa PC (por ejemplo `192.168.1.5`); la necesitarás para configurar los extremos.
En Windows/Mac/Linux puedes ver la IP con `ipconfig` o `ip a`.

> 💡 **En Linux**, la forma más rápida de saber la IP de la computadora que corre
> el servidor es el comando `hostname -I`: imprime directamente la(s) dirección(es)
> IP del equipo. La primera suele ser la de tu red WiFi (la que pondrás en
> `--server`, por ejemplo `192.168.1.5`).

**Conceptos clave que se repiten en todos los modos:**

- **Servidor (`--server`)**: la IP de la PC y el puerto, por ejemplo
  `192.168.1.5:5566`. **Los dos extremos deben apuntar al mismo servidor.**
- **Sesión (`--session`)**: un número del 1 al 255 que empareja los dos extremos.
  **Los dos extremos deben usar exactamente el mismo número de sesión.**
- **Rol (`--role`)**: `reader` (lee una tarjeta física) o `card` (se presenta como
  tarjeta ante un terminal). **Un extremo debe ser `reader` y el otro `card`.**

> ℹ️ **Sobre los tiempos:** una transacción por relay tarda típicamente
> **12–15 segundos**. Es normal por el camino WiFi + servidor; no significa que
> algo esté roto. Mantén la tarjeta y el terminal quietos durante ese tiempo.

---

### Preparación única: grabar el firmware de relay en la BomberCat

Cada BomberCat que vayas a usar como extremo (`reader` o `card`) necesita tener
grabado el **firmware de relay NFCGate**. Esto se hace **una sola vez** por
dispositivo (o cuando salga una versión nueva del firmware). Si compraste la
BomberCat con otro firmware, o no estás seguro, sigue estos pasos.

**Qué necesitas:**

- La BomberCat conectada por USB-C a tu PC.
- El [Arduino IDE](https://www.arduino.cc/en/software) 2.x **o** `arduino-cli`.
- El paquete de placas **Electronic Cats Mbed OS RP2040** y las librerías
  **WiFiNINA** y **Electronic Cats PN7150** (el Arduino IDE las instala desde su
  gestor de placas y de librerías).

**Con `arduino-cli` (lo más rápido):**

```sh
cd firmware/NFCGate
arduino-cli compile -b electroniccats:mbed_rp2040:bombercat --library ../core .
arduino-cli upload  -b electroniccats:mbed_rp2040:bombercat -p /dev/ttyACM0 .
```

> Sustituye `/dev/ttyACM0` por el puerto de tu BomberCat (en Windows será algo
> como `COM5`). Los avisos de WiFiNINA *"architecture may be incompatible"* son
> **normales** en la BomberCat, no son un error.

**Con el Arduino IDE:** abre `firmware/NFCGate/NFCGate.ino`, selecciona la placa
**"Electronic Cats BomberCat"** y, para que resuelva `#include <BomberCatCore.h>`,
crea un enlace de la carpeta `firmware/core` dentro de `~/Arduino/libraries`.
Después pulsa **Subir**.

**Comprueba que quedó grabado** (con el software de control ya instalado):

```sh
bombercat device info      # debe responder: fw 0.9.7, state idle
```

Si responde con la versión y `state idle`, la BomberCat está lista. Repite el
proceso en la **segunda** BomberCat si vas a usar el Modo 1.

> Los detalles técnicos completos (pines, librerías, opciones de compilación)
> están en [`firmware/NFCGate/README.md`](../firmware/NFCGate/README.md).

---

### Modo 1 — BomberCat Reader + BomberCat Card

**Propósito:** el modo más fiable y recomendado. Dos BomberCat: una lee una
tarjeta física y la otra la presenta a un terminal, en otro lugar, a través del
WiFi. Ideal para demostrar un relay de principio a fin sin depender de un
teléfono. *(Este modo está validado en hardware.)*

**Requisitos mínimos:**

- 2 dispositivos BomberCat con el firmware de relay NFCGate.
- 1 PC con el servidor NFCGate corriendo (paso anterior).
- 1 tarjeta física a leer y 1 terminal/lector contra el que probar.
- Todo en la misma red WiFi.

**Pasos:**

Conecta **ambas** BomberCat por USB a la misma PC. Cada una recibe un número
(`#1`, `#2`); puedes ver cuáles son con:

```sh
bombercat device list                # muestra las BomberCat conectadas
bombercat identify -d 1              # parpadea el LED de la #1 para reconocerla
```

1. **Configura el WiFi en las dos** (usa tu red real):
   ```sh
   bombercat config wifi -d 1 --ssid MiRedWiFi --pass 'miclave'
   bombercat config wifi -d 2 --ssid MiRedWiFi --pass 'miclave'
   ```
2. **Configura los roles** (una `reader`, la otra `card`; **misma sesión**):
   ```sh
   bombercat config nfcgate -d 1 --server 192.168.1.5:5566 --session 42 --role reader
   bombercat config nfcgate -d 2 --server 192.168.1.5:5566 --session 42 --role card
   ```
3. **Coloca la tarjeta física** sobre la BomberCat #1 (la `reader`).
4. **Arranca las dos**:
   ```sh
   bombercat run -d 1
   bombercat run -d 2
   ```
5. **Comprueba que se conectaron**:
   ```sh
   bombercat status -d 1        # debe indicar enlace y "peer" presente
   ```
6. **Acerca el terminal** a la BomberCat #2 (la `card`). Espera 12–15 s a que
   cruce la transacción.
7. Para ver el detalle en vivo (registros y APDUs) mientras pruebas:
   ```sh
   bombercat monitor -d 1
   ```
8. **Al terminar**, detén ambas:
   ```sh
   bombercat stop -d 1
   bombercat stop -d 2
   ```

**Recomendaciones de seguridad para este modo:**

- Mantén la tarjeta física **quieta y bien apoyada** sobre la `reader` durante
  toda la prueba; si se separa, el relay se corta.
- Verifica que ambas BomberCat estén en la **misma sesión**; un número distinto
  hace que nunca se emparejen.
- No dejes las BomberCat arrancadas sin supervisión; detenlas al acabar.

---

### Modo 2 — BomberCat Reader + Móvil con NFCGate en modo Card

**Propósito:** usar un teléfono Android con la app **NFCGate** como el extremo que
se presenta ante el terminal (modo *tarjeta*/HCE), mientras la BomberCat lee una
tarjeta física.

> [!IMPORTANT]
> ## 🔴 Este modo requiere un teléfono ROOTEADO
>
> Por una limitación de Android, un teléfono **normal (sin root)** **no puede**
> emular una tarjeta de pago hacia un terminal: su modo tarjeta solo responde a
> una tarjeta de prueba, así que el terminal EMV nunca llega a la app y **no
> cruza ningún dato**.
>
> Este modo **solo funciona** con un teléfono **rooteado** que tenga instalado el
> **módulo nativo de NFCGate (Xposed / `nfcd` parcheado)**. Si tu teléfono no está
> rooteado, **usa el Modo 3 en su lugar** — hace lo mismo con los roles
> invertidos y funciona en teléfonos normales.
>
> 📱 **¿No sabes cómo rootear el teléfono ni preparar NFCGate?** Sigue la
> [Guía para rootear un teléfono Android para NFCGate](https://github.com/ElectronicCats/bombercat-tools/blob/main/docs/guia-rooteo-android-nfcgate.es.md)
> (en el repositorio `bombercat-tools`), que explica paso a paso el rooteo con
> Magisk y la instalación del módulo de NFCGate (Zygisk + LSPosed), con sus
> advertencias correspondientes.

**Requisitos mínimos:**

- 1 BomberCat con el firmware de relay (será el `reader`).
- 1 teléfono Android **rooteado** con la app **NFCGate** y su módulo nativo.
- 1 PC con el servidor NFCGate corriendo.
- 1 tarjeta física a leer y 1 terminal contra el que probar.
- Todo en la misma red WiFi.

**Pasos:**

1. **Configura la BomberCat como `reader`** (WiFi + servidor + sesión):
   ```sh
   bombercat config wifi    --ssid MiRedWiFi --pass 'miclave'
   bombercat config nfcgate --server 192.168.1.5:5566 --session 42 --role reader
   ```
2. **En el teléfono, abre la app NFCGate** y en sus ajustes de red pon **la misma
   dirección del servidor** (la IP de la PC) y **el mismo número de sesión** (42).
3. En la app, **elige el modo emulación/tarjeta** (que el teléfono actúe como la
   tarjeta ante el terminal) y únete a la sesión.
4. **Coloca la tarjeta física** sobre la BomberCat y arráncala:
   ```sh
   bombercat run
   bombercat status        # comprueba que aparece el "peer" (el teléfono)
   ```
5. **Acerca el terminal al teléfono.** Espera 12–15 s a que cruce la transacción.
6. **Al terminar**, `bombercat stop` y detén el relay en la app.

**Recomendaciones de seguridad para este modo:**

- Confirma que la app muestra el relay **activo en modo emulación** antes de
  acercar el terminal.
- Un teléfono rooteado es más vulnerable; úsalo dedicado a pruebas, no como tu
  teléfono personal.
- Si no cruza nada, casi siempre es porque el teléfono **no está rooteado** o el
  módulo NFCGate no está activo → cambia al **Modo 3**.

---

### Modo 3 — BomberCat Card + Móvil con NFCGate en modo Reader

**Propósito:** el equivalente al Modo 2 pero **compatible con teléfonos normales
(sin root)**. Aquí el teléfono actúa como **lector** (lee una tarjeta física con
su NFC) y la BomberCat se presenta como **tarjeta** ante el terminal. Es la forma
recomendada si quieres usar un teléfono.

**Requisitos mínimos:**

- 1 BomberCat con el firmware de relay (será la `card`).
- 1 teléfono Android **normal** con la app **NFCGate** (no requiere root).
- 1 PC con el servidor NFCGate corriendo.
- 1 tarjeta física (la leerá el teléfono) y 1 terminal contra el que probar.
- Todo en la misma red WiFi.

**Pasos:**

1. **Configura la BomberCat como `card`** (WiFi + servidor + sesión):
   ```sh
   bombercat config wifi    --ssid MiRedWiFi --pass 'miclave'
   bombercat config nfcgate --server 192.168.1.5:5566 --session 42 --role card
   ```
2. **En el teléfono, abre NFCGate**, pon **la misma dirección del servidor** y **el
   mismo número de sesión** (42), y **elige el modo lector**. Únete a la sesión.
3. **Acerca la tarjeta física al teléfono** (el teléfono es quien la lee).
4. **Arranca la BomberCat**:
   ```sh
   bombercat run
   bombercat status        # comprueba que aparece el "peer" (el teléfono)
   ```
5. **Acerca el terminal a la BomberCat.** Espera 12–15 s a que cruce la
   transacción. Para ver el detalle en vivo:
   ```sh
   bombercat monitor
   ```
6. **Al terminar**, `bombercat stop` y detén el relay en la app.

**Recomendaciones de seguridad para este modo:**

- Mantén la tarjeta física **bien apoyada sobre el teléfono** durante la prueba.
- Como en los demás modos, verifica que **servidor y sesión coinciden** en los dos
  extremos.
- Es el modo preferente para demostraciones con teléfono por no requerir root.

---

## C. Solución de Problemas Comunes

| Problema | Causa probable | Solución |
|---|---|---|
| **No cruza ningún dato entre extremos** | Sesión distinta en cada extremo, o apuntan a servidores distintos. | Revisa que `--session` y `--server` sean **idénticos** en los dos extremos. |
| **`bombercat device list` no muestra la BomberCat** | Cable USB de solo carga, o falta permiso de puerto serie (Linux). | Usa un cable de **datos**; en Linux añade tu usuario al grupo `dialout` y reinicia sesión. |
| **`status` nunca muestra "peer"** | El otro extremo no se ha unido, o no hay WiFi. | Confirma que el servidor está corriendo, que ambos extremos están en la **misma WiFi** y que arrancaste los dos (`run`). |
| **Con teléfono en modo tarjeta (Modo 2) no pasa nada** | El teléfono **no está rooteado** (limitación de Android HCE). | Usa el **Modo 3**, que funciona en teléfonos normales. |
| **La transacción tarda "demasiado" (10–15 s)** | Es el tiempo normal del camino WiFi + servidor. | No es un fallo: mantén tarjeta y terminal **quietos** hasta que termine. |
| **El relay se corta a mitad** | La tarjeta se separó del lector, o se cayó el WiFi. | Vuelve a apoyar la tarjeta; acerca los equipos al router; vuelve a `run`. |

---

## D. Glosario de Términos

- **NFC (Near Field Communication):** tecnología de comunicación sin contacto a
  pocos centímetros. Es lo que usan las tarjetas de pago "contactless", los
  accesos por tarjeta y el pago con el móvil.
- **Tarjeta / Emulación (HCE):** hacer que un dispositivo **se comporte como una
  tarjeta** ante un lector. En un teléfono se llama *HCE* (Host Card Emulation).
- **Lector (Reader):** el papel de quien **lee** una tarjeta (genera el campo NFC
  y le hace preguntas). El terminal de pago es un lector.
- **Terminal / PoS:** el aparato que cobra o valida (por ejemplo, la terminal de
  un comercio o un lector de control de acceso).
- **Relay (retransmisión):** técnica que **reenvía** la conversación entre una
  tarjeta y un lector que están **en lugares distintos**, como si estuvieran
  juntos. Es lo que hace BomberCat en esta guía.
- **Replay:** reproducir una comunicación capturada previamente. Relacionado, pero
  no es lo que hace este flujo (aquí el reenvío es **en vivo**).
- **Auditoría de seguridad:** prueba **autorizada** para descubrir si un sistema
  es vulnerable, con permiso del propietario y dentro de un alcance acordado.
- **APDU:** cada "mensaje" que se intercambian tarjeta y lector. En el monitor los
  verás como secuencias de bytes en hexadecimal.
- **Servidor NFCGate:** el programa intermediario que reenvía los datos entre los
  dos extremos del relay por la red.
- **Sesión:** número (1–255) que **empareja** a los dos extremos en el servidor.
  Ambos deben usar el mismo.
- **Rol:** el papel de cada extremo: `reader` (lee una tarjeta física) o `card`
  (se presenta como tarjeta ante un terminal).
- **Firmware:** el programa interno de la BomberCat. Aquí, el firmware de relay
  compatible con NFCGate.
- **NFCGate:** proyecto/app de Android para capturar y retransmitir NFC, con el que
  la BomberCat es compatible.

---

> **Recordatorio final:** usa BomberCat solo en pruebas **autorizadas**. La
> responsabilidad del uso es **exclusivamente tuya**.
