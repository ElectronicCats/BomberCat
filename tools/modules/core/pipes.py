#!/usr/bin/env python3

# Electronic Cats
# pipes.py — named-pipe (FIFO) transport + a Wireshark launcher, so a live
# capture can be streamed straight into Wireshark's "-k -i <fifo>" as pcap.
# Ported from catnip's modules/core/pipes.py (CatSniffer) and adapted for
# BomberCat's NFCGate APDU capture (NFCGATE_PLAN.md Fase 8 / §16).
# Distributed as-is; no warranty is given.

import os
import platform
import threading
import subprocess
import logging
from pathlib import Path

logger = logging.getLogger("rich")

DEFAULT_PIPELINE_NAME = "fbombercat"
DEFAULT_UNIX_PATH = f"/tmp/{DEFAULT_PIPELINE_NAME}"
DEFAULT_WINDOWS_PATH = f"\\\\.\\pipe\\{DEFAULT_PIPELINE_NAME}"

# Windows named pipes need pywin32. Import it lazily so the module still loads
# (and the Unix path keeps working) on a box without it; only a Windows user who
# actually reaches WindowsPipe hits the error.
if platform.system().lower() == "windows":
    try:
        import win32pipe, win32file, pywintypes

        logger.info("[*] Windows library import done!")
    except:
        logger.error(
            "[bold red][X] Error[/bold red]: win32pipe, win32file, pywintypes modules not found. [yellow]Please install [bold]pywin32[/bold] package.[/yellow]"
        )
        exit(1)


def show_generic_error(title: str = "", e: object = "") -> None:
    logger.error(f"{title}: {e}")


class UnixPipe:
    """A POSIX FIFO Wireshark can read live. `open()` blocks until a reader
    (Wireshark) attaches, so it is normally called from a background thread and
    the caller waits on `ready_event`."""

    def __init__(self, path=DEFAULT_UNIX_PATH) -> None:
        self.pipe_path = path
        self.pipe_writer = None
        self.ready_event = threading.Event()
        self.create()

    def create(self):
        try:
            os.mkfifo(self.pipe_path)
            logger.info(f"[*] Pipeline created: {self.pipe_path}")
        except FileExistsError:
            logger.info("[-] Pipeline already exists.")
        except OSError as e:
            show_generic_error("Creating Pipeline", e)
            exit(1)

    def open(self, mode="ab") -> None:
        logger.info(f"[*] Check if exist: {self.pipe_path}")
        if not os.path.exists(self.pipe_path):
            self.create()
        try:
            # Opening a FIFO for write blocks until a reader is present.
            self.pipe_writer = open(self.pipe_path, mode, buffering=0)
            self.ready_event.set()
            logger.info(f"[*] Pipeline Open ({mode}): {self.pipe_path}")
        except Exception as e:
            show_generic_error("Opening Pipeline", e)
            exit(1)

    def read(self, size=1024) -> bytes:
        try:
            if self.pipe_writer:
                return self.pipe_writer.read(size)
            return b""
        except Exception:
            return b""

    def close(self) -> None:
        try:
            if self.pipe_writer:
                self.pipe_writer.close()
                self.pipe_writer = None
            self.ready_event.clear()
            logger.info(f"[*] Pipeline Closed: {self.pipe_path}")
        except Exception as e:
            show_generic_error("Closing Pipeline", e)

    def remove(self) -> None:
        try:
            if self.pipe_writer:
                self.pipe_writer.close()
                self.pipe_writer = None
            if os.path.exists(self.pipe_path):
                os.remove(self.pipe_path)
            logger.info(f"[*] Pipeline removed: {self.pipe_path}")
        except Exception as e:
            show_generic_error("Removing Pipeline", e)

    def write_packet(self, data: bytes) -> None:
        try:
            if self.pipe_writer:
                self.pipe_writer.write(data)
                self.pipe_writer.flush()
        except BrokenPipeError:
            # Wireshark went away; surface it so the capture loop can stop.
            show_generic_error("BrokenPipe", "Wireshark closed the pipe")
            self.remove()
            raise
        except Exception as e:
            show_generic_error("Writing Pipeline", e)


