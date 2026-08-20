#!/usr/bin/env python3
"""
RPTracker NSF Converter / Importer
Converts NES NSF files (specifically Pac-Man_CE.nsf with 2A03/VRC7/N163 audio)
into RPTracker RPT2 song format (.rpt) targeting OPL2.
"""

import sys
import os
import math
import struct

try:
    from py65.devices.mpu6502 import MPU
except ImportError:
    MPU = None

# RPT2 Constants
ROWS = 32
CHANS = 9
PATTERNS = 32

class PatternCell:
    def __init__(self, note=0, inst=0, vol=0, effect=0x0000):
        self.note = note      # 0=empty, 255=note off, 12-119=MIDI note
        self.inst = inst      # 0-255 instrument
        self.vol = vol        # 0-63 volume
        self.effect = effect  # 16-bit effect

    def to_bytes(self):
        lo = self.effect & 0xFF
        hi = (self.effect >> 8) & 0xFF
        return bytes([self.note, self.inst, self.vol, lo, hi])

class RPT3File:
    def __init__(self):
        self.octave = 3
        self.volume = 63
        self.song_length = 1
        self.bpm = 150
        self.patterns = [[PatternCell() for _ in range(CHANS)] for _ in range(ROWS * PATTERNS)]
        self.sequence = [0] * 256

    def set_cell(self, pattern_idx, row_idx, chan_idx, cell):
        if 0 <= pattern_idx < PATTERNS and 0 <= row_idx < ROWS and 0 <= chan_idx < CHANS:
            abs_row = pattern_idx * ROWS + row_idx
            self.patterns[abs_row][chan_idx] = cell

    def save(self, filename):
        os.makedirs(os.path.dirname(os.path.abspath(filename)), exist_ok=True)
        with open(filename, 'wb') as f:
            # Header
            f.write(b'RPT3')
            # Metadata: Octave (B), Volume (B), Song Length (H), BPM (H)
            f.write(struct.pack('<BBHH', self.octave, self.volume, self.song_length, self.bpm))
            # 32 patterns x 32 rows x 9 channels x 5 bytes
            for p in range(PATTERNS):
                for r in range(ROWS):
                    abs_row = p * ROWS + r
                    for c in range(CHANS):
                        f.write(self.patterns[abs_row][c].to_bytes())
            # Sequence order list (256 bytes)
            f.write(bytes(self.sequence[:256]))

