import struct
import zlib
import random

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
    # Calculate checksum over first 28 bytes
    checksum = calculate_checksum(header_bytes[:28])
    checksum_bytes = struct.pack('<I', checksum)
    header_bytes = header_bytes[:28] + checksum_bytes

    return header_bytes

# Function to generate payload of exactly payload_size bytes.
# The payload is created from a 32-bit event_id converted to bytes,
# which is zero-padded (if needed) by int.to_bytes.
def create_payload(event_id, payload_size=16):
    # event_id is a 32-bit integer. Its byte representation will be at most 4 bytes.
    # If payload_size > 4, int.to_bytes will pad the result with zeros.
    return (event_id & 0xFFFFFFFF).to_bytes(payload_size, byteorder='little')

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

# Modified function to generate multiple orbits.
# For each orbit:
#   - The number of events is randomly chosen between 66 and 68.
#   - For each fragment a payload size is randomly chosen from {16, 32, 48, 64}.
#   - A global event counter is used for uniform, sequential event IDs.
def generate_orbit_stream(version, source_id, run_number, starting_orbit_number, flags, num_orbits=4):
    bitstream = b''

    # Define the orbit header size in 128-bit (16-byte) words.
    # Here, the orbit header is 32 bytes, i.e. 2 words.
    orbit_header_size_words = 32 // 16  # equals 2

    global_event_id = 0  # Global event counter for uniform event IDs

    for orbit in range(num_orbits):
        orbit_number = starting_orbit_number + orbit  # Increment orbit number

        # Randomly choose the number of events (fragments) for this orbit between 66 and 68.
        event_count = random.randint(66, 68)
        num_fragments = event_count

        # Prepare a temporary container for the orbit's fragments.
        fragments_data = b''
        total_fragment_size = 0

        # Loop over each fragment in the orbit.
        for i in range(num_fragments):
            global_event_id += 1
            event_id = global_event_id  # Use the global event counter

            # Pick a payload size at random from {16, 32, 48, 64}
            payload_size = random.choice([16, 32, 48, 64])
            payload = create_payload(event_id, payload_size=payload_size)

            # Compute fragment_size in 128-bit words.
            fragment_size = (len(payload) * 8) // 128
            total_fragment_size += fragment_size

            fragments_data += payload

            # Calculate a 16-bit CRC for the payload.
            crc = calculate_checksum(payload) & 0xFFFF
            trailer_flags = 0x0
            fragments_data += create_fragment_trailer(trailer_flags, fragment_size, event_id, crc)

        # Compute the packet_word_count as:
        #   orbit_header_size_words + (sum of fragment sizes) + num_fragments
        packet_word_count = orbit_header_size_words + total_fragment_size + num_fragments
        print(f"Orbit {orbit+1}: event_count={event_count}, packet_word_count={packet_word_count}")

        # Create the orbit header with the computed packet_word_count and the random event_count.
        header_bytes = create_orbit_header(version, source_id, run_number, orbit_number, event_count, packet_word_count, flags)

        # Append the header followed by all fragments to the bitstream.
        bitstream += header_bytes + fragments_data

    return bitstream


if __name__ == "__main__":
    # Randomly choose a number of orbits between 4 and 16 (inclusive)
    num_orbits = random.randint(4, 16)
    print(f"Generating bitstream with {num_orbits} orbits.")

    bitstream = generate_orbit_stream(
        version=1,             # Version (16 bits)
        source_id=12345,       # Example source ID (32 bits)
        run_number=6789,       # Example run number (32 bits)
        starting_orbit_number=98765,  # Starting orbit number (32 bits)
        flags=0x0,             # Initial flags set to 0 (32 bits)
        num_orbits=num_orbits  # Use the random number of orbits
    )

    # Write the bitstream with the orbits to a .raw file
    with open('_4orbit_data.raw', 'wb') as f:
        f.write(bitstream)

    print("Bitstream with orbits generated and written to _4orbit_data.raw")
