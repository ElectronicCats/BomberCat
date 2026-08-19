# Guide — Rooting an Android phone for NFCGate (Mode 2)

> **Read this in another language:** [Español](guia-rooteo-android-nfcgate.md)

> **Companion manual to the [BomberCat User Guide](guia-usuario-bombercat.en.md).**
> You only need this document if you are going to use **Mode 2** (BomberCat
> *reader* + phone with NFCGate in *card*/HCE mode), which **requires a rooted
> phone**. If your phone is not rooted and you don't want to root it, **use
> Mode 3** from the main guide: it does the same thing with the roles reversed
> and **does not require root**.

---

## ⚠️ Warnings — READ THEM BEFORE YOU START

> [!WARNING]
> ## 🔴 Rooting your phone is a risky process and, in practice, irreversible
>
> - **IT ERASES ALL YOUR DATA.** Unlocking the *bootloader* performs a
>   **factory reset**: photos, messages, apps, and accounts are lost. **Make a
>   full backup before you start.**
> - **YOU CAN BRICK THE PHONE.** Flashing the wrong image or interrupting the
>   process can leave the phone unable to boot. The risk is real.
> - **IT VOIDS THE WARRANTY** of most manufacturers and may be permanently
>   recorded on the phone.
> - **IT REDUCES THE DEVICE'S SECURITY.** A rooted phone is more vulnerable.
>   Banking, payment (Google Pay), streaming, or corporate apps may **stop
>   working** (SafetyNet / Play Integrity).
> - **USE A PHONE DEDICATED TO TESTING**, never your personal phone or one with
>   data you care about.
>
> > **You are solely responsible.** Neither Electronic Cats nor the authors of
> > this guide are responsible for lost data, damaged devices, or any
> > consequence arising from following these steps.

> [!IMPORTANT]
> These steps are a **general reference** based on a specific phone
> (Google Pixel style / "stock" Android). **Every brand and model is
> different.** Before you start, look for the specific guide for **your exact
> model** (for example on forums like XDA Developers). Some manufacturers
> (certain Samsung, Huawei, etc.) **block bootloader unlocking** and cannot be
> rooted this way.

---

## Prerequisites

- An Android phone **dedicated to testing** (ideally a Pixel or similar with
  stock Android, which is the best supported).
- A **PC** (Windows, Linux, or Mac) with a USB port.
- A **USB data cable** (not charge-only).
- An Internet connection to download the factory image.
- **Time and patience**: don't do this in a hurry or with a low battery (>50%).

---

## Part 1 — Rooting the phone with Magisk

### Stage 1: Preparation

1. **Enable Developer Options.**
   Go to **Settings → About phone** and tap **seven times** on
   *Build Number*. A notice will appear saying you are now a developer.

2. **Enable OEM unlocking and USB debugging.**
   Go to **Settings → System → Developer options** and enable:
   - **OEM unlocking**.
   - **USB debugging**.

   > [!WARNING]
   > From the next step onward, **all data** on the phone will be **erased**.
   > Make sure you have the backup done.

3. **Install the tools on the PC.**
   - The **Android USB drivers** from your manufacturer (especially on Windows).
   - Google's **SDK Platform-Tools** (they contain the **ADB** and
     **Fastboot** executables). Download them from the official Android website:
     https://developer.android.com/tools/releases/platform-tools#downloads
     and unzip them into an easy-to-find folder (for example
     `platform-tools`).

### Stage 2: Unlock the bootloader

1. Connect the phone to the PC with the USB cable.
2. Open a terminal **inside the `platform-tools` folder** and check that the PC
   sees the phone:
   ```sh
   adb devices
   ```
   The first time, the phone will ask you to **authorize USB debugging** from
   this computer: accept. Your device should appear in the list.
3. Reboot the phone into *bootloader* mode (Fastboot):
   ```sh
   adb reboot bootloader
   ```
4. Unlock the bootloader:
   ```sh
   fastboot flashing unlock
   ```
   > On some models the command is `fastboot oem unlock`.

   A **confirmation** will appear on the phone's screen: use the volume buttons
   to select **unlock** and the power button to confirm. **At this moment the
   phone is erased.**

### Stage 3: Patch the image with Magisk

1. **Identify the exact build number** of your phone
   (Settings → About phone → Build Number).
2. **Download the Factory Image that matches EXACTLY** that build, from the
   manufacturer's official website.

   > [!WARNING]
   > The image **must match** your model and build. A wrong image can brick the
   > phone.

3. **Extract the `boot.img` file** from inside the factory image (it usually
   comes compressed in several layers: unzip until you find `boot.img`).
4. **Transfer `boot.img` to the phone** (to the Downloads folder, for example).
5. Install the **Magisk** app on the phone (from its official repository) and
   open it.
6. In Magisk, tap **Install → Select and Patch a File**, choose the `boot.img`
   you copied. Magisk will generate a **`magisk_patched-*.img`** file (it
   injects the superuser binaries into the image).

### Stage 4: Flash the patched image

1. **Copy the `magisk_patched-*.img` back to the PC**, inside the
   `platform-tools` folder.
2. Reboot the phone into Fastboot mode (if it isn't already):
   ```sh
   adb reboot bootloader
   ```
3. Flash the patched image (use the **exact name** of your file):
   ```sh
   fastboot flash boot magisk_patched-XXXXX.img
   ```
4. Reboot the phone:
   ```sh
   fastboot reboot
   ```

### Stage 5: Verification

1. Install the **Root Checker** app from the Play Store.
2. Open it and check that it confirms the device has **root (superuser)
   access** correctly.

If Root Checker confirms root, the phone is rooted. ✅

---

## Part 2 — Setting up NFCGate (Zygisk + LSPosed)

For NFCGate to work in *card*/HCE mode it needs its native module, which is
installed via **Zygisk** (inside Magisk) and **LSPosed**.

1. **Enable Zygisk in Magisk.**
   Open **Magisk → Settings** and enable **Zygisk**. Reboot the phone if
   prompted.

2. **Download LSPosed (Zygisk variant).**
   Go to the official *releases* page:
   `https://github.com/LSPosed/LSPosed/releases`
   and download the **`.zip` corresponding to Zygisk**.

   > [!IMPORTANT]
   > Download the **Zygisk** variant, not the Riru one. It must match how you
   > enabled the module (Zygisk inside Magisk).

3. **Install LSPosed as a module in Magisk.**
   Open **Magisk → Modules → Install from storage**, select the LSPosed `.zip`
   and, when it finishes, **reboot the phone**.

4. **Grant permissions in LSPosed.**
   Open the **LSPosed** app. Enable the corresponding module and grant the
   **necessary permissions to NFCGate** (and to **NFC** in general in the
   system settings).

5. **Verify in NFCGate.**
   Open the **NFCGate** app and go to the **Status** section: check that **all
   the necessary permissions and components appear as correct** (no red
   warnings). If the status is green, NFCGate is ready for **Mode 2**.

---

> **Go back to the main guide:** once the phone is rooted and NFCGate shows its
> *status* as correct, continue with
> [Mode 2 of the BomberCat User Guide](guia-usuario-bombercat.en.md#mode-2--bombercat-reader--phone-with-nfcgate-in-card-mode).

> **Reminder:** only root a phone **dedicated to testing**, and use BomberCat
> only in **authorized** audits. The responsibility is **entirely yours**.
