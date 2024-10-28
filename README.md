# BomberCat

<p align=center>
<a href="https://github.com/ElectronicCats/BomberCat/wiki">
  <img src="https://github.com/ElectronicCats/BomberCat/assets/107638696/354ce958-4d73-4198-bb89-8b5e16c7cd0a" width=70% />
</a>
</p>

<p align=center>
<a href="https://electroniccats.com/store/bombercat/">
  <img src="https://github.com/ElectronicCats/BomberCat/assets/107638696/f9bbb579-0f5c-4f9f-a3ca-376aa8895035" width="200" height="104" />
</a>
<a href="https://github.com/ElectronicCats/BomberCat/wiki">
  <img src="https://github.com/ElectronicCats/flipper-shields/assets/44976441/6aa7f319-3256-442e-a00d-33c8126833ec" width="200" height="104" />
</a>
</p>

<p align=center>
Also available at distributors:
</p>
<p align=center>
<a href="https://labs.ksec.co.uk/product-category/electronic-cat/">
<img src="https://cdn.ksec.co.uk/ksec-solutions/ksec-W-BW-MV-small-clipped.png" width="200" />
</a>
</p>

BomberCat 💣 is the latest security tool that combines the most common card technologies: NFC technology (Near Field Communication) and magnetic stripe technology used in access control, identification, and banking cards.
Specially created to audit banking terminals, and identify NFC readers and sniffing tools, with this tool you can audit, read, or emulate magnetic stripes and NFC cards.

It also has an ESP32 co-processor with WiFiNINA firmware that allows it to make WiFi connections to use with HTTP or MQTT protocols, which will allow relay and spoofing attacks to be tested over long distances or on local web servers.

### Applications
BomberCat features an RP2040 MCU working along with the PN7150 (a recent generation NFC chip). 
The USB interface is provided by the RP2040 MCU and the NFC functionality is guaranteed by the PN7150.

BomberCat is designed to be intuitive for users. Easy to program using frameworks such as Arduino, Circuitpython, and Micropython.  We have prepared a series of examples with which you can start experimenting, check out the examples folder.

**Card emulation mode:** in which BomberCat behaves as a smart card or a tag. In this mode, BomberCat emulates an NFC tag. It does not initiate communication, it only responds to the NFC reader. 
A typical application of the card emulation mode is how people use NFC on their smartphones to replace several cards, badges, or tags at once (using the same smartphone for RFID access controls, contactless payments, etc). 
However, card emulation mode is not only beneficial for smartphones but any type of portable device.

**Read/Write:** in which BomberCat behaves as an NFC reader/writer. In this mode, BomberCat communicates with a passive tag, an NFC smart card, or an NFC device operating in the card emulation mode. It can read or write to a tag (although reading is a more common use case because tags will often be write-protected). In this mode, BomberCat generates an RF field, while a tag or card only modulates it.

**Magspoof:** for magnetic stripes interaction.
Magspoof mode can emulate magnetic stripe cards by emulating the electromagnetic pulses of this type of card.


## Characteristics:
- Cortex M0+ processor
- USB C 2.0
- NFC Reader, Card, and NFC Forum
- Arduino compatible
- 100% CircuitPython compatible
- UF2 Bootloader
- WIFI (WiFiNINA)
- BLE (ArduinoBLE)
- 1 LED status
- Open Hardware
- MagSpoof coil
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

## Requirements

- **Arduino IDE:** version 2.3.3
- **Board Package:** Electronic Cats Mbed OS RP2040 Boards version 2.0.0

 Libraries:
 
  - **PubSubClient:** version 2.8.0
  - **WiFiNINA:** version 1.8.14
  - **Electronic Cats PN7150:** version 2.1.0
  - **SerialCommand:** [Arduino-SerialCommand](https://github.com/kroimon/Arduino-SerialCommand) 

### **Linux Setup Instructions**
If you are using Linux, ensure the BomberCat is properly recognized by running the post-install script located at:

`~/.arduino15/packages/electroniccats/hardware/mbed_rp2040/2.0.0/post_install.sh`

This step is essential to ensure the board can be detected and the firmware uploaded successfully.

## Wiki and Getting Started

[Getting Started in our Wiki](https://github.com/ElectronicCats/BomberCat/wiki)

## Disclaimer
>[!IMPORTANT]
>BomberCat is a wireless penetration testing tool intended **solely for use in authorized security audits, where such usage is permitted by applicable laws and regulations**. Before utilizing this tool, it is crucial to ensure compliance with all relevant legal requirements and obtain appropriate permissions from the relevant authorities.
>
>The board **does not provide** any means or authorization to utilize credit cards or engage in any financial transactions that are not legally authorized. **Electronic Cats holds no responsibility for any unauthorized use of the tool or any resulting damages**.

-------

## How to contribute <img src="https://electroniccats.com/wp-content/uploads/2018/01/fav.png" height="35"><img src="https://raw.githubusercontent.com/gist/ManulMax/2d20af60d709805c55fd784ca7cba4b9/raw/bcfeac7604f674ace63623106eb8bb8471d844a6/github.gif" height="30">
 Contributions are welcome! 

Please read the document  [**Contribution Manual**](https://github.com/ElectronicCats/electroniccats-cla/blob/main/electroniccats-contribution-manual.md)  which will show you how to contribute your changes to the project.

✨ Thanks to all our [contributors](https://github.com/ElectronicCats/BomberCat/graphs/contributors)! ✨

See [**_Electronic Cats CLA_**](https://github.com/ElectronicCats/electroniccats-cla/blob/main/electroniccats-cla.md) for more information.

See the  [**community code of conduct**](https://github.com/ElectronicCats/electroniccats-cla/blob/main/electroniccats-community-code-of-conduct.md) for a vision of the community we want to build and what we expect from it.
 
### Supported by
 Thanks [Wallee](https://en.wallee.com/) for support this open source project
 
 <a href="https://en.wallee.com/">
  <img src="https://assets-global.website-files.com/618247f2e428ac0537753ad7/618247f2e428acc566753b08_wallee_logo_RGB_turquoise.svg" width=30% />
</a>

## Maintainer

<p align=center>
<a href="https://github.com/sponsors/ElectronicCats">
  <img src="https://electroniccats.com/wp-content/uploads/2020/07/Badge_GHS.png" width="200" height="104" />
</a>
</p>

Electronic Cats invests time and resources providing this open-source design, please support Electronic Cats and open-source hardware by purchasing products from Electronic Cats!

### License
<p>
<img src= "https://github.com/ElectronicCats/AjoloteBoard/raw/master/OpenSourceLicense.png" width=30%>
</p>

Electronic Cats invests time and resources in providing this open-source design, please support Electronic Cats and open-source hardware by purchasing products from Electronic Cats!

Designed by Electronic Cats.

Firmware released under an GNU AGPL v3.0 license. See the LICENSE file for more information.

Hardware released under an CERN Open Hardware Licence v1.2. See the LICENSE_HARDWARE file for more information.

Electronic Cats is a registered trademark, please do not use if you sell these PCBs.

Jul 2022
