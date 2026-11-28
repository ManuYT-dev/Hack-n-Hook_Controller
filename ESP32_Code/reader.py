"""BLE-Pakete des RodTest-Sketches lesen. Unter Windows: py reader.py"""
import asyncio
import struct

DEVICE_NAME = "Heltec-Random-Data"
SERVICE_UUID = "2bcae4c0-7d55-4ce6-a628-823f5f3bdc01"
CHAR_UUID = "2bcae4c0-7d55-4ce6-a628-823f5f3bdc02"

MODES = {0: "Rell", 1: "CastSpeed", 3: "ButtonPress"}
PACKET = struct.Struct("<Bh")  # Header, signed int16 little-endian


def decode_packet(data):
    """Dekodiert eine BLE-Notification als (Modusname, aktiv, Wert)."""
    if len(data) != PACKET.size:
        raise ValueError(f"erwartet: 3 Byte, erhalten: {len(data)}")
    header, value = PACKET.unpack(data)
    if header & 0xF8:
        raise ValueError(f"reservierte Bits gesetzt: 0x{header:02X}")
    mode = (header >> 1) & 3
    if mode not in MODES:
        raise ValueError(f"unbelegter Modus: {mode:02b}")
    return MODES[mode], bool(header & 1), value


def on_packet(_characteristic, data):
    raw = data.hex(" ").upper()
    try:
        mode, active, value = decode_packet(data)
    except ValueError as error:
        print(f"[{raw}] Ungültig: {error}")
        return
    display = f"{value:+d}" if mode == "Rell" and value else str(value)
    print(f"[{raw}] {mode:<11} aktiv={int(active)}  wert={display}")


def matches_rod(device, advertisement):
    return (
        DEVICE_NAME in (advertisement.local_name, device.name)
        or SERVICE_UUID in (u.lower() for u in advertisement.service_uuids)
    )


async def main():
    try:
        from bleak import BleakClient, BleakScanner
        from bleak.exc import BleakError
    except ModuleNotFoundError as error:
        if error.name != "bleak":
            raise
        raise SystemExit("Bleak fehlt. Installation: py -m pip install bleak") from None

    while True:
        try:
            print(f"Suche {DEVICE_NAME} ...")
            device = await BleakScanner.find_device_by_filter(matches_rod, timeout=8.0)
            if device is None:
                print("Kein Heltec gefunden. Suche erneut ...")
                continue
            disconnected = asyncio.Event()
            async with BleakClient(device, disconnected_callback=lambda _: disconnected.set()) as client:
                await client.start_notify(CHAR_UUID, on_packet)
                print(f"Empfange Pakete von {device.address}. Beenden mit Strg+C.")
                await disconnected.wait()
            print("Verbindung getrennt.")
        except (BleakError, OSError, asyncio.TimeoutError) as error:
            print(f"Bluetooth-Fehler: {error}")
        await asyncio.sleep(2)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
