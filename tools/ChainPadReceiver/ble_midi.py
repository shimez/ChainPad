"""BLE-MIDI channel-message decoder for the Phase 1 development receiver.

Handles timestamped channel messages, running status and realtime bytes.
System Common / SysEx are deliberately rejected (not emitted by ChainPad).
BLE timestamps are transport metadata; the UI measures PC callback arrival.
"""
MIDI_SERVICE = "03b80e5a-ede8-4b33-a751-6ce34ec4c700"
MIDI_CHARACTERISTIC = "7772e5db-3868-4112-a1a9-f2669d106bf3"


def decode_packet(packet: bytes) -> list[bytes]:
    if len(packet) < 2 or packet[0] & 0xC0 != 0x80:
        raise ValueError("Invalid BLE-MIDI header")
    result = []
    index = 1
    running = None
    while index < len(packet):
        if not packet[index] & 0x80:
            raise ValueError("Missing BLE-MIDI timestamp")
        index += 1
        if index == len(packet):
            raise ValueError("Timestamp without message")
        status = packet[index]
        if status >= 0xF8:
            result.append(bytes([status]))
            index += 1
            continue
        if status & 0x80:
            index += 1
            if not 0x80 <= status <= 0xEF:
                raise ValueError("System Common / SysEx not supported in Phase 1")
            running = status
        elif running is None:
            raise ValueError("Running status without channel status")
        else:
            status = running
        count = 1 if status & 0xF0 in (0xC0, 0xD0) else 2
        data = []
        while len(data) < count:
            if index == len(packet):
                raise ValueError("Truncated MIDI message")
            value = packet[index]
            index += 1
            if value >= 0xF8:
                result.append(bytes([value]))
            elif value & 0x80:
                raise ValueError("Unexpected status in MIDI data")
            else:
                data.append(value)
        result.append(bytes([status, *data]))
    return result
