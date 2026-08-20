# Montar el servidor NFCGate en un servidor dedicado

Guía para desplegar el `nfcgate-server` de forma **permanente en un servidor
dedicado** (VPS, máquina en tu red, etc.) — es decir, **sin** usar
`bombercat testserver run`, que está pensado para levantar el servidor de forma
local y efímera en Docker. Al final se explica cómo configurar las dos BomberCat
para que se conecten a ese servidor.

---

## 1. Qué es el servidor y qué hace

El `nfcgate-server` es un simple **relé TCP**: acepta conexiones de clientes,
cada cliente se asocia a una **sesión** de 1 byte, y el servidor reenvía cada
trama (frame con prefijo de longitud) a *todos* los demás clientes de la misma
sesión. No hace criptografía ni lógica de aplicación; solo mueve APDUs entre los
dos extremos del relay.

```
[ tarjeta física ] --RF--> [ BomberCat READER ] --WiFi/TCP--\
                                                             >-- nfcgate-server (:5566)
[ terminal/PoS ]  <--RF--- [ BomberCat CARD/HCE ] --WiFi/TCP-/
```

- Escucha en `0.0.0.0:5566` (constante `HOST`/`PORT` en `server.py`).
- El plugin `log` decodifica e imprime cada trama relayada (`R` = viene del
  reader, `C` = viene de la card), útil para depurar.
- Requiere el runtime **protobuf 3.x** (`protobuf==3.20.3`); protobuf 4+ falla
  con *"Descriptors cannot be created directly"* al importar los `*_pb2.py`.
- **Necesita el parche de latencia** (Fases E y H). Ya viene incluido en el
  commit fijado del fork, así que clonando según §2.1 no hay que aplicar nada;
  solo hace falta a mano sobre un checkout pristine del upstream — ver §2.2. Sin
  él la latencia por transacción sube de ~4.2 s a ~13.5 s.

Origen del código: fork `ElectronicCats/nfcgate-server` (rama `v2`) fijado al
commit `fc9103d`, que es upstream `nfcgate/server@4d32cc1` + el parche de
latencia (ver `firmware/core/proto/UPSTREAM.md`).

---

## 2. Obtener el código del servidor en la máquina dedicada

### 2.1. Clonar el fork fijado

El servidor **no está versionado** en este repo (es un fixture clonado bajo
demanda). En el servidor dedicado, clona **nuestro fork** y fíjalo al commit
soportado:

```bash
git clone https://github.com/ElectronicCats/nfcgate-server.git /opt/nfcgate-server
cd /opt/nfcgate-server
git checkout fc9103d
```

> `fc9103d` (rama `v2`) es exactamente `nfcgate/server@4d32cc1` **más** el parche
> de latencia de §2.2, así que ya viene aplicado.

> El submódulo anidado `protocol` **no** hace falta inicializarlo: en tiempo de
> ejecución `server.py` solo usa `plugins/` y los `*_pb2.py` ya incluidos.

Los archivos que importan son:
- `server.py` — el relé.
- `plugins/mod_log.py`, `plugins/c2c_pb2.py`, `plugins/c2s_pb2.py`.

### 2.2. Aplicar el parche de latencia — **OBLIGATORIO**

> ✅ **Si clonaste el fork en §2.1, ya lo tienes.** `fc9103d` incluye estas dos
> fases, así que puedes saltar a §2.3 y limitarte a **verificarlas**. El resto de
> esta sección aplica si partes de un checkout pristine de `nfcgate/server` o de
> un clon anterior a la migración al fork.

> ⚠️ **No te saltes la verificación.** El upstream `4d32cc1` tal cual da **~13.5 s
> por transacción**; con el parche baja a **~4.2 s**. Es el mayor recorte de
> latencia de todo el proyecto y es **puro código del servidor** — no hay que
> reflashear las placas ni cambiar nada en ellas.

Las optimizaciones del relé viven en dos sitios distintos:

| Fase | Dónde vive | En el VPS |
|---|---|---|
| A.2, C, D, F, G, I | **Firmware** de la BomberCat | Ya están en el flash de las placas — no dependen de dónde corra el servidor |
| **E, H** | **`server.py`** (Python) | **Ya vienen en el fork** (`fc9103d`); a mano solo sobre un checkout pristine del upstream |

