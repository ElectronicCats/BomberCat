# BomberCat

## ¿Cómo funciona Hunter Cat NFC?
BomberCat es la última herramienta de seguridad que suma la tecnologia NFC (Near Field Communication) y de banda magnetica utilizada en control de acceso, identificación y tarjetas bancarias. Especialmente creado para auditar terminales bancarias identificar lectores NFC y herramientas de sniffing, con esta herramienta puedes auditar, leer o emular tarjetas de banda magnitica y NFC. 

Ademas cuenta con un co-procesador ESP32 en modo WIFININA que le permite hacer conexion WiFi para usar protocolos HTTP o MQTT, que permitira probar ataques de relay y spoofing a larga distancia o en webserver locales.

#Aplicaciones 
BomberCat se puede configurar para que se comporte como un lector de etiquetas NFC o una tarjeta de banda magnetica. 

BomberCat cuenta con un MCU RP2040 que funciona junto con el PN7150. La interfaz USB proporcionada por el RP2040 MCU y la funcionalidad NFC está garantizada gracias al PN7150.

BomberCat está diseñado para ser intuitivo para los usuarios. La comunicación entre dos dispositivos se establece de la forma más sencilla posible: acercándolos entre sí. La interfaz NFC puede operar en tres modos distintos:

*Modo de emulación de tarjeta:* donde BomberCat se comporta como una tarjeta inteligente o una etiqueta
En este modo, BomberCat emula una etiqueta NFC. No inicia la comunicación, solo responde a un lector NFC. Una aplicación típica del modo de emulación de tarjeta es cómo las personas usan NFC en sus teléfonos inteligentes para reemplazar varias tarjetas, insignias o etiquetas a la vez (usando el mismo teléfono para controles de acceso RFID, pagos sin contacto, etc.). El modo de emulación de tarjeta, sin embargo, no es útil solo para smartphones, sino para cualquier tipo de dispositivo portátil.

*Lectura/escritura:* donde BomberCat se comporta como un lector/grabador de NFC
Aquí, BomberCat se comunica con una etiqueta pasiva, una tarjeta inteligente NFC o un dispositivo NFC que funciona en modo de emulación de tarjeta. Puede leer o escribir en una etiqueta (aunque la lectura es un caso de uso más común porque las etiquetas a menudo estarán protegidas contra escritura). En este modo, HunterCat NFC genera el campo de RF, mientras que una etiqueta o tarjeta solo lo modula.

*Magspoof:* para interacciones más complejas
El modo Magspoof puede emular tarjetas de banda magnitica emulando los pulsos electromagnitcos de este tipo de tarjetas.

*WIFI:* El co-procesador ESP32 en modo WIFININA que le permitira hacer conexiones WiFi para usar protocolos HTTP o MQTT, que permitira probar ataques de relay y spoofing a larga distancia o en webserver locales.

## Characteristics:
- Procesador Cortex M0+
- USB C 2.0
- NFC Reader, Card and NFC Forum
- Arduino compatible
- 100% CircuitPython compatible
- UF2 Bootloader
- WIFI 
- (probably BLE)
- 1 LEDs status
- Open Hardware
- Magpoof coil
- Battery
- RF protocols supported
  - NFCIP-1, NFCIP-2 protocol 
  - ISO/IEC 14443A, ISO/IEC 14443B PICC, NFC Forum T4T modes via host interface
  - NFC Forum T3T via host interface
  - ISO/IEC 14443A, ISO/IEC 14443B PCD designed according to NFC Forum digital protocol T4T platform and ISO-DEP 
  - FeliCa PCD mode
  - MIFARE Classic PCD encryption mechanism (MIFARE Classic 1K/4K)
  - NFC Forum tag 1 to 5 (MIFARE Ultralight, Jewel, Open FeliCa tag, MIFAREDESFire
  - ISO/IEC 15693/ICODE VCD mode 
  - Includes NXP ISO/IEC14443-A and Innovatron ISO/IEC14443-B intellectual property licensing rights

<a href="https://electroniccats.com/store/bombercat/">
  <img src="https://electroniccats.com/wp-content/uploads/badge_store.png" width="200" height="104" />
</a>

# License

![OpenSourceLicense](https://github.com/ElectronicCats/AjoloteBoard/raw/master/OpenSourceLicense.png)

Electronic Cats invests time and resources providing this open source design, please support Electronic Cats and open-source hardware by purchasing products from Electronic Cats!

Designed by Electronic Cats.

Firmware released under an GNU AGPL v3.0 license. See the LICENSE file for more information.

Hardware released under an CERN Open Hardware Licence v1.2. See the LICENSE_HARDWARE file for more information.

Electronic Cats is a registered trademark, please do not use if you sell these PCBs.

Jul 2021
