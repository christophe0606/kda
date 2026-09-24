"""Read build metadata and opaque image bytes; never decode instructions."""
import hashlib
import re
import struct


def sha(data):
    return hashlib.sha256(data).hexdigest()


def hex_segments(path):
    memory = {}
    base = 0
    for line in path.read_text().splitlines():
        record = bytes.fromhex(line[1:])
        if line[0] != ':' or sum(record) & 255 or len(record) != record[0] + 5:
            raise ValueError('Invalid Intel HEX record')
        n, hi, lo, kind = record[:4]
        data = record[4:4+n]
        offset = hi * 256 + lo
        if kind == 0:
            for i, byte in enumerate(data):
                address = base + offset + i
                if address in memory and memory[address] != byte:
                    raise ValueError('Conflicting HEX bytes')
                memory[address] = byte
        elif kind == 4:
            base = int.from_bytes(data, 'big') << 16
        elif kind == 2:
            base = int.from_bytes(data, 'big') << 4
        elif kind not in (1, 3, 5):
            raise ValueError('Unsupported HEX record')
    result = []
    for address in sorted(memory):
        if not result or address != result[-1][0] + len(result[-1][1]):
            result.append((address, bytearray()))
        result[-1][1].append(memory[address])
    return [(a, bytes(b)) for a, b in result]


def elf_bytes(path, address, length):
    """Extract bytes by ELF32 virtual address solely for opaque readback hashing."""
    data = path.read_bytes()
    if data[:6] != b'\x7fELF\x01\x01':
        raise ValueError('Expected little-endian ELF32')
    # AC6 scatter-loading program headers describe load spans; section headers
    # retain execution addresses for code copied into ITCM during startup.
    shoff = struct.unpack_from('<I', data, 32)[0]
    shsize, count = struct.unpack_from('<HH', data, 46)
    for i in range(count):
        _, kind, flags, virtual, offset, size, _, _, _, _ = struct.unpack_from('<10I', data, shoff+i*shsize)
        if kind != 8 and flags & 2 and virtual <= address and address + length <= virtual + size:
            start = offset + address - virtual
            return data[start:start+length]
    raise ValueError(f'No file-backed ELF span {address:#x}+{length}')


def map_metadata(path):
    content = path.read_text()
    symbols, sections, edges, regions = {}, {}, {}, {}
    for line in content.splitlines():
        m = re.match(r'\s+(\S+)\s+(0x[\da-f]+)\s+(Thumb Code|ARM Code|Data|Number)\s+(\d+)\s+', line)
        if m:
            symbols[m[1]] = {'address': int(m[2],16), 'size': int(m[4]), 'type': m[3]}
        m = re.match(r'\s+(0x[\da-f]+)\s+(0x[\da-f]+)\s+(Code|Data|Zero|PAD)\s+(?:RO|RW)\s+\d+\s+(\S+)\s+(\S+)', line)
        if m:
            sections[f'{m[5]}({m[4]})'] = {'address':int(m[1],16), 'size':int(m[2],16), 'type':m[3]}
        m = re.match(r'\s+(\S+\([^ ]+\)) refers(?: \(Special\))? to (\S+\([^ ]+\)) for ', line)
        if m:
            edges.setdefault(m[1], set()).add(m[2])
        m = re.search(r'Execution Region (\S+) \(Base: (0x[\da-f]+), Size: (0x[\da-f]+), Max: (0x[\da-f]+)', line)
        if m:
            regions[m[1]] = {'address':int(m[2],16),'size':int(m[3],16),'capacity':int(m[4],16)}
    return symbols, sections, edges, regions


def residency(path):
    symbols, sections, edges, regions = map_metadata(path)
    roots = ['fir_batch_candidate.o(.text.fir_batch_candidate)',
             'fir_batch_baseline.o(.text.fir_batch_baseline)',
             'fir_batch_empty.o(.text.fir_batch_empty)',
             'fir_batch_empty.o(.text.fir_batch_control)',
             'fir_benchmark.o(.text.interval)']
    if 'fir_dispatch_medium' in symbols:
        # The owned initializer selects a helper pointer. Map cross references
        # cannot follow an indirect branch, so root every emitted owned FIR
        # helper, including both block-size alternatives, before closure.
        roots += sorted(key for key, section in sections.items()
                        if key.startswith('kda_fir_f32.o(.text.fir_')
                        and section['type'] == 'Code')
    closure, pending = {}, list(roots)
    while pending:
        key = pending.pop()
        if key in closure:
            continue
        if key not in sections:
            raise ValueError('Unresolved timed section: '+key)
        section = sections[key]
        region = regions['ITCM_RAM' if section['type']=='Code' else 'RW_RAM']
        if not (region['address'] <= section['address'] and
                section['address'] + section['size'] <= region['address'] + region['capacity']):
            raise ValueError('Timed section outside TCM: '+key)
        closure[key] = dict(section, references=sorted(edges.get(key, [])))
        pending.extend(sorted(edges.get(key, [])))
    for name in ('ITCM_RAM', 'RW_RAM', 'ARM_LIB_STACK'):
        if regions[name]['size'] > regions[name]['capacity']:
            raise ValueError('TCM capacity exceeded')
    return {'roots':roots, 'sections':closure, 'regions':regions,
            'benchmark':symbols['kda_fir_benchmark'],
            'stack':regions['ARM_LIB_STACK']}


def expected_readback(profile):
    """Exact programmed images and relocated timed code; opaque byte comparisons."""
    blocks = []
    for core in ('kda', 'M55_HE'):
        path = profile / f'out/{core}/DevKit-E8/Release/{core}.hex'
        for address, data in hex_segments(path):
            blocks.append((f'{core}:image', address, data))
    placement = residency(profile / 'out/kda/DevKit-E8/Release/kda.axf.map')
    elf = profile / 'out/kda/DevKit-E8/Release/kda.axf'
    for name, section in sorted(placement['sections'].items()):
        if section['type'] == 'Code':
            blocks.append((name, section['address'], elf_bytes(elf, section['address'], section['size'])))
    return [(name, address+offset, data[offset:offset+4096])
            for name,address,data in blocks for offset in range(0,len(data),4096)]