class CPU6502:
    """Lightweight 6502 CPU emulator for executing NSF player routines."""
    def __init__(self, memory):
        self.mem = memory
        self.a = 0
        self.x = 0
        self.y = 0
        self.sp = 0xFF
        self.pc = 0
        self.c = 0
        self.z = 0
        self.i = 1
        self.d = 0
        self.v = 0
        self.n = 0

    def get_p(self):
        return (self.c | (self.z << 1) | (self.i << 2) | (self.d << 3) |
                (1 << 4) | (1 << 5) | (self.v << 6) | (self.n << 7))

    def set_p(self, val):
        self.c = val & 1
        self.z = (val >> 1) & 1
        self.i = (val >> 2) & 1
        self.d = (val >> 3) & 1
        self.v = (val >> 6) & 1
        self.n = (val >> 7) & 1

    def push(self, val):
        self.mem[0x0100 + self.sp] = val & 0xFF
        self.sp = (self.sp - 1) & 0xFF

    def pop(self):
        self.sp = (self.sp + 1) & 0xFF
        return self.mem[0x0100 + self.sp]

    def set_nz(self, val):
        val &= 0xFF
        self.z = 1 if val == 0 else 0
        self.n = 1 if (val & 0x80) else 0
        return val

    def step(self):
        op = self.mem[self.pc]
        self.pc = (self.pc + 1) & 0xFFFF

        # Addressing modes helpers
        def imm():
            addr = self.pc
            self.pc = (self.pc + 1) & 0xFFFF
            return addr

        def zp():
            addr = self.mem[self.pc]
            self.pc = (self.pc + 1) & 0xFFFF
            return addr

        def zpx():
            addr = (self.mem[self.pc] + self.x) & 0xFF
            self.pc = (self.pc + 1) & 0xFFFF
            return addr

        def zpy():
            addr = (self.mem[self.pc] + self.y) & 0xFF
            self.pc = (self.pc + 1) & 0xFFFF
            return addr

        def abs_a():
            low = self.mem[self.pc]
            high = self.mem[(self.pc + 1) & 0xFFFF]
            self.pc = (self.pc + 2) & 0xFFFF
            return low | (high << 8)

        def abs_x():
            base = abs_a()
            return (base + self.x) & 0xFFFF

        def abs_y():
            base = abs_a()
            return (base + self.y) & 0xFFFF

        def ind_x():
            zp_addr = (self.mem[self.pc] + self.x) & 0xFF
            self.pc = (self.pc + 1) & 0xFFFF
            return self.mem[zp_addr] | (self.mem[(zp_addr + 1) & 0xFF] << 8)

        def ind_y():
            zp_addr = self.mem[self.pc]
            self.pc = (self.pc + 1) & 0xFFFF
            base = self.mem[zp_addr] | (self.mem[(zp_addr + 1) & 0xFF] << 8)
            return (base + self.y) & 0xFFFF

        def branch(cond):
            offset = self.mem[self.pc]
            self.pc = (self.pc + 1) & 0xFFFF
            if offset & 0x80:
                offset -= 256
            if cond:
                self.pc = (self.pc + offset) & 0xFFFF

        # Common opcodes
        if op == 0xEA: pass # NOP
        elif op == 0x78: self.i = 1 # SEI
        elif op == 0x58: self.i = 0 # CLI
        elif op == 0x18: self.c = 0 # CLC
        elif op == 0x38: self.c = 1 # SEC
        elif op == 0xD8: self.d = 0 # CLD
        elif op == 0xF8: self.d = 1 # SED
        elif op == 0xB8: self.v = 0 # CLV

        # LDA
        elif op == 0xA9: self.a = self.set_nz(self.mem[imm()])
        elif op == 0xA5: self.a = self.set_nz(self.mem[zp()])
        elif op == 0xB5: self.a = self.set_nz(self.mem[zpx()])
        elif op == 0xAD: self.a = self.set_nz(self.mem[abs_a()])
        elif op == 0xBD: self.a = self.set_nz(self.mem[abs_x()])
        elif op == 0xB9: self.a = self.set_nz(self.mem[abs_y()])
        elif op == 0xA1: self.a = self.set_nz(self.mem[ind_x()])
        elif op == 0xB1: self.a = self.set_nz(self.mem[ind_y()])

        # LDX
        elif op == 0xA2: self.x = self.set_nz(self.mem[imm()])
        elif op == 0xA6: self.x = self.set_nz(self.mem[zp()])
        elif op == 0xB6: self.x = self.set_nz(self.mem[zpy()])
        elif op == 0xAE: self.x = self.set_nz(self.mem[abs_a()])
        elif op == 0xBE: self.x = self.set_nz(self.mem[abs_y()])

        # LDY
        elif op == 0xA0: self.y = self.set_nz(self.mem[imm()])
        elif op == 0xA4: self.y = self.set_nz(self.mem[zp()])
        elif op == 0xB4: self.y = self.set_nz(self.mem[zpx()])
        elif op == 0xAC: self.y = self.set_nz(self.mem[abs_a()])
        elif op == 0xBC: self.y = self.set_nz(self.mem[abs_x()])

        # STA
        elif op == 0x85: self.mem[zp()] = self.a
        elif op == 0x95: self.mem[zpx()] = self.a
        elif op == 0x8D: self.mem[abs_a()] = self.a
        elif op == 0x9D: self.mem[abs_x()] = self.a
        elif op == 0x99: self.mem[abs_y()] = self.a
        elif op == 0x81: self.mem[ind_x()] = self.a
        elif op == 0x91: self.mem[ind_y()] = self.a

        # STX
        elif op == 0x86: self.mem[zp()] = self.x
        elif op == 0x96: self.mem[zpy()] = self.x
        elif op == 0x8E: self.mem[abs_a()] = self.x

        # STY
        elif op == 0x84: self.mem[zp()] = self.y
        elif op == 0x94: self.mem[zpx()] = self.y
        elif op == 0x8C: self.mem[abs_a()] = self.y

        # Transfers
        elif op == 0xAA: self.x = self.set_nz(self.a) # TAX
        elif op == 0x8A: self.a = self.set_nz(self.x) # TXA
        elif op == 0xA8: self.y = self.set_nz(self.a) # TAY
        elif op == 0x98: self.a = self.set_nz(self.y) # TYA
        elif op == 0x9A: self.sp = self.x # TXS
        elif op == 0xBA: self.x = self.set_nz(self.sp) # TSX

        # Stack
        elif op == 0x48: self.push(self.a) # PHA
        elif op == 0x68: self.a = self.set_nz(self.pop()) # PLA
        elif op == 0x08: self.push(self.get_p()) # PHP
        elif op == 0x28: self.set_p(self.pop()) # PLP

        # Jumps & Calls
        elif op == 0x4C: self.pc = abs_a() # JMP abs
        elif op == 0x6C: # JMP ind
            target = abs_a()
            low = self.mem[target]
            high_addr = (target & 0xFF00) | ((target + 1) & 0xFF)
            high = self.mem[high_addr]
            self.pc = low | (high << 8)
        elif op == 0x20: # JSR
            target = abs_a()
            ret = (self.pc - 1) & 0xFFFF
            self.push((ret >> 8) & 0xFF)
            self.push(ret & 0xFF)
            self.pc = target
        elif op == 0x60: # RTS
            low = self.pop()
            high = self.pop()
            self.pc = ((low | (high << 8)) + 1) & 0xFFFF

        # Branches
        elif op == 0x10: branch(self.n == 0) # BPL
        elif op == 0x30: branch(self.n == 1) # BMI
        elif op == 0x50: branch(self.v == 0) # BVC
        elif op == 0x70: branch(self.v == 1) # BVS
        elif op == 0x90: branch(self.c == 0) # BCC
        elif op == 0xB0: branch(self.c == 1) # BCS
        elif op == 0xD0: branch(self.z == 0) # BNE
        elif op == 0xF0: branch(self.z == 1) # BEQ

        # Increments & Decrements
        elif op == 0xE6: a = zp(); self.mem[a] = self.set_nz((self.mem[a] + 1) & 0xFF)
        elif op == 0xF6: a = zpx(); self.mem[a] = self.set_nz((self.mem[a] + 1) & 0xFF)
        elif op == 0xEE: a = abs_a(); self.mem[a] = self.set_nz((self.mem[a] + 1) & 0xFF)
        elif op == 0xFE: a = abs_x(); self.mem[a] = self.set_nz((self.mem[a] + 1) & 0xFF)
        elif op == 0xC6: a = zp(); self.mem[a] = self.set_nz((self.mem[a] - 1) & 0xFF)
        elif op == 0xD6: a = zpx(); self.mem[a] = self.set_nz((self.mem[a] - 1) & 0xFF)
        elif op == 0xCE: a = abs_a(); self.mem[a] = self.set_nz((self.mem[a] - 1) & 0xFF)
        elif op == 0xDE: a = abs_x(); self.mem[a] = self.set_nz((self.mem[a] - 1) & 0xFF)
        elif op == 0xE8: self.x = self.set_nz((self.x + 1) & 0xFF) # INX
        elif op == 0xC8: self.y = self.set_nz((self.y + 1) & 0xFF) # INY
        elif op == 0xCA: self.x = self.set_nz((self.x - 1) & 0xFF) # DEX
        elif op == 0x88: self.y = self.set_nz((self.y - 1) & 0xFF) # DEY

        # Comparisons
        elif op in (0xC9, 0xC5, 0xD5, 0xCD, 0xDD, 0xD9, 0xC1, 0xD1): # CMP
            a = imm() if op == 0xC9 else (zp() if op == 0xC5 else (zpx() if op == 0xD5 else (abs_a() if op == 0xCD else (abs_x() if op == 0xDD else (abs_y() if op == 0xD9 else (ind_x() if op == 0xC1 else ind_y()))))))
            val = self.mem[a]
            diff = self.a - val
            self.c = 1 if self.a >= val else 0
            self.set_nz(diff & 0xFF)
        elif op in (0xE0, 0xE4, 0xEC): # CPX
            a = imm() if op == 0xE0 else (zp() if op == 0xE4 else abs_a())
            val = self.mem[a]
            self.c = 1 if self.x >= val else 0
            self.set_nz((self.x - val) & 0xFF)
        elif op in (0xC0, 0xC4, 0xCC): # CPY
            a = imm() if op == 0xC0 else (zp() if op == 0xC4 else abs_a())
            val = self.mem[a]
            self.c = 1 if self.y >= val else 0
            self.set_nz((self.y - val) & 0xFF)

        # Bitwise & Math (AND, ORA, EOR, ADC, SBC)
        elif op in (0x29, 0x25, 0x35, 0x2D, 0x3D, 0x39, 0x21, 0x31): # AND
            a = imm() if op == 0x29 else (zp() if op == 0x25 else (zpx() if op == 0x35 else (abs_a() if op == 0x2D else (abs_x() if op == 0x3D else (abs_y() if op == 0x39 else (ind_x() if op == 0x21 else ind_y()))))))
            self.a = self.set_nz(self.a & self.mem[a])
        elif op in (0x09, 0x05, 0x15, 0x0D, 0x1D, 0x19, 0x01, 0x11): # ORA
            a = imm() if op == 0x09 else (zp() if op == 0x05 else (zpx() if op == 0x15 else (abs_a() if op == 0x0D else (abs_x() if op == 0x1D else (abs_y() if op == 0x19 else (ind_x() if op == 0x01 else ind_y()))))))
            self.a = self.set_nz(self.a | self.mem[a])
        elif op in (0x49, 0x45, 0x55, 0x4D, 0x5D, 0x59, 0x41, 0x51): # EOR
            a = imm() if op == 0x49 else (zp() if op == 0x45 else (zpx() if op == 0x55 else (abs_a() if op == 0x4D else (abs_x() if op == 0x5D else (abs_y() if op == 0x59 else (ind_x() if op == 0x41 else ind_y()))))))
            self.a = self.set_nz(self.a ^ self.mem[a])

        elif op in (0x69, 0x65, 0x75, 0x6D, 0x7D, 0x79, 0x61, 0x71): # ADC
            a = imm() if op == 0x69 else (zp() if op == 0x65 else (zpx() if op == 0x75 else (abs_a() if op == 0x6D else (abs_x() if op == 0x7D else (abs_y() if op == 0x79 else (ind_x() if op == 0x61 else ind_y()))))))
            val = self.mem[a]
            res = self.a + val + self.c
            self.v = 1 if (~(self.a ^ val) & (self.a ^ res) & 0x80) else 0
            self.c = 1 if res > 0xFF else 0
            self.a = self.set_nz(res & 0xFF)

        elif op in (0xE9, 0xE5, 0xF5, 0xED, 0xFD, 0xF9, 0xE1, 0xF1): # SBC
            a = imm() if op == 0xE9 else (zp() if op == 0xE5 else (zpx() if op == 0xF5 else (abs_a() if op == 0xED else (abs_x() if op == 0xFD else (abs_y() if op == 0xF9 else (ind_x() if op == 0xE1 else ind_y()))))))
            val = self.mem[a] ^ 0xFF
            res = self.a + val + self.c
            self.v = 1 if (~(self.a ^ val) & (self.a ^ res) & 0x80) else 0
            self.c = 1 if res > 0xFF else 0
            self.a = self.set_nz(res & 0xFF)

        # Shifts & Rotates (ASL, LSR, ROL, ROR)
        elif op == 0x0A: # ASL A
            self.c = (self.a >> 7) & 1
            self.a = self.set_nz((self.a << 1) & 0xFF)
        elif op in (0x06, 0x16, 0x0E, 0x1E):
            a = zp() if op == 0x06 else (zpx() if op == 0x16 else (abs_a() if op == 0x0E else abs_x()))
            val = self.mem[a]
            self.c = (val >> 7) & 1
            self.mem[a] = self.set_nz((val << 1) & 0xFF)
        elif op == 0x4A: # LSR A
            self.c = self.a & 1
            self.a = self.set_nz((self.a >> 1) & 0xFF)
        elif op in (0x46, 0x56, 0x4E, 0x5E):
            a = zp() if op == 0x46 else (zpx() if op == 0x56 else (abs_a() if op == 0x4E else abs_x()))
            val = self.mem[a]
            self.c = val & 1
            self.mem[a] = self.set_nz((val >> 1) & 0xFF)
        elif op == 0x2A: # ROL A
            c_old = self.c
            self.c = (self.a >> 7) & 1
            self.a = self.set_nz(((self.a << 1) | c_old) & 0xFF)
        elif op in (0x26, 0x36, 0x2E, 0x3E):
            a = zp() if op == 0x26 else (zpx() if op == 0x36 else (abs_a() if op == 0x2E else abs_x()))
            val = self.mem[a]
            c_old = self.c
            self.c = (val >> 7) & 1
            self.mem[a] = self.set_nz(((val << 1) | c_old) & 0xFF)
        elif op == 0x6A: # ROR A
            c_old = self.c
            self.c = self.a & 1
            self.a = self.set_nz((self.a >> 1) | (c_old << 7))
        elif op in (0x66, 0x76, 0x6E, 0x7E):
            a = zp() if op == 0x66 else (zpx() if op == 0x76 else (abs_a() if op == 0x6E else abs_x()))
            val = self.mem[a]
            c_old = self.c
            self.c = val & 1
            self.mem[a] = self.set_nz((val >> 1) | (c_old << 7))
        elif op in (0x24, 0x2C): # BIT
            a = zp() if op == 0x24 else abs_a()
            val = self.mem[a]
            self.z = 1 if (self.a & val) == 0 else 0
            self.v = (val >> 6) & 1
            self.n = (val >> 7) & 1