Qué hacen las dos fases del servidor (detalle completo en
[`firmware/LATENCIA_OPTIMIZACION.md`](../firmware/LATENCIA_OPTIMIZACION.md)):

- **Fase E — `TCP_NODELAY` + write coalescido** (el recorte grande: ~13.5 s → ~5 s).
  El upstream no desactiva Nagle y escribe cada trama en **dos** `write` (header de
  4 B + payload). Como el `wfile` de `StreamRequestHandler` no tiene buffer
  (`wbufsize=0`), salen como **dos segmentos TCP** y, con Nagle activo, el segundo
  espera el ACK del primero — o el timer de **delayed-ACK (~40 ms)** — en **cada**
  relay servidor→placa. Con EMV en lock-step estricto, esos ~36 saltos por
  transacción apilan segundos de tiempo muerto.
- **Fase H — logs por-frame fuera del hot-path** (~50–150 ms). Añade `-v/--verbose`;
  por defecto el servidor ya no imprime el hex-dump ni el `Publish reached` por cada
  trama relayada. Los logs de conexión/sesión se conservan siempre.

**Cómo aplicarlo.** El parche está versionado en este repo como
`tools/testserver/latency-fixes.patch`.

> En **local** no tienes que hacer nada: `bombercat testserver run` lo verifica
> solo (vía `tools/testserver/apply_patch.sh`, invocado desde `run.sh` antes de
> cada build y desde `fetch_server.sh` al clonar), y el commit fijado del fork ya
> lo trae. En el **servidor dedicado** pasa lo mismo si clonaste el fork (§2.1):
> no hay nada que aplicar. Lo que sigue es solo para un checkout pristine del
> upstream o un clon anterior a la migración.

Cópialo al servidor y aplícalo sobre ese checkout — desde **TU máquina**:

```bash
scp tools/testserver/latency-fixes.patch USUARIO@IP_DEL_VPS:/tmp/
```

y luego, en el **servidor** (por SSH):

```bash
cd /opt/nfcgate-server
sudo git apply /tmp/latency-fixes.patch
```

### 2.3. Verificar que el parche está realmente activo

Hay **tres niveles** de comprobación, y no son intercambiables: cada uno detecta
un fallo que el anterior no ve. Haz al menos el nivel 1 y el nivel 3.

#### Nivel 1 — el archivo en disco

En el servidor, dentro de `/opt/nfcgate-server`. Los tres comandos deben dar salida:

```bash
grep -n TCP_NODELAY server.py                   # Fase E: el setsockopt
grep -n "int.to_bytes(len(msg), 4" server.py    # Fase E: el write coalescido (UNA sola línea)
python3 server.py --help | grep verbose         # Fase H: el flag nuevo
```

El tercero funciona **sin** protobuf instalado (los plugins se importan en
tiempo de ejecución, no al arrancar `argparse`), así que sirve incluso antes de
montar el venv o la imagen.

Como diff limpio: `git -C /opt/nfcgate-server diff --stat` debe reportar
`server.py` con ~46 inserciones y 8 borrados.

#### Nivel 2 — el proceso que está corriendo

**Este es el que la gente se salta, y es el que falla.** El nivel 1 mira el
archivo; el servidor en marcha puede estar ejecutando otro código distinto.
Con Docker pasa constantemente: el `Dockerfile` hace `COPY server.py` en tiempo
de *build*, así que si parcheas y haces `docker restart`, el contenedor sigue
levantando el `server.py` viejo horneado en la imagen.

```bash
# Docker — pregunta DENTRO del contenedor, no en el host:
sudo docker exec nfcgate-server grep -c TCP_NODELAY /srv/server.py     # debe dar 2
sudo docker exec nfcgate-server python server.py --help | grep verbose

# systemd — confirma qué archivo ejecuta la unidad y compruébalo:
systemctl show nfcgate-server -p ExecStart
```

Si el `exec` no encuentra `TCP_NODELAY`, **reconstruye**: no basta con reiniciar.

```bash
cd /opt/nfcgate-server
sudo docker build -f Dockerfile -t nfcgate-server .
sudo docker rm -f nfcgate-server
sudo docker run -d --restart unless-stopped --name nfcgate-server -p 5566:5566 nfcgate-server
```

