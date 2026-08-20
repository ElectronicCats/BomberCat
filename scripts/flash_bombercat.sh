#!/usr/bin/env bash
#
# flash_bombercat.sh — Compila y flashea el firmware correcto en el BomberCat (RP2040)
#
# El BomberCat usa un MCU RP2040 con bootloader UF2 y se programa con arduino-cli
# usando el core "Electronic Cats Mbed OS RP2040 Boards".
#
# Uso:
#   ./flash_bombercat.sh                 # menú interactivo para elegir firmware
#   ./flash_bombercat.sh -f NFCGate      # flashea un firmware por nombre
#   ./flash_bombercat.sh -f NFCGate -m   # flashea y abre el monitor serie
#   ./flash_bombercat.sh -l              # lista los firmwares disponibles
#   ./flash_bombercat.sh --setup         # solo instala/prepara toolchain (core + libs)
#   ./flash_bombercat.sh -p /dev/ttyACM0 # fuerza un puerto en vez de autodetectar
#
# Opciones:
#   -f, --firmware <nombre>  Nombre del sketch a flashear (carpeta dentro de firmware/)
#   -p, --port <puerto>      Puerto serie del BomberCat (autodetectado si se omite)
#   -m, --monitor            Abre el monitor serie tras flashear
#   -l, --list               Lista los firmwares disponibles y termina
#   -c, --compile-only       Solo compila, no sube al dispositivo
#       --setup              Instala/actualiza core y librerías y termina
#   -y, --yes                No pide confirmación
#   -h, --help               Muestra esta ayuda

set -euo pipefail

# --------------------------------------------------------------------------- #
# Configuración
# --------------------------------------------------------------------------- #
FQBN="electroniccats:mbed_rp2040:bombercat"
CORE="electroniccats:mbed_rp2040"
BOARDS_INDEX_URL="https://raw.githubusercontent.com/ElectronicCats/Arduino_Boards_Index/master/package_electroniccats_index.json"

# Librerías necesarias (desde el Library Manager)
LIBRARIES=(
  "PubSubClient"
  "WiFiNINA"
  "Electronic Cats PN7150"
)
# SerialCommand (kroimon) no está en el Library Manager; se instala desde git.
SERIALCOMMAND_GIT="https://github.com/kroimon/Arduino-SerialCommand.git"

# Rutas: el directorio de firmware está junto a la carpeta scripts/ (../firmware)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRMWARE_DIR="$(cd "$SCRIPT_DIR/../firmware" && pwd)"

# --------------------------------------------------------------------------- #
# Colores / logging
# --------------------------------------------------------------------------- #
if [[ -t 1 ]]; then
  C_RED=$'\033[31m'; C_GRN=$'\033[32m'; C_YEL=$'\033[33m'
  C_BLU=$'\033[34m'; C_BLD=$'\033[1m';  C_RST=$'\033[0m'
else
  C_RED=""; C_GRN=""; C_YEL=""; C_BLU=""; C_BLD=""; C_RST=""
fi
info()  { printf '%s[i]%s %s\n' "$C_BLU" "$C_RST" "$*"; }
ok()    { printf '%s[✓]%s %s\n' "$C_GRN" "$C_RST" "$*"; }
warn()  { printf '%s[!]%s %s\n' "$C_YEL" "$C_RST" "$*" >&2; }
err()   { printf '%s[✗]%s %s\n' "$C_RED" "$C_RST" "$*" >&2; }
die()   { err "$*"; exit 1; }

# --------------------------------------------------------------------------- #
# Sistema (portabilidad Debian/Ubuntu/Mint/…)
# --------------------------------------------------------------------------- #
have() { command -v "$1" >/dev/null 2>&1; }

# ¿Es una distro de la familia Debian? (Debian, Ubuntu, Mint, Pop!_OS, …)
is_debian_like() {
  [[ -r /etc/os-release ]] || return 1
  local id id_like
  id="$(. /etc/os-release; echo "${ID:-}")"
  id_like="$(. /etc/os-release; echo "${ID_LIKE:-}")"
  [[ "$id" == debian || "$id" == ubuntu ]] && return 0
  [[ " $id_like " == *" debian "* || " $id_like " == *" ubuntu "* ]]
}

