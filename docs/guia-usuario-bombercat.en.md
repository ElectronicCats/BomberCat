# User Guide — BomberCat for NFC Audits

> **Read this in another language:** [Español](guia-usuario-bombercat.md)

---

## A. Introduction and Legal Warning

### What is BomberCat?

**BomberCat** is a security tool that works with contactless cards
(**NFC** — the same ones used for payments, access control, or identification)
and with magnetic stripe. With it, an auditor can **read, emulate, or relay**
a card to check whether a system is vulnerable.

This guide specifically covers the **relay** function: making a card that is
in one place "appear" in front of a reader that is somewhere else, joining the
two ends over WiFi. It is the classic test to demonstrate whether a reader
accepts a card that is not actually present.

> [!WARNING]
> ## ⚠️ LIABILITY NOTICE — READ IT BEFORE USING
>
> **BomberCat is designed EXCLUSIVELY for authorized security audits and
> testing in controlled environments.**
>
> - Use it **only** on systems, cards, and terminals that **belong to you** or
>   for which you have **explicit written permission** from the owner.
> - Using this tool on third-party cards, accounts, access credentials, or
>   terminals without authorization is, in most countries, a **crime**.
>
> > **The user is solely responsible for how they use the device and for any
> > legal, ethical, or other consequences that may arise from its misuse.**
>
> Electronic Cats and the authors of this firmware are **not responsible** for
> any unauthorized use or for the damages arising from it. The device does
> **not** authorize you to use credit cards or to carry out financial
> transactions that are not legally authorized.

**Before each test, make sure you have:**

- [ ] Written authorization from the owner of the system or the card.
- [ ] A defined scope (what is tested, when, and where).
- [ ] A controlled environment, without affecting third-party people or services.

---

## B. Specifications and Modes of Use

### What is always required? (for all three modes)

The relay always joins **two ends** through a small intermediary program
called the **NFCGate server**. One of the ends acts as a **reader** (reads a
real card) and the other as a **card** (presents itself to a terminal). No
matter which mode you choose: you always need these three pieces.

| Piece | What it is for |
|---|---|
| **NFCGate server** | A program that runs on a PC and forwards the data between the two ends. |
| **WiFi network** | The two ends and the server PC must be on the **same WiFi network**. |
| **Two ends** | One in *reader* mode and the other in *card* mode (depending on the chosen mode). |

**One-time installation of the control software (on your PC, only once):**

```sh
cd tools
python3 -m pip install -r requirements.txt   # installs the dependencies
python3 bombercat.py --help                  # checks that it works
```

> On Linux, for the PC to "see" the BomberCat over USB you may need to add
> your user to the `dialout` group:
> `sudo usermod -aG dialout $USER` and log back in.

**Start the NFCGate server (on the PC, before any test):**

```sh
bombercat testserver run              # starts the server, listens on port 5566
```

Leave it running in its own terminal window. Note the **IP address** of that
PC (for example `192.168.1.5`); you will need it to configure the ends.
On Windows/Mac/Linux you can see the IP with `ipconfig` or `ip a`.

> 💡 **On Linux**, the fastest way to find the IP of the computer running the
> server is the `hostname -I` command: it prints the machine's IP address(es)
> directly. The first one is usually the one for your WiFi network (the one
> you will put in `--server`, for example `192.168.1.5`).

**Key concepts that recur in every mode:**

- **Server (`--server`)**: the IP of the PC and the port, for example
  `192.168.1.5:5566`. **Both ends must point to the same server.**
- **Session (`--session`)**: a number from 1 to 255 that pairs the two ends.
  **Both ends must use exactly the same session number.**
- **Role (`--role`)**: `reader` (reads a physical card) or `card` (presents
  itself as a card to a terminal). **One end must be `reader` and the other `card`.**

> ℹ️ **About timing:** a relay transaction typically takes **12–15 seconds**.
> This is normal because of the WiFi + server path; it does not mean something
> is broken. Keep the card and the terminal still during that time.

---

### One-time preparation: flash the relay firmware onto the BomberCat

Every BomberCat you will use as an end (`reader` or `card`) needs the **NFCGate
relay firmware** flashed onto it. This is done **once** per device (or when a new
firmware version is released). If you bought the BomberCat with different firmware,
or you are not sure, follow these steps.

**What you need:**

- The BomberCat connected to your PC over USB-C.
- The [Arduino IDE](https://www.arduino.cc/en/software) 2.x **or** `arduino-cli`.
- The **Electronic Cats Mbed OS RP2040** board package and the **WiFiNINA** and
  **Electronic Cats PN7150** libraries (the Arduino IDE installs them from its
  board and library managers).

**With `arduino-cli` (the fastest way):**

```sh
cd firmware/NFCGate
arduino-cli compile -b electroniccats:mbed_rp2040:bombercat --library ../core .
arduino-cli upload  -b electroniccats:mbed_rp2040:bombercat -p /dev/ttyACM0 .
```

> Replace `/dev/ttyACM0` with your BomberCat's port (on Windows it will be
> something like `COM5`). The WiFiNINA *"architecture may be incompatible"*
> warnings are **normal** on the BomberCat, not an error.

