"""BLE-Pakete des RodTest-Sketches lesen. Unter Windows: py reader.py"""
import asyncio
import struct

DEVICE_NAME = "Heltec-Random-Data"
SERVICE_UUID = "2bcae4c0-7d55-4ce6-a628-823f5f3bdc01"
CHAR_UUID = "2bcae4c0-7d55-4ce6-a628-823f5f3bdc02"

MAP_NAMES = {1: "FishingRod", 2: "Boat", 3: "Global"}
BUTTONS = {
    (1, 1): "Throw", (1, 5): "CancelCast",
    (1, 6): "SwitchBait", (1, 7): "ToggleReady",
    (2, 1): "Anchor", (2, 2): "Interact", (2, 3): "ToggleFishing",
    (2, 4): "Boost", (3, 0): "Pause",
}
AXES = {(2, 0): "Drive", (1, 8): "CastSpeed"}


def normalized(raw):
    return max(-1.0, min(1.0, raw / 32767.0))


def decode_packet(data):
    """Liefert (Aktionspfad, Rell als 0/1, sonst bool/float/(x, y)) für genau eine Notification."""
    if not data:
        raise ValueError("leeres Paket")
    header = data[0]
    kind = header & 3
    key = ((header >> 2) & 3, header >> 4)
    size = (1, 1, 3, 5)[kind]
    if len(data) != size:
        raise ValueError(f"Header 0x{header:02X} erwartet {size} Byte, erhalten: {len(data)}")

    if kind <= 1 and key == (1, 0):
        action, value = "Rell", kind
    elif kind <= 1 and key in BUTTONS:
        action, value = BUTTONS[key], bool(kind)
    elif kind == 2 and key in AXES:
        action = AXES[key]
        value = normalized(struct.unpack_from("<h", data, 1)[0])
    elif kind == 3 and key == (1, 2):
        action = "Aim"
        x, y = struct.unpack_from("<hh", data, 1)
        value = (normalized(x), normalized(y))
    else:
        raise ValueError(f"unbekannte Aktion oder falscher Typ für Header 0x{header:02X}")
    return f"{MAP_NAMES[key[0]]}.{action}", value


def on_packet(_characteristic, data):
    raw = data.hex(" ").upper()
    try:
        action, value = decode_packet(data)
    except ValueError as error:
        print(f"[{raw}] Ungültig: {error}")
        return
    if action == "FishingRod.Rell":
        display = f"{value} (Kurbel dreht sich)" if value else "0 (Kurbel inaktiv)"
    elif isinstance(value, bool):
        display = "gedrückt" if value else "losgelassen"
    elif isinstance(value, tuple):
        display = f"x={value[0]:+.3f} y={value[1]:+.3f}"
    else:
        display = f"{value:+.3f}"
    print(f"[{raw}] {action} {display}")


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