#### Nivel 3 — el comportamiento en el cable (concluyente)

Los dos niveles anteriores requieren shell en el servidor y confían en lo que
*dice* el código. Este mide lo que el servidor **hace**, desde tu máquina y sin
hardware:

```bash
bombercat testserver verify IP_DEL_SERVIDOR 5566
```

Conecta dos clientes a una sesión, relaya varias tramas y observa en cuántos
segmentos TCP llega cada una. Es la firma directa de la Fase E:

Imprime una tabla con lo que ocurrió en cada trama (en cuántos segmentos llegó,
el hueco tras la cabecera y el round trip), las tres medianas en las que se apoya
el veredicto, y un panel final:

| Veredicto | Qué significa |
|---|---|
| ✓ `PATCH ACTIVE` · `frames split across segments  0 / 8` | El servidor manda header+payload en **un** segmento: el parche está vivo en el código que responde en ese puerto. Sale con código 0. |
| ✗ `PATCH MISSING` · `frames split across segments  8 / 8` | Cada trama sale en **dos** segmentos → es el `send_to_clients()` sin parchear. El panel incluye los pasos para arreglarlo. Sale con código 1. |

Si no puede llegar al servidor sale con código 2 y te dice qué revisar.

El comando también reporta el **round trip** del relay, que es lo que te dice si
tu VPS está demasiado lejos (§5.1).

> El verificador se puede correr suelto (`python tools/testserver/verify_patch.py
> HOST PUERTO`) para una salida en texto plano, o con `--json` si quieres
> procesarlo desde un script; el CLI usa esa misma salida JSON para dibujar la
> tabla y los paneles.

> **Sobre el tiempo, no te confundas:** el veredicto se basa en la *fragmentación
> en dos segmentos*, no en los milisegundos. La fragmentación es estructural y se
> ve en cualquier ruta; el stall de ~40 ms de delayed-ACK que esa fragmentación
> provoca **solo aparece donde los ACK se retrasan de verdad**. Si corres el
> verificador en el propio servidor (loopback) verás `gap ~0.00 ms` aunque esté
> **sin parchear**, porque el kernel hace ACK al instante. Por eso juzgar por
> tiempo daría por bueno un servidor lento; el verificador no lo hace, pero tú
> tampoco lo hagas a ojo.

> **Si ya tenías el servidor corriendo**, reinícialo después de parchear
> (`sudo docker restart nfcgate-server` o `sudo systemctl restart nfcgate-server`);
> si usas Docker hay que **reconstruir la imagen** primero, porque el `Dockerfile`
> hace `COPY server.py` en tiempo de build (ver §3.5).

> **¿Y si el parche falla al aplicar?** Casi siempre es porque el checkout no está
> en `4d32cc1` (`git -C /opt/nfcgate-server rev-parse --short HEAD` para
> comprobarlo) o porque ya lo lleva aplicado — que es justo el caso si estás en
> `fc9103d`, el commit del fork. `git apply --check` te lo dirá sin tocar nada.

---

## 3. Opción A — Desplegar en un VPS con Docker (paso a paso, desde cero)

Esta es la ruta recomendada y está pensada para alguien que **nunca ha usado un
VPS**. Reutiliza tal cual el `Dockerfile` de este repo
(`tools/testserver/Dockerfile`), que ya fija `protobuf==3.20.3` y carga el
plugin `log`.

### 3.0. Qué es un VPS y qué necesitas

Un **VPS** (Virtual Private Server) es una máquina Linux en la nube que alquilas
(DigitalOcean, Hetzner, Linode, Vultr, AWS Lightsail, Oracle Cloud, etc.). Te la
administras tú por línea de comandos vía **SSH**.

Al crear el VPS, el proveedor te pide/da estas cosas — apúntalas:

| Dato | Qué es | Ejemplo |
|---|---|---|
| **IP pública** | Dirección para conectarte y a la que apuntarán las BomberCat | `203.0.113.10` |
| **Usuario** | Usuario de administración inicial | `root` o `ubuntu` |
| **Método de acceso** | Contraseña **o** (mejor) llave SSH | llave SSH |
| **Sistema operativo** | Elige **Ubuntu 22.04/24.04 LTS** (los comandos de esta guía asumen eso) | Ubuntu 24.04 |

