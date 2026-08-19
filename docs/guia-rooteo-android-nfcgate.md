# Guía — Rootear un teléfono Android para NFCGate (Modo 2)

> **Manual complementario de la [Guía de Uso de BomberCat](guia-usuario-bombercat.md).**
> Solo necesitas este documento si vas a usar el **Modo 2** (BomberCat *reader* +
> teléfono con NFCGate en modo *tarjeta*/HCE), que **exige un teléfono rooteado**.
> Si tu teléfono no está rooteado y no quieres rootearlo, **usa el Modo 3** de la
> guía principal: hace lo mismo con los roles invertidos y **no requiere root**.

---

## ⚠️ Advertencias — LÉELAS ANTES DE EMPEZAR

> [!WARNING]
> ## 🔴 Rootear tu teléfono es un proceso arriesgado e irreversible en la práctica
>
> - **BORRA TODOS TUS DATOS.** Desbloquear el *bootloader* hace un **borrado de
>   fábrica**: se pierden fotos, mensajes, apps y cuentas. **Haz una copia de
>   seguridad completa antes de empezar.**
> - **PUEDES INUTILIZAR EL TELÉFONO (*brick*).** Flashear una imagen equivocada o
>   interrumpir el proceso puede dejar el teléfono sin arrancar. El riesgo es real.
> - **ANULA LA GARANTÍA** de la mayoría de fabricantes y puede quedar registrado
>   de forma permanente en el teléfono.
> - **REDUCE LA SEGURIDAD DEL DISPOSITIVO.** Un teléfono rooteado es más
>   vulnerable. Apps de banca, pagos (Google Pay), streaming o de empresa pueden
>   **dejar de funcionar** (SafetyNet / Play Integrity).
> - **USA UN TELÉFONO DEDICADO A PRUEBAS**, nunca tu teléfono personal ni uno con
>   datos que te importen.
>
> > **Tú eres el único responsable.** Ni Electronic Cats ni los autores de esta
> > guía se hacen responsables de datos perdidos, dispositivos dañados ni de
> > cualquier consecuencia derivada de seguir estos pasos.

> [!IMPORTANT]
> Estos pasos son una **referencia general** basada en un teléfono concreto
> (estilo Google Pixel / Android "stock"). **Cada marca y modelo es distinto.**
> Antes de empezar, busca la guía específica de **tu modelo exacto** (por ejemplo
> en foros como XDA Developers). Algunos fabricantes (ciertos Samsung, Huawei,
> etc.) **bloquean el desbloqueo del bootloader** y no se pueden rootear por esta
> vía.

---

## Requisitos previos

- Un teléfono Android **dedicado a pruebas** (idealmente un Pixel o similar con
  Android stock, que es lo mejor soportado).
- Una **PC** (Windows, Linux o Mac) con un puerto USB.
- Un **cable USB de datos** (no de solo carga).
- Conexión a Internet para descargar la imagen de fábrica.
- **Tiempo y calma**: no hagas esto con prisa ni con la batería baja (>50 %).

---

## Parte 1 — Rootear el teléfono con Magisk

### Etapa 1: Preparación

1. **Activa las Opciones de Desarrollador.**
   Ve a **Ajustes → Acerca del teléfono** y toca **siete veces** sobre
   *Número de compilación* (Build Number). Aparecerá un aviso de que ya eres
   desarrollador.

2. **Activa el desbloqueo OEM y la depuración USB.**
   Entra en **Ajustes → Sistema → Opciones de desarrollador** y activa:
   - **Desbloqueo de OEM** (*OEM unlocking*).
   - **Depuración por USB** (*USB debugging*).

   > [!WARNING]
   > A partir del siguiente paso se **borrarán todos los datos** del teléfono.
   > Asegúrate de tener la copia de seguridad hecha.

3. **Instala las herramientas en la PC.**
   - Los **drivers USB de Android** de tu fabricante (sobre todo en Windows).
   - Las **SDK Platform-Tools** de Google (contienen los ejecutables **ADB** y
     **Fastboot**). Descárgalas de la web oficial de Android:
     https://developer.android.com/tools/releases/platform-tools?hl=es-419#downloads
     y descomprímelas en una carpeta fácil de encontrar (por ejemplo
     `platform-tools`).

### Etapa 2: Desbloquear el bootloader

1. Conecta el teléfono a la PC con el cable USB.
2. Abre una terminal **dentro de la carpeta `platform-tools`** y comprueba que la
   PC ve el teléfono:
   ```sh
   adb devices
   ```
   La primera vez el teléfono pedirá **autorizar la depuración USB** desde este
   equipo: acepta. Debe aparecer tu dispositivo en la lista.