**With the Arduino IDE:** open `firmware/NFCGate/NFCGate.ino`, select the board
**"Electronic Cats BomberCat"** and, so it resolves `#include <BomberCatCore.h>`,
create a link to the `firmware/core` folder inside `~/Arduino/libraries`. Then
click **Upload**.

**Check that it was flashed** (with the control software already installed):

```sh
bombercat device info      # should reply: fw 0.9.7, state idle
```

If it replies with the version and `state idle`, the BomberCat is ready. Repeat
the process on the **second** BomberCat if you are going to use Mode 1.

> The full technical details (pins, libraries, build options) are in
> [`firmware/NFCGate/README.md`](../firmware/NFCGate/README.md).

---

### Mode 1 — BomberCat Reader + BomberCat Card

**Purpose:** the most reliable and recommended mode. Two BomberCats: one reads
a physical card and the other presents it to a terminal, in another location,
over WiFi. Ideal for demonstrating an end-to-end relay without depending on a
phone. *(This mode is validated on hardware.)*

**Minimum requirements:**

- 2 BomberCat devices with the NFCGate relay firmware.
- 1 PC with the NFCGate server running (previous step).
- 1 physical card to read and 1 terminal/reader to test against.
- Everything on the same WiFi network.

**Steps:**

Connect **both** BomberCats over USB to the same PC. Each one gets a number
(`#1`, `#2`); you can see which is which with:

```sh
bombercat device list                # shows the connected BomberCats
bombercat identify -d 1              # blinks the LED of #1 to recognize it
```

1. **Configure the WiFi on both** (use your real network):
   ```sh
   bombercat config wifi -d 1 --ssid MyWiFiNetwork --pass 'mypassword'
   bombercat config wifi -d 2 --ssid MyWiFiNetwork --pass 'mypassword'
   ```
2. **Configure the roles** (one `reader`, the other `card`; **same session**):
   ```sh
   bombercat config nfcgate -d 1 --server 192.168.1.5:5566 --session 42 --role reader
   bombercat config nfcgate -d 2 --server 192.168.1.5:5566 --session 42 --role card
   ```
3. **Place the physical card** on BomberCat #1 (the `reader`).
4. **Start both**:
   ```sh
   bombercat run -d 1
   bombercat run -d 2
   ```
5. **Check that they connected**:
   ```sh
   bombercat status -d 1        # should show a link and a "peer" present
   ```
6. **Bring the terminal close** to BomberCat #2 (the `card`). Wait 12–15 s for
   the transaction to cross.
7. To see the live detail (logs and APDUs) while you test:
   ```sh
   bombercat monitor -d 1
   ```
8. **When finished**, stop both:
   ```sh
   bombercat stop -d 1
   bombercat stop -d 2
   ```

**Safety recommendations for this mode:**

- Keep the physical card **still and firmly resting** on the `reader`
  throughout the test; if it separates, the relay is cut off.
- Verify that both BomberCats are on the **same session**; a different number
  means they will never pair.
- Do not leave the BomberCats running unattended; stop them when done.

---

### Mode 2 — BomberCat Reader + Phone with NFCGate in Card mode

**Purpose:** use an Android phone with the **NFCGate** app as the end that
presents itself to the terminal (*card*/HCE mode), while the BomberCat reads a
physical card.

> [!IMPORTANT]
> ## 🔴 This mode requires a ROOTED phone
>
> Due to an Android limitation, a **normal (non-rooted)** phone **cannot**
> emulate a payment card toward a terminal: its card mode only responds to a
> test card, so the EMV terminal never reaches the app and **no data crosses**.
>
> This mode **only works** with a **rooted** phone that has the **native
> NFCGate module (Xposed / patched `nfcd`)** installed. If your phone is not
> rooted, **use Mode 3 instead** — it does the same thing with the roles
> reversed and works on normal phones.
>
> 📱 **Don't know how to root the phone or set up NFCGate?** Follow the
> [Guide to rooting an Android phone for NFCGate](guia-rooteo-android-nfcgate.en.md),
> which explains step by step how to root with Magisk and install the NFCGate
> module (Zygisk + LSPosed), with their corresponding warnings.

**Minimum requirements:**

- 1 BomberCat with the relay firmware (it will be the `reader`).
- 1 **rooted** Android phone with the **NFCGate** app and its native module.
- 1 PC with the NFCGate server running.
- 1 physical card to read and 1 terminal to test against.
- Everything on the same WiFi network.

**Steps:**

1. **Configure the BomberCat as `reader`** (WiFi + server + session):
   ```sh
   bombercat config wifi    --ssid MyWiFiNetwork --pass 'mypassword'
   bombercat config nfcgate --server 192.168.1.5:5566 --session 42 --role reader
   ```
