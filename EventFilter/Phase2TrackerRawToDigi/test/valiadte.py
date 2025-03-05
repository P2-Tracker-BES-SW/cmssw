import ROOT
import struct
import zlib

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

def load_external_payloads(payload_file):
    """
    Load externally dumped payloads from a file.
    Assumes hex format, one payload per line.
    """
    with open(payload_file, "r") as f:
        payloads = [bytes.fromhex(line.strip().replace(" ", "")) for line in f if line.strip()]
    return payloads

def compare_payloads(root_payload, external_payload):
    """
    Compare two byte sequences and return differences.
    """
    if root_payload == external_payload:
        return True, None
    else:
        diff_indices = [i for i in range(min(len(root_payload), len(external_payload))) if root_payload[i] != external_payload[i]]
        return False, diff_indices

def inspect_fed_raw_data(
    root_file_path,
    external_payload_file,
    tree_name="Events",
    branch_name="FEDRawDataCollection_dthDAQToFEDRawData__FEDRAW.obj",
    fed_id_range=(0, 16000),
    print_hex=False,
    max_hex_len=64
):
    """
    Inspect and compare FEDRawDataCollection from ROOT file with external payloads.
    
    :param root_file_path:  Path to the ROOT file
    :param external_payload_file: Path to external payload file (hex dump format)
    :param tree_name:       Name of the TTree (default "Events")
    :param branch_name:     Name of the FEDRawDataCollection branch
    :param fed_id_range:    Tuple (start, end) of FED IDs to check
    :param print_hex:       If True, print a hex dump of the FED payload
    :param max_hex_len:     Max number of bytes to show in the hex dump
    """
    # Load external payloads
    external_payloads = load_external_payloads(external_payload_file)

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

    num_entries = tree.GetEntries()
    print(f"\n ROOT File: {root_file_path}")
    print(f" TTree: '{tree_name}' | Entries: {num_entries}")

    total_checked = 0
    total_mismatches = 0

    # Loop over events
    for event_idx in range(num_entries):
        tree.GetEntry(event_idx)

        try:
            fed_collection = getattr(tree, branch_name)
        except AttributeError:
            print(f"Error: Branch '{branch_name}' not found in TTree '{tree_name}'.")
            return

        print(f"\n Event {event_idx}:")
        fed_count = 0
        start_fed_id, end_fed_id = fed_id_range

        for fed_id in range(start_fed_id, end_fed_id):
            fed_data = fed_collection.FEDData(fed_id)
            data_size = fed_data.size()
            if data_size > 0:
                fed_count += 1
                print(f"  ➜ FED ID {fed_id}: {data_size} bytes", end='')

                c_buf = fed_data.data()
                root_payload = bytes(c_buf[i] for i in range(data_size))

                # Compare with external payload
                if event_idx < len(external_payloads):
                    external_payload = external_payloads[event_idx]
                    match, diff_indices = compare_payloads(root_payload, external_payload)

                    total_checked += 1
                    if match:
                        print(" ✔ Matched")
                    else:
                        print(" ❌ Mismatch detected")
                        total_mismatches += 1
                        if print_hex:
                            print("     ROOT Payload:    " + hex_dump(root_payload, max_hex_len))
                            print("     External Payload:" + hex_dump(external_payload, max_hex_len))
                            print(f"     Difference at indices: {diff_indices}")

        if fed_count == 0:
            print("   No FED data found in this event.")

    # Summary statement
    print("\n================ SUMMARY ================")
    if total_checked == 0:
        print("No FED entries with data were found for comparison.")
    elif total_mismatches == 0:
        print(f"All {total_checked} checked FED entries matched the external payloads.")
    else:
        print(f"{total_mismatches} out of {total_checked} checked FED entries had mismatches.")

    root_file.Close()


if __name__ == "__main__":
    root_file_path = "outputFEDRawData.root"
    external_payload_file = "payloads.txt"  # Update with actual file containing hex dumps

    inspect_fed_raw_data(
        root_file_path,
        external_payload_file,
        tree_name="Events",
        branch_name="FEDRawDataCollection_dthDAQToFEDRawData__FEDRAW.obj",
        fed_id_range=(0, 1),
        print_hex=True,
        max_hex_len=64
    )