# Instala paquetes del sistema con apt (solo en distros Debian-like).
apt_install() {
  is_debian_like || return 1
  have apt-get || return 1
  local sudo=""; [[ "$(id -u)" -ne 0 ]] && sudo="sudo"
  info "Instalando con apt: $*"
  $sudo apt-get update -qq && $sudo apt-get install -y "$@"
}

# Descarga una URL a stdout usando curl o wget (lo que haya).
fetch() {
  if have curl; then curl -fsSL "$1"
  elif have wget; then wget -qO- "$1"
  else return 1; fi
}

# --------------------------------------------------------------------------- #
# Opciones
# --------------------------------------------------------------------------- #
FIRMWARE=""
PORT=""
DO_MONITOR=0
DO_LIST=0
COMPILE_ONLY=0
SETUP_ONLY=0
ASSUME_YES=0

usage() { sed -n '2,40p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    -f|--firmware)     FIRMWARE="${2:-}"; shift 2 ;;
    -p|--port)         PORT="${2:-}"; shift 2 ;;
    -m|--monitor)      DO_MONITOR=1; shift ;;
    -l|--list)         DO_LIST=1; shift ;;
    -c|--compile-only) COMPILE_ONLY=1; shift ;;
    --setup)           SETUP_ONLY=1; shift ;;
    -y|--yes)          ASSUME_YES=1; shift ;;
    -h|--help)         usage; exit 0 ;;
    *) die "Opción desconocida: $1 (usa -h para ayuda)" ;;
  esac
done