Requisitos mínimos de sobra: **1 vCPU / 1 GB RAM / 10 GB disco**. El relé es
liviano.

### 3.1. Crear tu llave SSH (en TU computadora, una sola vez)

La llave SSH es la forma segura de entrar sin contraseña. Si ya tienes una
(existe `~/.ssh/id_ed25519.pub`), sáltate esto.

```bash
ls ~/.ssh/id_ed25519.pub 2>/dev/null || ssh-keygen -t ed25519 -C "bombercat-vps"
```

Pulsa Enter en todas las preguntas (ubicación por defecto; passphrase opcional).
Esto crea dos archivos: `~/.ssh/id_ed25519` (privada, **nunca la compartas**) y
`~/.ssh/id_ed25519.pub` (pública, esta sí se sube al VPS).

Al crear el VPS, la mayoría de proveedores te dejan **pegar tu llave pública**
(el contenido de `~/.ssh/id_ed25519.pub`) en el panel web. Hazlo — así entras sin
contraseña desde el primer momento. Muestra su contenido para copiarlo con:

```bash
cat ~/.ssh/id_ed25519.pub
```

> Si tu proveedor solo te dio **usuario + contraseña**, puedes copiar la llave
> después con: `ssh-copy-id USUARIO@IP_DEL_VPS` (te pedirá la contraseña una vez).

### 3.2. Conectarte al VPS por SSH

Desde tu terminal (reemplaza el usuario y la IP por los tuyos):

```bash
ssh USUARIO@IP_DEL_VPS         # p. ej.  ssh root@203.0.113.10
```

- La **primera vez** te dirá *"The authenticity of host ... can't be established"*
  y `Are you sure you want to continue connecting?` → escribe `yes` y Enter.
  (Es normal: solo memoriza la huella del servidor.)
- Si usaste llave SSH, entras directo. Si es por contraseña, te la pedirá.
- Ya dentro, el prompt cambia a algo como `root@nombre-vps:~#`. Todo lo que
  escribas ahora corre **en el VPS**, no en tu máquina. Para salir: `exit`.

### 3.3. Instalar Docker en el VPS

Ya conectado por SSH, instala Docker con el script oficial:

```bash
curl -fsSL https://get.docker.com | sudo sh
sudo docker run --rm hello-world      # comprueba que Docker funciona
```

Si ves *"Hello from Docker!"*, está listo. (Opcional, para no escribir `sudo`
cada vez: `sudo usermod -aG docker $USER` y luego reconéctate con `exit` + `ssh`.)

### 3.4. Llevar el servidor + el Dockerfile al VPS

El `Dockerfile` de este repo copia `server.py` y `plugins/` **desde su build
context**, así que en el VPS necesitas esos archivos junto al Dockerfile. Hay dos
formas; elige una.

**Forma A (recomendada) — clonar el servidor en el VPS y copiarle el Dockerfile.**

Primero, en el **VPS** (por SSH) clona y fija el código del relé:

```bash
sudo git clone https://github.com/ElectronicCats/nfcgate-server.git /opt/nfcgate-server
cd /opt/nfcgate-server && sudo git checkout fc9103d
```

Luego, en **TU máquina** (abre otra terminal local, sin cerrar la del VPS), copia
el Dockerfile de este repo al VPS con `scp` — el `Dockerfile` vive aquí, no en el
fork:

```bash
scp tools/testserver/Dockerfile USUARIO@IP_DEL_VPS:/opt/nfcgate-server/Dockerfile
```

> **El parche ya no se copia por separado**: `fc9103d` lo trae incluido. Aun así,
> corre las comprobaciones de §2.3 en el VPS antes de construir la imagen.

**Forma B — subir todo desde tu máquina** (si ya tienes `./server` clonado aquí,
ver §2.1). Copia la carpeta del servidor y el Dockerfile juntos:

```bash
scp -r server USUARIO@IP_DEL_VPS:/opt/nfcgate-server
scp tools/testserver/Dockerfile USUARIO@IP_DEL_VPS:/opt/nfcgate-server/Dockerfile
```

