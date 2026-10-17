"""Inspect generated InstallShield 6 metadata inside an ISO; no retail fixtures."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from check_iso import SECTOR, both, fixture, record

DESCRIPTOR = 64
TABLE = DESCRIPTOR + 0x400
RECORDS = TABLE + 16


def header_fixture():
    data = bytearray(4096)
    struct.pack_into('<5I', data, 0, 0x28635349, 0x01006000, 0, DESCRIPTOR, 0x400)
    for offset, value in [(0xc, 0x400), (0x14, len(data)-TABLE),
                          (0x18, len(data)-TABLE), (0x1c, 1), (0x28, 3), (0x2c, 16),
                          (0x3e, 0x280), (0x15a, 0x2e0)]:
        struct.pack_into('<I', data, DESCRIPTOR+offset, value)
    struct.pack_into('<I', data, TABLE, 0x200)
    for index, (flags, size, compressed, offset, name, volume) in enumerate([
            (4, 0x100000003, 11, 512, 0x220, 2), (0, 7, 7, 0, 0x240, 1), (8, 0, 0, 0, 0, 1)]):
        start = RECORDS+index*0x57
        struct.pack_into('<HQQQ', data, start, flags, size, compressed, offset)
        struct.pack_into('<IH', data, start+0x3a, name, 0)
        struct.pack_into('<H', data, start+0x55, volume)
    # One group, one component and one group-name reference, all relative to the descriptor.
    struct.pack_into('<3I', data, DESCRIPTOR+0x280, 0x2c0, 0x2a0, 0)
    struct.pack_into('<I', data, DESCRIPTOR+0x2a0, 0x2c0)
    struct.pack_into('<2I', data, DESCRIPTOR+0x2b6, 0, 2)
    struct.pack_into('<3I', data, DESCRIPTOR+0x2e0, 0x380, 0x300, 0)
    struct.pack_into('<I', data, DESCRIPTOR+0x300, 0x380)
    struct.pack_into('<HI', data, DESCRIPTOR+0x36f, 1, 0x3a0)
    struct.pack_into('<I', data, DESCRIPTOR+0x3a0, 0x2c0)
    for offset, value in [(DESCRIPTOR+0x2c0, b'Game "files"'), (DESCRIPTOR+0x380, b'Fixture'),
                          (TABLE+0x200, b'Data\\Scenes'), (TABLE+0x220, b'sample\n\xe9.bin'),
                          (TABLE+0x240, b'external.bin')]:
        data[offset:offset+len(value)+1] = value+b'\0'
    return data


def iso_with_header(header):
    image = fixture()
    blocks = (len(header)+SECTOR-1)//SECTOR
    image.extend(bytes(blocks*SECTOR))
    image[32*SECTOR:32*SECTOR+len(header)] = header
    for block in (16, 17):
        image[block*SECTOR+80:block*SECTOR+88] = both(32+blocks, 4)
    position = 22*SECTOR
    while image[position]:
        position += image[position]
    entry = record('DaTa1.HdR;1'.encode('utf-16-be'), 32, len(header))
    image[position:position+len(entry)] = entry
    return image


def check(executable):
    with tempfile.TemporaryDirectory(prefix='beachlife-cabinet-') as temporary:
        path = Path(temporary) / 'Données 日本.iso'

        def inspect(header, error=None, missing=False):
            body = fixture() if missing else iso_with_header(header)
            path.write_bytes(body)
            before = hashlib.sha256(body).digest()
            result = subprocess.run([str(Path(executable).resolve()), 'cabinets', str(path)],
                                    capture_output=True, timeout=15)
            assert hashlib.sha256(path.read_bytes()).digest() == before
            if error:
                assert result.returncode == 1, (result.returncode, result.stderr)
                assert error in result.stderr.decode(), (error, result.stderr)
                assert not result.stdout
            else:
                assert result.returncode == 0 and not result.stderr, result.stderr
                return json.loads(result.stdout)

        result = inspect(header_fixture())
        assert result['format_version'] == 6 and result['header_bytes'] == 4096
        assert result['name_encoding'] == 'byte-values'
        assert result['components'] == ['Fixture']
        assert result['directories'] == ['Data\\Scenes']
        assert result['groups'] == [{'name': 'Game "files"', 'first_file': 0, 'last_file': 2}]
        files = result['files']
        assert [x['kind'] for x in files] == ['cabinet', 'external', 'placeholder']
        assert files[0]['name'] == 'sample\n\xe9.bin'
        assert (files[0]['size'], files[0]['compressed_size'], files[0]['data_offset'], files[0]['volume']) == (0x100000003, 11, 512, 2)
        assert files[1]['name'] == 'external.bin' and not files[2]['name']
        # The generated ISO contains only metadata, never data1.cab/data2.cab.
        inspect(b'', 'not present', missing=True)
        for length in (0, 4, 19, 100, len(header_fixture())-1):
            inspect(header_fixture()[:length], 'outside')
        for offset, value, error in [
                (0, 0, 'InstallShield 6'), (4, 0x01007000, 'InstallShield 6'),
                (12, 0xfffffff0, 'outside'), (16, 1, 'descriptor'),
                (DESCRIPTOR+0x18, 7, 'sizes disagree'),
                (DESCRIPTOR+0x1c, 100001, 'count'),
                (DESCRIPTOR+0x1c, 0, 'count'),
                (DESCRIPTOR+0x28, 1000, 'directory table'),
                (DESCRIPTOR+0x2c, 4096, 'file descriptors'),
                (TABLE, 0xfffffff0, 'outside'),
                (RECORDS+0x3a, 0xfffffff0, 'outside'),
                (DESCRIPTOR+0x288, 0x280, 'Cyclic'),
                (DESCRIPTOR+0x284, 0, 'Missing'),
                (DESCRIPTOR+0x2a0, 0, 'Missing'),
                (DESCRIPTOR+0x2ba, 3, 'file range'),
                (DESCRIPTOR+0x371, 0xfffffff0, 'outside'),
                (DESCRIPTOR+0x3a0, 0, 'Missing')]:
            data = header_fixture(); struct.pack_into('<I', data, offset, value)
            inspect(data, error)
        data = header_fixture(); struct.pack_into('<H', data, RECORDS+0x3e, 1)
        inspect(data, 'directory index')
        data = header_fixture(); struct.pack_into('<H', data, DESCRIPTOR+0x36f, 72)
        inspect(data, 'component group table')
        data = header_fixture(); struct.pack_into('<I', data, RECORDS+0x3a, len(data)-TABLE-1); data[-1] = 65
        inspect(data, 'unterminated')
        inspect(bytes(16*1024*1024+1), '16 MiB')
    print('Generated cabinet metadata, callbacks, 64-bit sizes, names, ranges and cycles passed.')


if __name__ == '__main__':
    check(sys.argv[1])