# --------------------------------------------------------------------------- #
# Firmwares disponibles (carpetas con un .ino del mismo nombre)
# --------------------------------------------------------------------------- #
list_firmwares() {
  local d name
  for d in "$FIRMWARE_DIR"/*/; do
    name="$(basename "$d")"
    [[ -f "$d/$name.ino" ]] && echo "$name"
  done | sort
}

if [[ $DO_LIST -eq 1 ]]; then
  info "Firmwares disponibles en $FIRMWARE_DIR:"
  list_firmwares | sed 's/^/  - /'
  exit 0
fi

# --------------------------------------------------------------------------- #
# arduino-cli: verificar / instalar
# --------------------------------------------------------------------------- #
ensure_arduino_cli() {
  if command -v arduino-cli >/dev/null 2>&1; then
    ok "arduino-cli encontrado: $(arduino-cli version | head -n1)"
    return
  fi
  warn "arduino-cli no está instalado."
  if ! confirm "¿Instalarlo en ~/.local/bin ahora?"; then
    die "arduino-cli es necesario. Instálalo desde https://arduino.github.io/arduino-cli/"
  fi
  # El instalador oficial necesita un descargador. Si no hay ninguno, intenta
  # traer curl con apt (en distros Debian-like).
  if ! have curl && ! have wget; then
    warn "No se encontró 'curl' ni 'wget' (necesarios para descargar)."
    apt_install curl \
      || die "Instala 'curl' o 'wget' y vuelve a ejecutar: sudo apt-get install curl"
  fi
  local bindir="$HOME/.local/bin"
  mkdir -p "$bindir"
  info "Descargando arduino-cli..."
  fetch https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh \
    | BINDIR="$bindir" sh
  export PATH="$bindir:$PATH"
  command -v arduino-cli >/dev/null 2>&1 || die "No se pudo instalar arduino-cli."
  case ":$PATH:" in
    *":$bindir:"*) ;;
    *) warn "Añade $bindir a tu PATH (p. ej. en ~/.zshrc): export PATH=\"$bindir:\$PATH\"" ;;
  esac
  ok "arduino-cli instalado."
}

# --------------------------------------------------------------------------- #
# Core + librerías
# --------------------------------------------------------------------------- #
ensure_core() {
  info "Configurando índice de placas de Electronic Cats..."
  arduino-cli config init --overwrite >/dev/null 2>&1 || true
  arduino-cli config add board_manager.additional_urls "$BOARDS_INDEX_URL" >/dev/null 2>&1 \
    || arduino-cli config set board_manager.additional_urls "$BOARDS_INDEX_URL" >/dev/null 2>&1 || true
  arduino-cli core update-index

  if arduino-cli core list 2>/dev/null | grep -q "^${CORE}"; then
    ok "Core $CORE ya instalado."
  else
    info "Instalando core $CORE..."
    arduino-cli core install "$CORE"
    ok "Core instalado."
  fi

  # Linux: script post-install para permisos/reglas udev del bootloader RP2040
  local post_install
  post_install="$(find "$HOME/.arduino15/packages/electroniccats/hardware/mbed_rp2040" \
                    -name post_install.sh 2>/dev/null | sort | tail -n1 || true)"
  if [[ "$(uname -s)" == "Linux" && -n "$post_install" ]]; then
    warn "En Linux puede hacer falta ejecutar (con permisos) el post-install para reglas udev:"
    printf '      sudo %s\n' "$post_install"
  fi
}

ensure_libraries() {
  info "Instalando/verificando librerías..."
  arduino-cli lib update-index
  local lib
  for lib in "${LIBRARIES[@]}"; do
    if arduino-cli lib list 2>/dev/null | grep -qi "^$lib "; then
      ok "Librería '$lib' presente."
    else
      info "Instalando '$lib'..."
      arduino-cli lib install "$lib" || warn "No se pudo instalar '$lib' automáticamente."
    fi
  done
  # SerialCommand (kroimon) desde git
  if arduino-cli lib list 2>/dev/null | grep -qi "SerialCommand"; then
    ok "Librería 'SerialCommand' presente."
  else
    info "Instalando 'SerialCommand' desde git..."
    arduino-cli config set library.enable_unsafe_install true >/dev/null 2>&1 || true
    arduino-cli lib install --git-url "$SERIALCOMMAND_GIT" \
      || warn "No se pudo instalar SerialCommand automáticamente. Instálala manualmente si el sketch la requiere."
  fi
}

# --------------------------------------------------------------------------- #
# Detección de puerto
# --------------------------------------------------------------------------- #
detect_port() {
  # 1) Preguntar a arduino-cli qué placa reconoce
  local p
  p="$(arduino-cli board list 2>/dev/null | awk -v fqbn="$FQBN" '$0 ~ fqbn {print $1; exit}')"
  if [[ -n "$p" ]]; then echo "$p"; return; fi
  # 2) Fallback: primer puerto serie tipo ACM/USB disponible
  for p in /dev/ttyACM* /dev/ttyUSB* /dev/cu.usbmodem* /dev/tty.usbmodem*; do
    [[ -e "$p" ]] && { echo "$p"; return; }
  done
  return 1
}

# En Debian/Ubuntu/Mint el acceso a /dev/ttyACM* requiere pertenecer al grupo
# 'dialout'. Si no, arduino-cli falla con "Permission denied" al subir por serie.
ensure_serial_access() {
  local port="$1"
  [[ "$(uname -s)" == "Linux" ]] || return 0
  [[ -e "$port" ]] || return 0
  # Si ya podemos leer/escribir el dispositivo, no hay nada que hacer.
  [[ -r "$port" && -w "$port" ]] && return 0
  local grp
  grp="$(stat -c '%G' "$port" 2>/dev/null || echo dialout)"
  warn "Sin permisos sobre $port (pertenece al grupo '$grp')."
  if ! id -nG 2>/dev/null | tr ' ' '\n' | grep -qx "$grp"; then
    warn "Tu usuario no está en el grupo '$grp'. Para arreglarlo permanentemente:"
    printf '      sudo usermod -aG %s "$USER"   # luego cierra sesión y vuelve a entrar\n' "$grp" >&2
    if have sudo && confirm "¿Añadirte al grupo '$grp' ahora?"; then
      sudo usermod -aG "$grp" "$USER" \
        && warn "Hecho. Debes CERRAR SESIÓN y volver a entrar (o reiniciar) para que aplique."
    fi
  fi
}

# En modo bootloader (doble reset) el RP2040 NO es un puerto serie, sino una
# unidad de almacenamiento USB (RPI-RP2). Se flashea copiando el .uf2 a ella.
detect_uf2_mount() {
  local mp
  # Linux: cualquier vfat montado con el marcador INFO_UF2.TXT del bootloader
  while read -r mp; do
    [[ -n "$mp" && -f "$mp/INFO_UF2.TXT" ]] && { echo "$mp"; return 0; }
  done < <(mount 2>/dev/null | awk '$5=="vfat"{print $3}')
  # macOS y rutas típicas por si el marcador no fuese legible
  for mp in /media/*/RPI-RP2 /run/media/*/RPI-RP2 /Volumes/RPI-RP2; do
    [[ -d "$mp" ]] && { echo "$mp"; return 0; }
  done
  return 1
}

# El core mbed_rp2040 no genera un .uf2 al compilar (solo .elf/.bin/.hex);
# la conversión a UF2 la hace la herramienta elf2uf2 que trae el core.
make_uf2() {
  local elf="$1" uf2="$2" elf2uf2
  elf2uf2="$(find "$HOME/.arduino15/packages/electroniccats/tools/rp2040tools" \
              -name elf2uf2 -type f 2>/dev/null | sort -V | tail -n1)"
  [[ -n "$elf2uf2" && -x "$elf2uf2" ]] \
    || die "No se encontró la herramienta elf2uf2 del core (¿está instalado electroniccats:mbed_rp2040?)."
  "$elf2uf2" "$elf" "$uf2" >/dev/null 2>&1 \
    || die "Falló la conversión .elf → .uf2 con elf2uf2."
}

# Flashea copiando el .uf2 a la unidad del bootloader.
flash_uf2() {
  local uf2="$1" mnt="$2"
  info "Copiando $(basename "$uf2") a $mnt ..."
  cp "$uf2" "$mnt/" || die "No se pudo copiar el .uf2 a $mnt."
  sync
}

# --------------------------------------------------------------------------- #
# Utilidades
# --------------------------------------------------------------------------- #
confirm() {
  [[ $ASSUME_YES -eq 1 ]] && return 0
  local reply
  read -r -p "$1 [s/N] " reply
  [[ "$reply" =~ ^([sS]|[yY])$ ]]
}

choose_firmware() {
  local fws=() i=1 choice
  mapfile -t fws < <(list_firmwares)
  [[ ${#fws[@]} -eq 0 ]] && die "No se encontraron firmwares en $FIRMWARE_DIR"
  printf '\n%sSelecciona el firmware a flashear:%s\n' "$C_BLD" "$C_RST" >&2
  for i in "${!fws[@]}"; do
    printf '  %2d) %s\n' "$((i+1))" "${fws[$i]}" >&2
  done
  read -r -p "Número [1-${#fws[@]}]: " choice
  [[ "$choice" =~ ^[0-9]+$ ]] && (( choice >= 1 && choice <= ${#fws[@]} )) \
    || die "Selección inválida."
  echo "${fws[$((choice-1))]}"
}

# --------------------------------------------------------------------------- #
# Flujo principal
# --------------------------------------------------------------------------- #
ensure_arduino_cli
ensure_core
ensure_libraries

if [[ $SETUP_ONLY -eq 1 ]]; then
  ok "Toolchain lista. Ejecuta el script de nuevo con -f <firmware> para flashear."
  exit 0
fi

# Elegir firmware
[[ -z "$FIRMWARE" ]] && FIRMWARE="$(choose_firmware)"
SKETCH_DIR="$FIRMWARE_DIR/$FIRMWARE"
[[ -f "$SKETCH_DIR/$FIRMWARE.ino" ]] \
  || die "No existe el firmware '$FIRMWARE' (falta $SKETCH_DIR/$FIRMWARE.ino). Usa -l para listar."

# Aviso: firmwares que requieren credenciales WiFi
if [[ -f "$SKETCH_DIR/arduino_secrets.h" ]]; then
  warn "'$FIRMWARE' usa arduino_secrets.h — revisa que tu SSID/clave estén configurados antes de flashear."
fi

# Compilar (a un directorio temporal para obtener el .uf2 y reutilizar el binario)
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT
info "Compilando '$FIRMWARE' (FQBN: $FQBN)..."
arduino-cli compile --fqbn "$FQBN" --output-dir "$BUILD_DIR" "$SKETCH_DIR"
ok "Compilación correcta."

if [[ $COMPILE_ONLY -eq 1 ]]; then
  ok "Solo compilación solicitada. Listo."
  exit 0
fi

# Localizar el .elf generado (el core no produce .uf2 directamente)
ELF_FILE="$(find "$BUILD_DIR" -maxdepth 1 -name '*.elf' | head -n1)"

# Elegir método de flasheo:
#   - Si el usuario forzó -p, usar ese puerto serie.
#   - Si la placa está en modo bootloader (unidad RPI-RP2 montada), copiar el .uf2.
#   - Si no, intentar detectar un puerto serie (arduino-cli hará el reset a 1200bps).
UF2_MOUNT=""
if [[ -z "$PORT" ]]; then
  info "Detectando el BomberCat..."
  UF2_MOUNT="$(detect_uf2_mount || true)"
  if [[ -z "$UF2_MOUNT" ]]; then
    PORT="$(detect_port || true)"
  fi
fi

if [[ -n "$UF2_MOUNT" ]]; then
  ok "BomberCat en modo bootloader detectado: $UF2_MOUNT"
  [[ -n "$ELF_FILE" ]] || die "No se encontró el .elf compilado en $BUILD_DIR."
  confirm "¿Flashear '$FIRMWARE' por UF2 en $UF2_MOUNT?" || die "Cancelado por el usuario."
  UF2_FILE="$BUILD_DIR/$FIRMWARE.uf2"
  info "Convirtiendo firmware a UF2..."
  make_uf2 "$ELF_FILE" "$UF2_FILE"
  flash_uf2 "$UF2_FILE" "$UF2_MOUNT"
  ok "Firmware '$FIRMWARE' flasheado por UF2. La placa se reiniciará sola."
elif [[ -n "$PORT" ]]; then
  ok "Usando puerto serie: $PORT"
  ensure_serial_access "$PORT"
  confirm "¿Flashear '$FIRMWARE' en $PORT?" || die "Cancelado por el usuario."
  info "Flasheando..."
  arduino-cli upload -p "$PORT" --fqbn "$FQBN" --input-dir "$BUILD_DIR" --verbose "$SKETCH_DIR"
  ok "Firmware '$FIRMWARE' flasheado en $PORT."
else
  die "No se detectó el BomberCat. Ponlo en modo bootloader (doble reset → aparece la unidad RPI-RP2) o conéctalo normalmente, o usa -p <puerto>."
fi

# Monitor
if [[ $DO_MONITOR -eq 1 ]]; then
  info "Abriendo monitor serie (Ctrl-C para salir)..."
  # Tras un flasheo por UF2 la placa se reinicia; esperar y (re)detectar el puerto.
  if [[ -z "$PORT" ]]; then
    for _ in 1 2 3 4 5; do
      sleep 1
      PORT="$(detect_port || true)"
      [[ -n "$PORT" ]] && break
    done
  else
    sleep 2
  fi
  if [[ -n "$PORT" ]]; then
    arduino-cli monitor -p "$PORT"
  else
    warn "No se encontró el puerto serie para el monitor. Ejecútalo manualmente: arduino-cli monitor -p /dev/ttyACM0"
  fi
fi