> Con la Forma B tampoco necesitas copiar el parche: tu `./server` local lo trae
> del fork (o aplicado), y `scp -r` sube el árbol de trabajo tal cual. Aun así,
> corre las comprobaciones de §2.3 en el VPS antes de construir la imagen — si tu
> copia local estuviera fijada al upstream pristine (`SERVER_COMMIT=4d32cc1`),
> subirías el servidor lento sin darte cuenta.

Sea cual sea la forma, en el VPS deberías tener:

```
/opt/nfcgate-server/
├── Dockerfile          ← el de tools/testserver/, reutilizado
├── server.py           ← CON el parche de latencia (viene en fc9103d, §2.2)
└── plugins/  (mod_log.py, c2c_pb2.py, c2s_pb2.py, __init__.py)
```

### 3.5. Construir la imagen y correr el servidor

De vuelta en la terminal **del VPS** (SSH), dentro de `/opt/nfcgate-server` (que
es el build context que espera el Dockerfile):

```bash
cd /opt/nfcgate-server
sudo docker build -f Dockerfile -t nfcgate-server .

sudo docker run -d --restart unless-stopped \
  --name nfcgate-server \
  -p 5566:5566 \
  nfcgate-server
```

- `-d --restart unless-stopped` → corre en segundo plano y **vuelve a arrancar
  solo** tras reinicios o caídas del VPS (a diferencia de `testserver run`, que
  vive en primer plano y muere al cerrar la sesión SSH con Ctrl-C).
- **Sin argumentos** = sin plugins = **modo rápido**. El plugin `log` hace un
  `ParseFromString` de protobuf + `print` por **cada** trama relayada, y eso cae
  en el hot-path lock-step del relay (el mismo coste que la Fase H sacó del core
  del servidor, pero que en el plugin sigue ahí). Déjalo fuera para operar.
- **Para depurar**, relanza el contenedor cargando el plugin y/o el modo verboso:

  ```bash
  sudo docker rm -f nfcgate-server
  sudo docker run -d --restart unless-stopped --name nfcgate-server \
    -p 5566:5566 nfcgate-server log -v
  sudo docker logs -f nfcgate-server        # APDUs en vivo
  ```

  `log` imprime los APDUs decodificados (`R`/`C`, ver §8) y `-v` añade el
  hex-dump por trama del propio servidor. Ambos **cuestan latencia**: vuelve al
  arranque sin argumentos cuando termines de diagnosticar.
- Para cambiar el puerto publicado hacia fuera: `-p 6000:5566` (el contenedor
  siempre escucha en 5566 por dentro; el primer número es el que abres en el VPS).

> **Al reconstruir tras parchear:** el `Dockerfile` hace `COPY server.py` en
> tiempo de build, así que un `docker restart` **no** recoge cambios en el código.
> Tras aplicar (o corregir) el parche hay que `docker build` otra vez y recrear el
> contenedor: `sudo docker rm -f nfcgate-server` y volver a lanzar el `docker run`.

> Puedes cerrar la sesión SSH (`exit`) y el contenedor **sigue corriendo** en el
> VPS. Para gestionarlo luego, vuelve a entrar por SSH:
> `sudo docker stop nfcgate-server` / `start` / `logs -f`.

---

## 4. Opción B — Ejecutar directo con systemd (sin Docker)

Si prefieres no usar Docker, aísla protobuf en un venv y corre bajo systemd.
(El parche de §2.2 sigue siendo **obligatorio** por esta ruta — aplícalo antes.)

```bash
cd /opt/nfcgate-server
python3 -m venv .venv
.venv/bin/pip install "protobuf==3.20.3"
```

Crea `/etc/systemd/system/nfcgate-server.service`:

```ini
[Unit]
Description=NFCGate relay server (BomberCat)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
WorkingDirectory=/opt/nfcgate-server
# Sin plugins ni -v = modo rápido (ver §3.5). Para depurar: "server.py log -v",
# luego 'systemctl daemon-reload && systemctl restart nfcgate-server'.
ExecStart=/opt/nfcgate-server/.venv/bin/python server.py
Restart=always
RestartSec=2
# Endurecimiento opcional:
DynamicUser=yes
NoNewPrivileges=yes

[Install]
WantedBy=multi-user.target
```

