#!/bin/bash
# runDTHDAQ.sh
# Usage: ./runDTHDAQ.sh <raw_file>
#
# This script counts events in the raw file, modifies line 30 in the pre-existing
# DTHDAQtoFEDRAWData_cfg.py file to:
#     input = cms.untracked.int32($eventNum)
# where $eventNum is the number of events found, and then runs cmsRun.
if [ "$#" -ne 1 ]; then
  echo "Usage: $0 <raw_file>"
  exit 1
fi

RAWFILE="$1"

# Create a temporary Python file that counts events.
TMP_PY=$(mktemp /tmp/countEvents.XXXXXX.py)

cat > "$TMP_PY" << 'EOF'
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
    fmt = {1: "<B", 2: "<H", 4: "<I", 6: "<Q"}[size]
    if size == 6:
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
        if len(data) - pointer < orbitHeaderSize:
            print(f"Not enough data for an orbit header at offset {pointer}. Ending parse.")
            break

        markerH = data[pointer]
        markerO = data[pointer+1]
        if markerH != orbitHeaderMarkerH or markerO != orbitHeaderMarkerO:
            print(f"Invalid orbit header marker at orbit {orbit_count+1} (offset {pointer}): got {hex(markerH)} {hex(markerO)}")
            break

        pointer = pointer + 2 + orbitVersionSize + sourceIdSize + runNumberSize + orbitNumberSize
        # 2 bytes for the marker "H" "O"

        eventCountReserved = read_little_endian(data, pointer, eventCountResSize)
        pointer += eventCountResSize
        eventCount = eventCountReserved & 0xFFF

        packetWordCount = read_little_endian(data, pointer, packetWordCountSize)
        pointer = pointer + packetWordCountSize + flagsSize + checksumSize

        orbit_count += 1
        total_events += eventCount

        print(f"Orbit {orbit_count}: eventCount={eventCount}, packetWordCount={packetWordCount}")
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
EOF

# Make the temporary Python script executable.
chmod +x "$TMP_PY"

# Run the Python script to count events in the raw file.
EVENTNUM=$(python3 "$TMP_PY" "$RAWFILE" | grep "Total events found:" | awk '{print $NF}')

# Remove the temporary Python script.
rm "$TMP_PY"

if [ -z "$EVENTNUM" ]; then
  echo "Error: Could not determine the number of events from $RAWFILE"
  exit 1
fi

echo "Number of events found: $EVENTNUM"

# Modify line 30 of the existing CMSSW configuration file.
sed -i "30s/.*/    input = cms.untracked.int32(${EVENTNUM})/" DTHDAQtoFEDRAWData_cfg.py

echo "Updated DTHDAQtoFEDRAWData_cfg.py at line 30 with event count ${EVENTNUM}"

# Run cmsRun with the modified configuration file.
cmsRun DTHDAQtoFEDRAWData_cfg.py
