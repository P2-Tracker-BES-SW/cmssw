#!/usr/bin/env python3
import sys
import struct

# Constants (in bytes)
orbitHeaderSize         = 32
fragmentPayloadWordSize = 16  # each word is 16 bytes

# Field sizes in the orbit header (in bytes)
orbitVersionSize = 2
sourceIdSize     = 4
runNumberSize    = 4
orbitNumberSize  = 4
eventCountResSize= 4
packetWordCountSize = 4
flagsSize        = 4
checksumSize     = 4

# Markers (expected values)
orbitHeaderMarkerH = 0x48  # 'H'
orbitHeaderMarkerO = 0x4F  # 'O'

def read_little_endian(data, offset, size):
    """Read a little-endian unsigned integer from data starting at offset."""
    # Construct format string: e.g., '<H' for 2 bytes, '<I' for 4 bytes, etc.
    fmt = {1: "<B", 2: "<H", 4: "<I", 6: "<Q"}[size]
    # For 6 bytes, we read 8 bytes then mask (see below)
    if size == 6:
        # Read 8 bytes, then mask to 44 bits (0xFFFFFFFFFFF)
        raw = data[offset:offset+8]
        if len(raw) < 8:
            raise ValueError("Not enough bytes to read 6-byte field (using 8-byte read).")
        value = struct.unpack("<Q", raw)[0] & 0xFFFFFFFFFFF
        return value
    else:
        raw = data[offset:offset+size]
        if len(raw) < size:
            raise ValueError("Not enough bytes to read field at offset {}".format(offset))
        return struct.unpack(fmt, raw)[0]

def count_events_in_file(filename):
    with open(filename, "rb") as f:
        data = f.read()
    pointer = 0
    total_events = 0
    orbit_count = 0

    while pointer < len(data):
        # Check if there's enough data for an orbit header
        if len(data) - pointer < orbitHeaderSize:
            print(f"Not enough data for an orbit header at offset {pointer}. Ending parse.")
            break

        # Check header markers
        markerH = data[pointer]
        markerO = data[pointer+1]
        if markerH != orbitHeaderMarkerH or markerO != orbitHeaderMarkerO:
            print(f"Invalid orbit header marker at orbit {orbit_count+1} (offset {pointer}): got {hex(markerH)} {hex(markerO)}")
            break

        pointer += 2  # consume marker bytes

        # Parse orbit header fields
        version = read_little_endian(data, pointer, orbitVersionSize)
        pointer += orbitVersionSize

        sourceId = read_little_endian(data, pointer, sourceIdSize)
        pointer += sourceIdSize

        runNumber = read_little_endian(data, pointer, runNumberSize)
        pointer += runNumberSize

        orbitNumber = read_little_endian(data, pointer, orbitNumberSize)
        pointer += orbitNumberSize

        eventCountReserved = read_little_endian(data, pointer, eventCountResSize)
        pointer += eventCountResSize
        # The lower 12 bits hold the event count
        eventCount = eventCountReserved & 0xFFF

        packetWordCount = read_little_endian(data, pointer, packetWordCountSize)
        pointer += packetWordCountSize

        flags = read_little_endian(data, pointer, flagsSize)
        pointer += flagsSize

        checksum = read_little_endian(data, pointer, checksumSize)
        pointer += checksumSize

        orbit_count += 1
        total_events += eventCount

        print(f"Orbit {orbit_count}: run {runNumber}, orbit {orbitNumber}, eventCount={eventCount}, packetWordCount={packetWordCount}")

        # Calculate the orbit's data block size (in bytes)
        orbitDataSizeBytes = packetWordCount * fragmentPayloadWordSize - orbitHeaderSize
        if len(data) - pointer < orbitDataSizeBytes:
            print(f"Orbit {orbit_count}: orbitDataSizeBytes = {orbitDataSizeBytes} but only {len(data) - pointer} bytes remain. Ending parse.")
            break
        pointer += orbitDataSizeBytes

    print(f"\nTotal orbits parsed: {orbit_count}")
    print(f"Total events found: {total_events}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python countEvents.py <rawfile>")
        sys.exit(1)
    filename = sys.argv[1]
    count_events_in_file(filename)