Habilitar y arrancar:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now nfcgate-server
sudo journalctl -u nfcgate-server -f    # conexiones/sesiones (APDUs solo con 'log -v')
```

> `server.py` sale limpio con SIGTERM (lo que envía systemd al parar/reiniciar);
> con SIGINT (Ctrl-C) el upstream lanza un traceback, por eso systemd/Docker son
> preferibles a correrlo a mano.

---

## 5. Red y firewall

Ambas BomberCat deben poder alcanzar el **puerto TCP 5566** del servidor.

```bash
# Abrir el puerto (ejemplos):
sudo ufw allow 5566/tcp                 # Ubuntu/Debian con ufw
# o firewalld:
sudo firewall-cmd --add-port=5566/tcp --permanent && sudo firewall-cmd --reload
```

- Si es un **VPS**, abre además el puerto en el grupo de seguridad / firewall
  del proveedor.
- Las BomberCat usan WiFi: si el servidor está en otra red, necesitas IP pública
  (o port-forward) alcanzable desde la red WiFi de las BomberCat.
- **Sin TLS/autenticación:** el relé no cifra ni autentica nada. Cualquiera que
  alcance el puerto y adivine el byte de sesión entra al relay. Para uso fuera de
  un laboratorio cerrado, ponlo detrás de una VPN (WireGuard/Tailscale) o
  restringe el origen por firewall a las IPs conocidas.

Comprueba desde otra máquina que el puerto responde:

```bash
nc -vz IP_DEL_SERVIDOR 5566
```

### 5.1. Ojo con la distancia: el RTT es latencia que no se puede optimizar

Mover el relé de la LAN a un VPS **añade latencia por diseño**, y ninguna
optimización de software la recupera. EMV es lock-step estricto: cada par de APDU
son **4 saltos** (reader→servidor, servidor→card, card→servidor, servidor→reader)
y hay ~18 pares por transacción, o sea **~72 saltos unidireccionales** placa↔servidor.

| Latencia unidireccional placa↔VPS | Coste añadido por transacción |
|---|---|
| ~1 ms (LAN) | ~0.07 s — despreciable |
| ~20 ms (VPS en la misma región) | **~1.4 s** |
| ~80 ms (VPS en otro continente) | **~5.8 s** — duplica con creces el total |

Sobre el piso medido de ~4.2 s, un VPS lejano puede llevarte fuera del
presupuesto de tiempo de una terminal EMV. Elige el VPS **geográficamente cerca**
de donde estén las placas, y si no necesitas que el relé sea remoto, déjalo en la
LAN. Mide el RTT real desde la red WiFi de las placas con `ping IP_DEL_SERVIDOR`
(el valor de la tabla es la mitad del RTT que reporta `ping`).

---

## 6. Verificar el servidor (smoke test, sin hardware)

Desde cualquier máquina con este repo puedes lanzar el smoke test contra el
servidor dedicado (abre dos clientes TCP en la misma sesión y verifica que el
relay entrega los blobs idénticos):

```bash
bombercat testserver smoke  IP_DEL_SERVIDOR 5566   # ¿relaya correctamente?
bombercat testserver verify IP_DEL_SERVIDOR 5566   # ¿tiene el parche de latencia?
```

Salida esperada: `RELAY SMOKE TEST PASSED` y `RESULT: PATCH ACTIVE`. Ninguno de
los dos levanta un servidor local; solo se conectan al host/puerto indicado.

Los dos son necesarios y comprueban cosas distintas: `smoke` valida que el relé
**funciona** (entrega los blobs idénticos), `verify` que además es **rápido** —
un servidor sin parchear pasa el `smoke` sin inmutarse. Detalle de `verify` en §2.3.

---

## 7. Configurar las BomberCat para el servidor dedicado

El firmware NFCGate arranca en el REPL **SerialControl** y se configura por
USB-serial con el CLI `bombercat`. La config se persiste en flash y **gana** por
encima de los valores de compilación de `arduino_secrets.h`.

Para cada BomberCat (conéctala por USB y ejecuta):

```bash
# 1) WiFi (una red que tenga alcance al servidor)
bombercat config wifi --ssid "MiRed" --pass "s3cret"

# 2) Servidor dedicado + sesión + rol
#    --server admite host o host:puerto (si omites el puerto usa 5566)
bombercat config nfcgate --server IP_DEL_SERVIDOR:5566 --session 42 --role reader

