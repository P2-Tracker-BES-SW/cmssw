import ROOT
import struct
import zlib
import random

def hex_dump(data: bytes, max_length=64):
    """
    Return a hex-string representation of the given bytes.
    Only shows the first `max_length` bytes to avoid huge output.
    """
    length = min(len(data), max_length)
    hex_str = ' '.join(f'{b:02X}' for b in data[:length])
    if len(data) > max_length:
        hex_str += ' ... [truncated]'
    return hex_str

def inspect_fed_raw_data(
    root_file_path,
    tree_name="Events",
    branch_name="FEDRawDataCollection_dthDAQToFEDRawData__FEDRAW.obj",
    fed_id_range=(1200, 1600),
    print_hex=False,
    max_hex_len=64
):
    """
    Inspect and print the contents of the FEDRawDataCollection in a ROOT file.
    
    For each FED payload, the code extracts the first 4 bytes (if available),
    converts these bytes to an integer (interpreting them in little-endian order),
    and compares the value (in decimal) to the event order (starting at 1).
    
    It also checks that the remainder of the bytes (if any) are all zero.
    
    At the end, a summary is printed indicating whether all checked entries passed
    the 4-byte number matching check and the all-zero remainder check.
    
    :param root_file_path:  Path to the ROOT file
    :param tree_name:       Name of the TTree (default "Events")
    :param branch_name:     Name of the FEDRawDataCollection branch
    :param fed_id_range:    Tuple (start, end) of FED IDs to check
    :param print_hex:       If True, print a hex dump of the FED payload
    :param max_hex_len:     Max number of bytes to show in the hex dump
    """
    # Open the ROOT file
    root_file = ROOT.TFile.Open(root_file_path)
    if not root_file or root_file.IsZombie():
        print(f"Error: Could not open file '{root_file_path}'.")
        return

    # Access the TTree
    tree = root_file.Get(tree_name)
    if not tree:
        print(f"Error: TTree '{tree_name}' not found in the file.")
        return

    # Get number of entries in the tree
    num_entries = tree.GetEntries()
    print(f"\n ROOT File: {root_file_path}")
    print(f" TTree: '{tree_name}' | Entries: {num_entries}")

    # Counters for overall checking
    total_checked = 0
    total_failures = 0

    # Loop over events
    for event_idx in range(num_entries):
        tree.GetEntry(event_idx)

        # Access the FEDRawDataCollection branch
        try:
            fed_collection = getattr(tree, branch_name)
        except AttributeError:
            print(f"Error: Branch '{branch_name}' not found in TTree '{tree_name}'.")
            return

        print(f"\n Event {event_idx}:")

        fed_count = 0
        start_fed_id, end_fed_id = fed_id_range

        # Loop over possible FED IDs
        for fed_id in range(start_fed_id, end_fed_id):
            fed_data = fed_collection.FEDData(fed_id)
            data_size = fed_data.size()
            if data_size > 0:
                fed_count += 1
                print(f"  ➜ FED ID {fed_id}: {data_size} bytes", end='')

                # Retrieve the FED payload as Python bytes
                c_buf = fed_data.data()
                raw_bytes = bytes(c_buf[i] for i in range(data_size))

                # If the FED has at least 4 bytes, perform our checks:
                # 1. The first 4 bytes (as a little-endian integer) should equal the event order (1-indexed)
                # 2. All remaining bytes (if any) should be zero.
                if data_size >= 4:
                    first4 = raw_bytes[:4]
                    value = int.from_bytes(first4, byteorder='little')
                    expected = event_idx + 1  # 1-indexed event number

                    # Check if the remainder (if any) are all zeros.
                    remainder = raw_bytes[4:]
                    remainder_is_zero = all(b == 0 for b in remainder)
                    
                    total_checked += 1

                    pass_first4 = (value == expected)
                    pass_remainder = remainder_is_zero

                    if pass_first4 and pass_remainder:
                        cmp_str = (f"(first 4 bytes: {value} == expected event {expected} "
                                   f"and remainder is all zero)")
                    else:
                        cmp_str = (f"(first 4 bytes: {value} {'==' if pass_first4 else '!='} expected event {expected}; "
                                   f"remainder is {'all zero' if pass_remainder else 'not all zero'})")
                        total_failures += 1

                    print(" " + cmp_str, end='')

                if print_hex:
                    dump_str = hex_dump(raw_bytes, max_hex_len)
                    print(f"\n     Hex dump (up to {max_hex_len} bytes): {dump_str}")
                else:
                    print("")  # just end the line

        if fed_count == 0:
            print("   No FED data found in this event.")

    # Summary statement at the end
    print("\n================ SUMMARY ================")
    if total_checked == 0:
        print("No FED entries with at least 4 bytes were found for checking.")
    elif total_failures == 0:
        print(f"All {total_checked} checked FED entries passed the check (first 4 bytes matches the event number (order in file) and remainder bytes are zeros).")
    else:
        print(f"{total_failures} out of {total_checked} checked FED entries did NOT pass the check.")

    # Close the file
    root_file.Close()


if __name__ == "__main__":
    root_file_path = "outputFEDRawData.root"
    inspect_fed_raw_data(
        root_file_path,
        tree_name="Events",
        branch_name="FEDRawDataCollection_dthDAQToFEDRawData__FEDRAW.obj",
        fed_id_range=(1200, 1600),
        print_hex=True,   # Toggle for raw hex output
        max_hex_len=64
    )
