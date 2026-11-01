"""Verify a two-realm alliance, product transfer, trade and clean shutdown on Linux."""

import os
from pathlib import Path
import random
import selectors
import signal
import socket
import struct
import subprocess
import tempfile
import time

from prepare_local import ROOT, REALMS, prepare


def available_port_block():
    for _ in range(50):
        base = random.randrange(20000, 50000)
        sockets = []
        try:
            for offset in range(len(REALMS)):
                sock = socket.socket()
                sockets.append(sock)
                sock.bind(("127.0.0.1", base + offset))
            return base
        except OSError:
            continue
        finally:
            for sock in sockets:
                sock.close()
    raise RuntimeError("No free local port block found.")


def inventory(path):
    record = struct.Struct("@100sif")
    data = path.read_bytes()
    if len(data) % record.size:
        raise ValueError("Inventory layout does not match this host's native C ABI.")
    return {
        name.split(b"\0", 1)[0].decode(): amount
        for name, amount, _weight in record.iter_unpack(data)
    }


def main():
    if not (ROOT / "Maester").is_file():
        raise SystemExit("Build first: make")
    selector = selectors.DefaultSelector()
    processes, logs = {}, {}
    transcript = []

    def send(realm, command):
        transcript.append(f"[{realm}] $ {command}")
        processes[realm].stdin.write((command + "\n").encode())
        processes[realm].stdin.flush()

    def wait_for(realm, text, timeout=12):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if text in logs[realm]:
                return
            for key, _ in selector.select(0.1):
                chunk = os.read(key.fd, 65536)
                if chunk:
                    logs[key.data] += chunk.decode(errors="replace")
                else:
                    selector.unregister(key.fileobj)
        raise AssertionError(f"{realm}: expected {text!r}\n{logs[realm]}")

    with tempfile.TemporaryDirectory(prefix="citadel-check-") as temp:
        folder = prepare(Path(temp) / "realms", available_port_block())
        try:
            for realm in ("Stark", "Lannister"):
                process = subprocess.Popen(
                    [
                        str(ROOT / "Maester"),
                        str(folder / realm / "config.dat"),
                        str(folder / realm / "inventory.db"),
                    ],
                    cwd=ROOT,
                    stdin=subprocess.PIPE,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    start_new_session=True,
                )
                processes[realm] = process
                logs[realm] = ""
                os.set_blocking(process.stdout.fileno(), False)
                selector.register(process.stdout, selectors.EVENT_READ, realm)
                wait_for(realm, f"Maester of {realm} initialized.")

            send("Stark", "LIST REALMS")
            wait_for("Stark", "- Lannister")
            send("Stark", "ENVOY STATUS")
            wait_for("Stark", "FREE")
            send("Stark", "PLEDGE Lannister stark.png")
            wait_for("Lannister", "Alliance request received from Stark.")
            send("Lannister", "PLEDGE RESPOND Stark ACCEPT")
            wait_for("Stark", "Alliance with Lannister forged successfully!")
            transcript.append("[Stark] Alliance with Lannister forged successfully!")
            send("Stark", "LIST PRODUCTS Lannister")
            wait_for("Stark", "Listing products from Lannister:")
            buyer_path = folder / "Stark" / "inventory.db"
            seller_path = folder / "Lannister" / "inventory.db"
            buyer_before, seller_before = inventory(buyer_path), inventory(seller_path)
            product = next(name for name, amount in seller_before.items() if amount > 1)
            send("Stark", "START TRADE Lannister")
            wait_for("Stark", "(trade)>")
            send("Stark", f"ADD {product} 1")
            send("Stark", "SEND")
            wait_for("Stark", "Order accepted by Lannister. Stock updated.")
            assert inventory(seller_path)[product] == seller_before[product] - 1
            assert inventory(buyer_path)[product] == buyer_before.get(product, 0) + 1
            transcript.append("[Stark] Order accepted by Lannister. Stock updated.")
            transcript.append(
                "[Check] Sender and receiver inventories persisted the expected quantities."
            )
            for realm in processes:
                send(realm, "EXIT")
            for process in processes.values():
                assert process.wait(timeout=5) == 0
            transcript.append("[Check] Both realm processes exited successfully.")
        finally:
            for process in processes.values():
                if process.poll() is None:
                    os.killpg(process.pid, signal.SIGTERM)
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid, signal.SIGKILL)
                        process.wait(timeout=3)
                process.stdin.close()
                process.stdout.close()
            selector.close()
    print("\n".join(transcript))
    print(
        "PASS: startup, envoys, alliance/file transfer, remote products, trade persistence and shutdown."
    )


if __name__ == "__main__":
    main()