# 3) Verifica lo que quedó guardado
bombercat config show
```

**Puntos clave de la configuración de las dos placas:**

| Campo | READER (lee tarjeta) | CARD/HCE (emula ante terminal) |
|---|---|---|
| `--server` | `IP_DEL_SERVIDOR:5566` | `IP_DEL_SERVIDOR:5566` (**el mismo**) |
| `--session` | `42` | `42` (**debe coincidir**, 1..255) |
| `--role` | `reader` | `card` |

- Las dos placas apuntan al **mismo servidor** y comparten el **mismo byte de
  sesión**; solo cambia el `--role`.
- Si dos BomberCat distintas están en el mismo PC, distínguelas con `-d`/`--port`
  (ver `bombercat device list`); `config` hace parpadear el LED de la placa que
  acaba de configurar.

> Alternativa por compilación: si vas a flashear ya apuntando al servidor, edita
> `firmware/NFCGate/arduino_secrets.h` (`RELAY_SERVER`, `RELAY_PORT`,
> `RELAY_SESSION`, `RELAY_ROLE`) **antes** de compilar. Deja `RELAY_AUTOSTART 0`
> si vas a controlar la placa por USB con el CLI; solo pon `1` para un equipo
> autónomo que no vayas a manejar por serial.

---

## 8. Arrancar y observar el relay

Con el servidor dedicado en marcha y ambas placas configuradas:

```bash
# En cada BomberCat (o en cada terminal apuntando a su placa):
bombercat run          # asocia WiFi, conecta al servidor y registra la sesión
bombercat status       # estado: state / link connected / peer present / APDUs
bombercat monitor      # stream en vivo de los APDUs (hex) — Ctrl-C para salir
bombercat stop         # detiene el relay
```

Si arrancaste el servidor **con el plugin `log`** (modo diagnóstico, §3.5) verás
las tramas en su salida (`docker logs -f nfcgate-server` o
`journalctl -u nfcgate-server -f`):

```
[log] ('a.b.c.d', 38336) OP_PSH R: (initial) 00a404000e325041592e5359532e444446303100
[log] ('a.b.c.d', 38330) OP_PSH C: (initial) 6f23840e325041592e5359532e4444463031a5119000
```
`R` = datos desde el reader, `C` = datos desde la card.

> En el arranque normal (sin `log`) el servidor solo registra conexiones y
> sesiones — es lo correcto para operar. Para ver los APDUs sin pagar la latencia
> del servidor, usa `bombercat monitor` en las placas.

---

## 9. Checklist rápido de problemas

- `bombercat run` no llega a `relaying`:
  - ¿El puerto responde? `nc -vz IP_DEL_SERVIDOR 5566`.
  - ¿La WiFi de la placa alcanza al servidor? (misma red o IP pública/port-forward).
  - ¿Coinciden `--session` en ambas placas?
  - `bombercat config show` para confirmar host/puerto/rol persistidos.
- El servidor arranca pero no relaya: revisa que **las dos** placas usen la misma
  sesión y roles opuestos (`reader` + `card`).
- Traceback de protobuf en el servidor: tienes protobuf 4+; fija `3.20.3`.
- **La transacción funciona pero tarda ~13 s (en vez de ~4–5 s): falta el parche
  de latencia.** Es el síntoma clásico de la Fase E ausente (Nagle/delayed-ACK).
  Diagnóstico en un comando, desde tu máquina:
  `bombercat testserver verify IP_DEL_SERVIDOR 5566` (§2.3, nivel 3). Si dice
  `PATCH MISSING`, vuelve a §2.2 — y si usas Docker recuerda **reconstruir la
  imagen**, que un `restart` no basta.
- **`grep TCP_NODELAY` sí da salida pero sigue lento:** estás mirando el archivo,
  no el proceso. Pregunta dentro del contenedor
  (`sudo docker exec nfcgate-server grep -c TCP_NODELAY /srv/server.py`) — §2.3,
  nivel 2.
- La transacción tarda bastante más que ~4.5 s **con** el parche aplicado: mide el
  RTT desde la red de las placas (`ping IP_DEL_SERVIDOR`) y compáralo con la tabla
  de §5.1; un VPS lejano añade segundos por sí solo. Comprueba también que no
  dejaste el servidor arrancado con `log` / `-v` de una sesión de depuración.