3. Reinicia el teléfono en modo *bootloader* (Fastboot):
   ```sh
   adb reboot bootloader
   ```
4. Desbloquea el bootloader:
   ```sh
   fastboot flashing unlock
   ```
   > En algunos modelos el comando es `fastboot oem unlock`.

   En la pantalla del teléfono aparecerá una **confirmación**: usa los botones de
   volumen para seleccionar **desbloquear** y el botón de encendido para
   confirmar. **En este momento se borra el teléfono.**

### Etapa 3: Parchear la imagen con Magisk

1. **Identifica el número de compilación (build) exacto** de tu teléfono
   (Ajustes → Acerca del teléfono → Número de compilación).
2. **Descarga la Imagen de Fábrica (*Factory Image*) que coincida EXACTAMENTE**
   con ese build, desde la web oficial del fabricante.

   > [!WARNING]
   > La imagen **debe coincidir** con tu modelo y build. Una imagen equivocada
   > puede inutilizar el teléfono.

3. **Extrae el archivo `boot.img`** de dentro de la imagen de fábrica
   (normalmente viene comprimida en varias capas: descomprime hasta encontrar
   `boot.img`).
4. **Transfiere `boot.img` al teléfono** (a la carpeta de Descargas, por ejemplo).
5. Instala la app **Magisk** en el teléfono (desde su repositorio oficial) y
   ábrela.
6. En Magisk, pulsa **Instalar → Seleccionar y parchear un archivo**, elige el
   `boot.img` que copiaste. Magisk generará un archivo **`magisk_patched-*.img`**
   (inyecta los binarios de superusuario en la imagen).

### Etapa 4: Flashear la imagen parcheada

1. **Copia el `magisk_patched-*.img` de vuelta a la PC**, dentro de la carpeta
   `platform-tools`.
2. Reinicia el teléfono en modo Fastboot (si no lo está):
   ```sh
   adb reboot bootloader
   ```
3. Flashea la imagen parcheada (pon el **nombre exacto** de tu archivo):
   ```sh
   fastboot flash boot magisk_patched-XXXXX.img
   ```
4. Reinicia el teléfono:
   ```sh
   fastboot reboot
   ```

### Etapa 5: Verificación

1. Instala la app **Root Checker** desde la Play Store.
2. Ábrela y comprueba que confirma que el dispositivo tiene **acceso root
   (superusuario)** correctamente.

Si Root Checker confirma root, el teléfono está rooteado. ✅

---

## Parte 2 — Preparar NFCGate (Zygisk + LSPosed)

Para que NFCGate funcione en modo *tarjeta*/HCE necesita su módulo nativo, que se
instala mediante **Zygisk** (dentro de Magisk) y **LSPosed**.

1. **Habilita Zygisk en Magisk.**
   Abre **Magisk → Ajustes** y activa **Zygisk**. Reinicia el teléfono si te lo
   pide.

2. **Descarga LSPosed (variante Zygisk).**
   Entra en la página oficial de *releases*:
   `https://github.com/LSPosed/LSPosed/releases`
   y descarga el archivo **`.zip` correspondiente a Zygisk**.

   > [!IMPORTANT]
   > Descarga la variante **Zygisk**, no la de Riru. Debe coincidir con cómo
   > habilitaste el módulo (Zygisk dentro de Magisk).

3. **Instala LSPosed como módulo en Magisk.**
   Abre **Magisk → Módulos → Instalar desde almacenamiento**, selecciona el `.zip`
   de LSPosed y, al terminar, **reinicia el teléfono**.

4. **Concede permisos en LSPosed.**
   Abre la app **LSPosed**. Activa el módulo correspondiente y da los **permisos
   necesarios a NFCGate** (y a **NFC** en general en los ajustes del sistema).

5. **Verifica en NFCGate.**
   Abre la app **NFCGate** y entra en la sección **Status**: comprueba que
   aparecen **todos los permisos y componentes necesarios como correctos** (sin
   avisos en rojo). Si el status está en verde, NFCGate está listo para el
   **Modo 2**.

---

> **Vuelve a la guía principal:** una vez el teléfono está rooteado y NFCGate
> muestra su *status* correcto, continúa con el
> [Modo 2 de la Guía de Uso de BomberCat](guia-usuario-bombercat.md#modo-2--bombercat-reader--móvil-con-nfcgate-en-modo-card).

> **Recordatorio:** rootea solo un teléfono **dedicado a pruebas** y usa BomberCat
> únicamente en auditorías **autorizadas**. La responsabilidad es **exclusivamente
> tuya**.
