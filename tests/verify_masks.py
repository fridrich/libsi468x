import json
import sys

def find_mask_for_label(long_label, short_label):
    # This function reverse-computes the 16-bit character mask
    # that converts a long label (16 chars) into a short label (8 chars).
    # If no such mapping is possible, returns None.
    
    # Trim trailing spaces for comparison but maintain exact indices
    long_clean = long_label
    short_clean = short_label.strip()
    
    mask = 0
    short_idx = 0
    
    if not short_clean:
        return 0
        
    for i in range(len(long_clean)):
        if short_idx < len(short_clean) and long_clean[i] == short_clean[short_idx]:
            # Set the corresponding bit (Bit 15 is index 0, Bit 14 is index 1...)
            bit_pos = 15 - i
            mask |= (1 << bit_pos)
            short_idx += 1
            
    # If we successfully matched all characters of the short label, return the mask
    if short_idx == len(short_clean):
        return mask
    return None

def decode_short_label(long_label, char_mask):
    short_label = ""
    for i in range(min(16, len(long_label))):
        bit_pos = 15 - i
        if (char_mask & (1 << bit_pos)) != 0:
            short_label += long_label[i]
    return short_label.strip()

def main():
    print("==========================================================================")
    print("  libsi468x stations.json Short Label Mask Consistency Proof Utility      ")
    print("==========================================================================")

    # Load records from stations.json
    try:
        with open("/home/fstrba/reverse-enginner/stations.json", "r", encoding="utf-8") as f:
            stations_data = json.load(f)
    except Exception as e:
        print(f"Error loading stations.json: {e}")
        sys.exit(1)

    stations_list = stations_data.get("stations", [])
    print(f"Loaded {len(stations_list)} station records from stations.json.")
    print("-" * 115)
    print(f" {'Channel':<5} | {'Long Program Label':<18} | {'stations.json Short':<16} | {'Computed Mask':<8} | {'Decoded Short':<16} | {'Status':<6}")
    print("-" * 115)

    success_count = 0
    fail_count = 0

    for entry in stations_list:
        channel = entry.get("channel", "N/A")
        long_label = entry.get("program", "")
        short_label = entry.get("short_program", "")
        
        # 1. Reverse-compute the character mask
        mask = find_mask_for_label(long_label, short_label)
        
        if mask is None:
            print(f" {channel:<5} | {long_label.strip():<18} | {short_label:<16} | {'N/A':<8} | {'N/A':<16} | {'FAIL':<6}")
            fail_count += 1
            continue
            
        # 2. Decode the label using our computed mask
        decoded = decode_short_label(long_label, mask)
        
        # Verify the decoded matches reference
        equal = (decoded == short_label.strip())
        if equal:
            success_count += 1
            status = "PASS"
        else:
            fail_count += 1
            status = "FAIL!"
            
        print(f" {channel:<5} | {long_label.strip():<18} | {short_label.strip():<16} | 0x{mask:04x}   | {decoded:<16} | {status:<6}")

    print("-" * 115)
    print("Verification Summary:")
    print(f"  Total Stations Checked: {len(stations_list)}")
    print(f"  Successful Matches:     {success_count}")
    print(f"  Failures:               {fail_count}")
    if len(stations_list) > 0:
        accuracy = (success_count / len(stations_list)) * 100
        print(f"  Mathematical Accuracy:  {accuracy:.2f}%")
    print("==========================================================================")

    sys.exit(0 if fail_count == 0 else 1)

if __name__ == "__main__":
    main()