class WindowsPipe:
    """A Windows named pipe Wireshark can read live (mirror of UnixPipe)."""

    def __init__(self, path=DEFAULT_WINDOWS_PATH) -> None:
        self.pipe_path = path
        self.pipe_writer = None
        self.ready_event = threading.Event()
        self.create()

    def create(self):
        try:
            self.pipe_writer = win32pipe.CreateNamedPipe(
                self.pipe_path,
                win32pipe.PIPE_ACCESS_DUPLEX,
                win32pipe.PIPE_TYPE_BYTE | win32pipe.PIPE_WAIT,
                2,
                65536,
                65536,
                0,
                None,
            )
        except FileExistsError:
            logger.info("[-] Pipeline already exists.")
        except pywintypes.error as e:
            logger.error(f"[X] {e}")
            exit(1)

    def open(self) -> None:
        logger.info(f"[*] Waiting for a client on {self.pipe_path}.")
        try:
            win32pipe.ConnectNamedPipe(self.pipe_writer, None)
            self.ready_event.set()
            logger.info(f"[*] Pipeline Open: {self.pipe_path}")
        except pywintypes.error as e:
            if e.winerror == 535:  # ERROR_PIPE_CONNECTED
                self.ready_event.set()
                logger.info("[*] Client already connected")
            elif e.winerror == 232:  # ERROR_NO_DATA
                logger.warning("[!] Client connected and disconnected immediately")
                return
            else:
                show_generic_error("Opening Pipeline", e)
                raise

    def read(self, size=1024) -> bytes:
        try:
            if self.pipe_writer:
                _, available, _ = win32pipe.PeekNamedPipe(self.pipe_writer, 0)
                if available == 0:
                    return b""
                hr, data = win32file.ReadFile(self.pipe_writer, min(size, available))
                return data
            return b""
        except Exception:
            return b""

    def close(self) -> None:
        try:
            if self.pipe_writer:
                win32pipe.DisconnectNamedPipe(self.pipe_writer)
                win32file.CloseHandle(self.pipe_writer)
                self.pipe_writer = None
            self.ready_event.clear()
            logger.info(f"[*] Pipeline Closed: {self.pipe_path}")
        except Exception as e:
            show_generic_error("Closing Pipeline", e)

    def remove(self) -> None:
        try:
            if self.pipe_writer:
                # If ConnectNamedPipe is still pending in a background thread,
                # CloseHandle blocks until it completes. Unblock it first by
                # connecting a throwaway client so ConnectNamedPipe returns.
                if not self.ready_event.is_set():
                    try:
                        dummy = win32file.CreateFile(
                            self.pipe_path,
                            win32file.GENERIC_READ,
                            0,
                            None,
                            win32file.OPEN_EXISTING,
                            0,
                            None,
                        )
                        win32file.CloseHandle(dummy)
                    except Exception:
                        pass
                try:
                    win32pipe.DisconnectNamedPipe(self.pipe_writer)
                except Exception:
                    pass
                win32file.CloseHandle(self.pipe_writer)
                self.pipe_writer = None
            self.ready_event.clear()
            logger.info(f"[*] Pipeline removed: {self.pipe_path}")
        except Exception as e:
            show_generic_error("Removing Pipeline", e)

    def write_packet(self, data: bytes) -> None:
        try:
            win32file.WriteFile(self.pipe_writer, data)
            win32file.FlushFileBuffers(self.pipe_writer)
        except pywintypes.error as e:
            if e.winerror in (109, 232):  # broken pipe / no data
                logger.warning("[!] Wireshark disconnected")
                self.close()
            else:
                show_generic_error("Writing Pipeline", e)


class Wireshark(threading.Thread):
    def __init__(self, pipe_name=None, profile=None):
        super().__init__(daemon=True)
        self.system = platform.system()
        if pipe_name is None:
            self.pipe_name = (
                DEFAULT_WINDOWS_PATH if self.system == "Windows" else DEFAULT_UNIX_PATH
            )
        else:
            self.pipe_name = pipe_name
        self.profile = profile
        self.running = True
        self.wireshark_process: subprocess.Popen | None = None

    def get_wireshark_path(self):
        if self.system == "Windows":
            exe_path = Path("C:\\Program Files\\Wireshark\\Wireshark.exe")
            if not exe_path.exists():
                exe_path = Path("C:\\Program Files (x86)\\Wireshark\\Wireshark.exe")
        elif self.system == "Linux":
            exe_path = Path("/usr/bin/wireshark")
            if not exe_path.exists():
                exe_path = Path("/usr/local/bin/wireshark")
        elif self.system == "Darwin":
            exe_path = Path("/Applications/Wireshark.app/Contents/MacOS/Wireshark")
        else:
            show_generic_error("Unsupported OS", "We don't support this OS yet.")
            return None
        return exe_path

    def get_wireshark_pipepath(self):
        return self.pipe_name

    def get_wireshark_cmd(self):
        exe_path = self.get_wireshark_path()
        fifo_path = self.get_wireshark_pipepath()
        cmd = [str(exe_path), "-k", "-i", fifo_path]
        if self.profile:
            cmd = [str(exe_path), "-k", "-i", fifo_path, "-C", self.profile]
        return cmd

    def run(self):
        cmd = self.get_wireshark_cmd()
        try:
            self.wireshark_process = subprocess.Popen(cmd)
            # Wait for the process to finish, otherwise the thread exits immediately
            self.wireshark_process.wait()
        except Exception as e:
            show_generic_error("Can't start Wireshark", e)