2. **On the phone, open the NFCGate app** and in its network settings enter
   **the same server address** (the PC's IP) and **the same session number** (42).
3. In the app, **choose emulation/card mode** (so the phone acts as the card
   in front of the terminal) and join the session.
4. **Place the physical card** on the BomberCat and start it:
   ```sh
   bombercat run
   bombercat status        # check that the "peer" (the phone) appears
   ```
5. **Bring the terminal close to the phone.** Wait 12–15 s for the transaction
   to cross.
6. **When finished**, `bombercat stop` and stop the relay in the app.

**Safety recommendations for this mode:**

- Confirm that the app shows the relay **active in emulation mode** before
  bringing the terminal close.
- A rooted phone is more vulnerable; use it dedicated to testing, not as your
  personal phone.
- If nothing crosses, it is almost always because the phone **is not rooted**
  or the NFCGate module is not active → switch to **Mode 3**.

---

### Mode 3 — BomberCat Card + Phone with NFCGate in Reader mode

**Purpose:** the equivalent of Mode 2 but **compatible with normal
(non-rooted) phones**. Here the phone acts as the **reader** (reads a physical
card with its NFC) and the BomberCat presents itself as a **card** to the
terminal. This is the recommended way if you want to use a phone.

**Minimum requirements:**

- 1 BomberCat with the relay firmware (it will be the `card`).
- 1 **normal** Android phone with the **NFCGate** app (no root required).
- 1 PC with the NFCGate server running.
- 1 physical card (the phone will read it) and 1 terminal to test against.
- Everything on the same WiFi network.

**Steps:**

1. **Configure the BomberCat as `card`** (WiFi + server + session):
   ```sh
   bombercat config wifi    --ssid MyWiFiNetwork --pass 'mypassword'
   bombercat config nfcgate --server 192.168.1.5:5566 --session 42 --role card
   ```
2. **On the phone, open NFCGate**, enter **the same server address** and **the
   same session number** (42), and **choose reader mode**. Join the session.
3. **Bring the physical card close to the phone** (the phone is the one
   reading it).
4. **Start the BomberCat**:
   ```sh
   bombercat run
   bombercat status        # check that the "peer" (the phone) appears
   ```
5. **Bring the terminal close to the BomberCat.** Wait 12–15 s for the
   transaction to cross. To see the live detail:
   ```sh
   bombercat monitor
   ```
6. **When finished**, `bombercat stop` and stop the relay in the app.

**Safety recommendations for this mode:**

- Keep the physical card **firmly resting on the phone** during the test.
- As in the other modes, verify that **server and session match** on both ends.
- It is the preferred mode for demonstrations with a phone because it does not
  require root.

---

## C. Common Troubleshooting

| Problem | Likely cause | Solution |
|---|---|---|
| **No data crosses between the ends** | Different session on each end, or they point to different servers. | Check that `--session` and `--server` are **identical** on both ends. |
| **`bombercat device list` does not show the BomberCat** | Charge-only USB cable, or missing serial port permission (Linux). | Use a **data** cable; on Linux add your user to the `dialout` group and log back in. |
| **`status` never shows a "peer"** | The other end has not joined, or there is no WiFi. | Confirm that the server is running, that both ends are on the **same WiFi**, and that you started both (`run`). |
| **With the phone in card mode (Mode 2) nothing happens** | The phone **is not rooted** (Android HCE limitation). | Use **Mode 3**, which works on normal phones. |
| **The transaction takes "too long" (10–15 s)** | This is the normal time for the WiFi + server path. | It is not a failure: keep the card and terminal **still** until it finishes. |
| **The relay cuts off midway** | The card separated from the reader, or the WiFi dropped. | Rest the card again; move the devices closer to the router; `run` again. |

---

## D. Glossary of Terms

- **NFC (Near Field Communication):** contactless communication technology at a
  few centimeters. It is what "contactless" payment cards, card-based access,
  and mobile payment use.
- **Card / Emulation (HCE):** making a device **behave like a card** in front
  of a reader. On a phone it is called *HCE* (Host Card Emulation).
- **Reader:** the role of the one that **reads** a card (generates the NFC
  field and asks it questions). The payment terminal is a reader.
- **Terminal / PoS:** the device that charges or validates (for example, a
  store terminal or an access-control reader).
- **Relay:** a technique that **forwards** the conversation between a card and
  a reader that are **in different places**, as if they were together. It is
  what BomberCat does in this guide.
- **Replay:** replaying a previously captured communication. Related, but not
  what this flow does (here the forwarding is **live**).
- **Security audit:** an **authorized** test to find out whether a system is
  vulnerable, with the owner's permission and within an agreed scope.
- **APDU:** each "message" exchanged between card and reader. In the monitor
  you will see them as sequences of bytes in hexadecimal.
- **NFCGate server:** the intermediary program that forwards the data between
  the two ends of the relay over the network.
- **Session:** a number (1–255) that **pairs** the two ends on the server.
  Both must use the same one.
- **Role:** the part played by each end: `reader` (reads a physical card) or
  `card` (presents itself as a card to a terminal).
- **Firmware:** the internal program of the BomberCat. Here, the NFCGate-
  compatible relay firmware.
- **NFCGate:** an Android project/app for capturing and relaying NFC, with
  which the BomberCat is compatible.

---

> **Final reminder:** use BomberCat only in **authorized** tests. The
> responsibility for its use is **entirely yours**.
