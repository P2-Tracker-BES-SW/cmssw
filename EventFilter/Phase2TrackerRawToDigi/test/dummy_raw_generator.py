import struct
import zlib

# Function to calculate CRC32 checksum
def calculate_checksum(data):
    return zlib.crc32(data) & 0xFFFFFFFF

# Function to generate the Orbit Header
def create_orbit_header(version, source_id, run_number, orbit_number, event_count, packet_word_count, flags):
    header = 0
    header |= 0x48  # First byte
    header |= 0x4F << 8  # Second byte
    header |= (version & 0xFFFF) << 16  # Next 2 bytes (16 bits)
    header |= (source_id & 0xFFFFFFFF) << 32  # Next 4 bytes (32 bits)
    header |= (run_number & 0xFFFFFFFF) << 64  # Next 4 bytes (32 bits)
    header |= (orbit_number & 0xFFFFFFFF) << 96  # Next 4 bytes (32 bits)
    header |= (event_count & 0xFFF) << 128  # Next 12 bits
    header |= (0x0 & 0xFFFFF) << 140  # Reserved (20 bits)
    header |= (packet_word_count & 0xFFFFFFFF) << 160  # Next 4 bytes (32 bits)
    header |= (flags & 0xFFFFFFFF) << 192  # Next 4 bytes (32 bits)
    header |= 0x0 << 224  # Checksum placeholder

    header_bytes = header.to_bytes(32, byteorder='little')
    checksum = calculate_checksum(header_bytes[:28])  # Calculate checksum over first 28 bytes
    checksum_bytes = struct.pack('<I', checksum)
    header_bytes = header_bytes[:28] + checksum_bytes

    return header_bytes

# Function to generate payload (128 bits)
def create_payload(event_id):
    payload = (event_id & 0xFFFFFFFF)  # 32-bit Event ID
    return payload.to_bytes(16, byteorder='little')

# Function to generate the Fragment Trailer
def create_fragment_trailer(flags, fragment_size, event_id, crc):
    trailer = 0
    trailer |= 0x48  # First byte
    trailer |= 0x46 << 8  # Second byte
    trailer |= (flags & 0xFFFF) << 16  # Flags (16 bits)
    trailer |= (fragment_size & 0xFFFFFFFF) << 32  # Fragment size (32 bits)
    trailer |= (event_id & 0xFFFFFFFFFFF) << 64  # Event ID (44 bits)
    trailer |= (0x0 & 0xF) << 108  # Reserved (4 bits)
    trailer |= (crc & 0xFFFF) << 112  # CRC (16 bits)

    return trailer.to_bytes(16, byteorder='little')

# Modified function to generate multiple orbits
def generate_orbit_stream(version, source_id, run_number, starting_orbit_number, event_count, packet_word_count, flags, num_fragments=67, num_orbits=4):
    bitstream = b''

    # Loop over the number of orbits
    for orbit in range(num_orbits):
        orbit_number = starting_orbit_number + orbit  # Increment orbit number
        bitstream += create_orbit_header(version, source_id, run_number, orbit_number, event_count, packet_word_count, flags)

        # Generate payloads and fragment trailers for each fragment in the orbit
        for i in range(num_fragments):
            event_id = i + 1 + orbit * num_fragments  # Event ID across orbits
            payload = create_payload(event_id)
            bitstream += payload

            # Fragment trailer specifics
            fragment_size = 128  # Payload size (128 bits)
            crc = calculate_checksum(payload) & 0xFFFF
            trailer_flags = 0x0
            bitstream += create_fragment_trailer(trailer_flags, fragment_size, event_id, crc)

    return bitstream

# Example usage
if __name__ == "__main__":
    bitstream = generate_orbit_stream(
        version=1,             # Version (16 bits)
        source_id=12345,       # Example source ID (32 bits)
        run_number=6789,       # Example run number (32 bits)
        starting_orbit_number=98765,  # Starting orbit number (32 bits)
        event_count=67,        # Number of events per orbit (12 bits)
        packet_word_count=134, # Each fragment is 2 words, 67 * 2 = 134
        flags=0x0              # Initial flags set to 0 (32 bits)
    )

    # Write the bitstream with 4 orbits to a .raw file
    with open('_4orbit_data.raw', 'wb') as f:
        f.write(bitstream)

    print("Bitstream with 4 orbits generated and written to _4orbit_data.raw")