class NSFConverter:
    def __init__(self, nsf_path):
        with open(nsf_path, 'rb') as f:
            self.data = f.read()

        if self.data[:5] != b'NESM\x1a':
            raise ValueError("Invalid NSF magic header")

        self.total_songs = self.data[6]
        self.start_song = self.data[7]
        self.load_addr, self.init_addr, self.play_addr = struct.unpack('<HHH', self.data[8:14])
        self.title = self.data[0x0E:0x2E].split(b'\x00')[0].decode('latin1', 'ignore').strip()
        self.artist = self.data[0x2E:0x4E].split(b'\x00')[0].decode('latin1', 'ignore').strip()
        self.payload = self.data[0x80:]

    def convert_track(self, track_idx, max_seconds=100):
        # 60 FPS frame count
        total_frames = max_seconds * 60

        ram = bytearray(0x10000)
        ram[self.load_addr:self.load_addr+len(self.payload)] = self.payload

        apu_regs = bytearray(32)

        def write_io(addr, val):
            addr &= 0xFFFF
            val &= 0xFF
            ram[addr] = val
            if 0x4000 <= addr <= 0x4017:
                apu_regs[addr - 0x4000] = val

        class HookMemory:
            def __getitem__(_, a): return ram[a & 0xFFFF]
            def __setitem__(_, a, v): write_io(a, v)

        mem = HookMemory()
        cpu = MPU(memory=mem) if MPU is not None else CPU6502(mem)

        # Call INIT
        cpu.a = track_idx
        cpu.x = 0
        cpu.pc = self.init_addr
        cpu.sp = 0xFF
        ram[0x01FF] = 0x00; ram[0x01FE] = 0x00
        step = 0
        while cpu.pc != 0x0001 and step < 500000:
            cpu.step()
            step += 1

        # Frame history
        history = []
        for f in range(total_frames):
            cpu.pc = self.play_addr
            cpu.sp = 0xFF
            ram[0x01FF] = 0x00; ram[0x01FE] = 0x00
            step = 0
            while cpu.pc != 0x0001 and step < 500000:
                cpu.step()
                step += 1

            status = apu_regs[0x15]

            # Decode APU channels
            # Square 1
            sq1_en = bool(status & 0x01)
            sq1_p = apu_regs[2] | ((apu_regs[3] & 0x07) << 8)
            sq1_v_reg = apu_regs[0]
            sq1_vol = (sq1_v_reg & 0x0F) if (sq1_v_reg & 0x10) else (15 if sq1_en else 0)
            sq1_note = 0
            if sq1_en and sq1_p > 0 and sq1_vol > 0:
                freq = 1789773.0 / (16.0 * (sq1_p + 1))
                if 20 <= freq <= 12000:
                    sq1_note = int(round(12.0 * math.log2(freq / 440.0) + 69))

            # Square 2
            sq2_en = bool(status & 0x02)
            sq2_p = apu_regs[6] | ((apu_regs[7] & 0x07) << 8)
            sq2_v_reg = apu_regs[4]
            sq2_vol = (sq2_v_reg & 0x0F) if (sq2_v_reg & 0x10) else (15 if sq2_en else 0)
            sq2_note = 0
            if sq2_en and sq2_p > 0 and sq2_vol > 0:
                freq = 1789773.0 / (16.0 * (sq2_p + 1))
                if 20 <= freq <= 12000:
                    sq2_note = int(round(12.0 * math.log2(freq / 440.0) + 69))

            # Triangle
            tri_en = bool(status & 0x04)
            tri_p = apu_regs[10] | ((apu_regs[11] & 0x07) << 8)
            tri_note = 0
            if tri_en and tri_p > 0:
                freq = 1789773.0 / (32.0 * (tri_p + 1))
                if 20 <= freq <= 12000:
                    tri_note = int(round(12.0 * math.log2(freq / 440.0) + 69))

            # Noise
            noise_en = bool(status & 0x08)
            noise_v_reg = apu_regs[12]
            noise_vol = (noise_v_reg & 0x0F) if (noise_v_reg & 0x10) else (15 if noise_en else 0)
            noise_note = 36 if (noise_en and noise_vol > 0) else 0

            # DMC / PCM
            dmc_en = bool(status & 0x10)
            dmc_val = apu_regs[0x11] & 0x7F
            dmc_note = 38 if (dmc_en and dmc_val > 0) else 0

            history.append({
                'sq1': (sq1_note, sq1_vol),
                'sq2': (sq2_note, sq2_vol),
                'tri': (tri_note, 15 if tri_note else 0),
                'noise': (noise_note, noise_vol),
                'dmc': (dmc_note, 15 if dmc_note else 0)
            })

        # Group frames into tracker rows (e.g. 6 frames per row = 10 rows/sec)
        frames_per_row = 6
        num_rows = min(32 * ROWS, len(history) // frames_per_row)

        rpt = RPT3File()
        active_rows = 0

        # Map channels:
        # Ch 0: Square 1  (Inst 80 = Synth Square Lead)
        # Ch 1: Square 2  (Inst 81 = Synth Saw Lead)
        # Ch 2: Triangle  (Inst 33 = Bass)
        # Ch 3: Noise     (Inst 115 = Percussion)
        # Ch 4: DMC / PCM (Inst 118 = Synth Drum)
        for r_idx in range(num_rows):
            f_frame = history[r_idx * frames_per_row]
            pattern_idx = r_idx // ROWS
            row_in_pat = r_idx % ROWS

            ch_data = [
                (f_frame['sq1'][0], 80, f_frame['sq1'][1]),
                (f_frame['sq2'][0], 81, f_frame['sq2'][1]),
                (f_frame['tri'][0], 33, f_frame['tri'][1]),
                (f_frame['noise'][0], 115, f_frame['noise'][1]),
                (f_frame['dmc'][0], 118, f_frame['dmc'][1]),
            ]

            row_has_note = False
            for c_idx in range(min(CHANS, len(ch_data))):
                midi_note, inst_id, vol_15 = ch_data[c_idx]
                if midi_note > 0:
                    row_has_note = True
                    vol_63 = min(63, vol_15 * 4)
                    cell = PatternCell(note=midi_note, inst=inst_id, vol=vol_63)
                    rpt.set_cell(pattern_idx, row_in_pat, c_idx, cell)

            if row_has_note:
                active_rows = r_idx + 1

        # Calculate song length in patterns
        patterns_used = max(1, math.ceil(active_rows / ROWS))
        patterns_used = min(PATTERNS, patterns_used)

        rpt.song_length = patterns_used
        for p in range(patterns_used):
            rpt.sequence[p] = p

        return rpt

def convert_nsf_to_rpt(nsf_path, output_dir):
    os.makedirs(output_dir, exist_ok=True)
    converter = NSFConverter(nsf_path)
    print(f"Loaded NSF: {nsf_path}")
    print(f"Title: {converter.title}")
    print(f"Total Songs/Tracks: {converter.total_songs}")

    exported_files = []
    for track_idx in range(converter.total_songs):
        rpt = converter.convert_track(track_idx)
        out_filename = os.path.join(output_dir, f"PACMAN{track_idx+1:02d}.RPT")
        rpt.save(out_filename)
        size = os.path.getsize(out_filename)
        print(f"  Track {track_idx+1:2d}/{converter.total_songs} -> {out_filename} ({size} bytes, length: {rpt.song_length} patterns)")
        exported_files.append(out_filename)

    print(f"\n✓ Successfully exported {len(exported_files)} RPT files to {output_dir}")
    return exported_files

if __name__ == "__main__":
    nsf_file = sys.argv[1] if len(sys.argv) > 1 else "NSF/Pac-Man_CE.nsf"
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "music"
    convert_nsf_to_rpt(nsf_file, out_dir)
