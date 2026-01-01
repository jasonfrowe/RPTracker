import struct
import sys
import os

# Configuration
ROWS = 32
CHANS = 9
PATS_R1 = 16
PATS_R2 = 32

def convert_rpt1_to_rpt2(input_file, output_file):
    if not os.path.exists(input_file):
        print(f"Error: {input_file} not found.")
        return

    with open(input_file, "rb") as f:
        # 1. Read Header
        header = f.read(4)
        if header != b"RPT1":
            print("Error: Not a valid RPT1 file.")
            return

        # 2. Read Metadata (Octave:1, Vol:1, SongLength:2)
        meta = f.read(4)
        octave, vol, song_len = struct.unpack("<BBH", meta)
        print(f"Converting: {input_file}")
        print(f"Metadata: Octave {octave}, Vol {vol}, Seq Length {song_len}")

        # 3. Read 16 Patterns (4-byte cells)
        # Size = 16 * 32 * 9 * 4 = 18,432 bytes
        pats_data = []
        for p in range(PATS_R1):
            rows = []
            for r in range(ROWS):
                cells = []
                for c in range(CHANS):
                    cell_raw = f.read(4)
                    if len(cell_raw) < 4:
                        break
                    # Unpack: Note, Inst, Vol, Old_Effect
                    n, i, v, e_old = struct.unpack("BBBB", cell_raw)
                    # Convert to 5-byte cell: Note, Inst, Vol, Effect_Lo, Effect_Hi
                    # We map old effect to the low byte of the new 16-bit effect
                    new_cell = struct.pack("BBBHB", n, i, v, e_old, 0) 
                    # Wait, struct format "BBBH" is 5 bytes because of alignment. 
                    # Let's use individual bytes to be 100% sure of 5-byte packing.
                    new_cell = bytearray([n, i, v, e_old, 0])
                    cells.append(new_cell)
                rows.append(cells)
            pats_data.append(rows)

        # 4. Read the 256-byte Order List
        # It was located after the patterns in RPT1
        order_list = f.read(256)

    # WRITE RPT2
    with open(output_file, "wb") as f:
        # Write Header
        f.write(b"RPT2")
        # Write Metadata
        f.write(struct.pack("<BBH", octave, vol, song_len))

        # Write the 16 original patterns (now 5-bytes per cell)
        for p in range(PATS_R1):
            for r in range(ROWS):
                for c in range(CHANS):
                    f.write(pats_data[p][r][c])

        # Write 16 EMPTY patterns (to reach 32)
        # Each pattern is 32 * 9 * 5 = 1440 bytes
        empty_pattern = b"\x00" * 1440
        for _ in range(PATS_R2 - PATS_R1):
            f.write(empty_pattern)

        # Write Sequence Order List (always 256 bytes)
        if len(order_list) < 256:
            order_list = order_list.ljust(256, b"\x00")
        f.write(order_list)

    print(f"Success! Saved to {output_file}")
    print(f"New file size: {os.path.getsize(output_file)} bytes")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python convert.py input.rpt [output.rpt]")
    else:
        infile = sys.argv[1]
        outfile = sys.argv[2] if len(sys.argv) > 2 else "NEW_" + infile
        convert_rpt1_to_rpt2(infile, outfile)