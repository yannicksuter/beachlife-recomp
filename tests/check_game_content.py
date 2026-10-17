"""Exercise complete generated cabinet payloads through the native ISO adapter."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from content_fixture import content_files, iso_files, RECORDS


def check(executable):
    executable = str(Path(executable).resolve())
    with tempfile.TemporaryDirectory(prefix='beachlife-content-') as temporary:
        image = Path(temporary)/'Données 日本.iso'
        files, expected = content_files()
        original = iso_files(files)
        image.write_bytes(original)
        index = json.loads(subprocess.check_output([executable, 'game-files', str(image)], timeout=15))
        assert {x['path'] for x in index['files']} == set(expected)
        for name, data in expected.items():
            result = subprocess.run([executable, 'game-cat', str(image), name], capture_output=True, timeout=15)
            assert result.returncode == 0 and not result.stderr, result.stderr
            assert result.stdout == data
        assert image.read_bytes() == original

        def rejects(files, path='Data/Sprites/test.dat'):
            body = iso_files(files); image.write_bytes(body)
            result = subprocess.run([executable, 'game-cat', str(image), path], capture_output=True, timeout=15)
            assert result.returncode == 1 and result.stderr, (result.returncode, result.stderr)
            assert hashlib.sha256(image.read_bytes()).digest() == hashlib.sha256(body).digest()

        bad = {k:bytearray(v) for k,v in files.items()}; del bad['data2.cab']; rejects(bad)
        bad = {k:bytearray(v) for k,v in files.items()}; bad['data2.cab'] = bad['data2.cab'][:84]; rejects(bad)
        for offset, fmt, value in [(RECORDS+2*0x57+26, '<I', 0),
                                   (RECORDS+2*0x57, '<H', 5),
                                   (RECORDS+2*0x57+0x54, '<B', 1),
                                   (RECORDS+2*0x57+2, '<Q', 2),
                                   (RECORDS+2*0x57+10, '<Q', 0x100000000),
                                   (RECORDS+2*0x57+18, '<Q', 1)]:
            bad = {k:bytearray(v) for k,v in files.items()}
            struct.pack_into(fmt, bad['data1.hdr'], offset, value); rejects(bad)
        payload = struct.unpack_from('<Q', files['data1.hdr'], RECORDS+2*0x57+18)[0]
        for size in (0, 65535):
            bad = {k:bytearray(v) for k,v in files.items()}
            struct.pack_into('<H', bad['data2.cab'], payload, size); rejects(bad)
        # An alias may refer to an earlier identical payload. The fixture's
        # script descriptor becomes a second sprite name without copying bytes.
        alias = {k:bytearray(v) for k,v in files.items()}
        alias['data1.hdr'][RECORDS+0x57:RECORDS+0x57+42] = alias['data1.hdr'][RECORDS+2*0x57:RECORDS+2*0x57+42]
        struct.pack_into('<I', alias['data1.hdr'], RECORDS+0x57+0x4c, 2)
        alias['data1.hdr'][RECORDS+0x57+0x54] = 1
        image.write_bytes(iso_files(alias))
        result = subprocess.run([executable, 'game-cat', str(image), 'Data/Script/sample.pis'], capture_output=True, timeout=15)
        assert result.returncode == 0 and result.stdout == expected['Data/Sprites/test.dat'], result.stderr
        for previous in (1, 999999):
            bad = {k:bytearray(v) for k,v in alias.items()}
            struct.pack_into('<I', bad['data1.hdr'], RECORDS+0x57+0x4c, previous)
            rejects(bad, 'Data/Script/sample.pis')
        bad = {k:bytearray(v) for k,v in alias.items()}
        bad['data1.hdr'][RECORDS+0x57+26] ^= 1
        rejects(bad, 'Data/Script/sample.pis')
        bad = {k:bytearray(v) for k,v in files.items()}; bad['Data/loose.txt'][0] ^= 1
        rejects(bad, 'Data/loose.txt')
        image.write_bytes(original)
        result = subprocess.run([executable, 'game-cat', str(image), 'Setup.exe'], capture_output=True, timeout=15)
        assert result.returncode == 1 and not result.stdout
    print('Generated game mapping, compressed/empty/external reads, checksums and malformed payloads passed.')


if __name__ == '__main__': check(sys.argv[1])
