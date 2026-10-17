"""Check the native ISO indexer using generated images, with no retail inputs."""
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

SECTOR = 2048


def both(value, count):
    return value.to_bytes(count, 'little') + value.to_bytes(count, 'big')


def record(name, block, size, directory=False):
    body = bytearray(33 + len(name) + (len(name) % 2 == 0))
    body[0] = len(body)
    body[2:10] = both(block, 4)
    body[10:18] = both(size, 4)
    body[25] = 2 if directory else 0
    body[28:32] = both(1, 2)
    body[32] = len(name)
    body[33:33+len(name)] = name
    return body


def fixture(joliet=True):
    image = bytearray(32 * SECTOR)

    def descriptor(kind, root, unicode=False):
        data = bytearray(SECTOR)
        data[0:7] = bytes([kind]) + b'CD001\x01'
        label = 'TEST DISC'
        data[40:72] = label.ljust(16).encode('utf-16-be') if unicode else label.ljust(32).encode()
        data[80:88] = both(32, 4)
        data[120:124] = both(1, 2)
        data[124:128] = both(1, 2)
        data[128:132] = both(SECTOR, 2)
        data[156:190] = record(b'\0', root, SECTOR, True)
        data[881] = 1
        if unicode:
            data[88:91] = b'%/E'
        return data

    image[16*SECTOR:17*SECTOR] = descriptor(1, 20)
    end = 17
    if joliet:
        image[17*SECTOR:18*SECTOR] = descriptor(2, 22, True)
        end = 18
    image[end*SECTOR:end*SECTOR+7] = b'\xffCD001\x01'
    for root, unicode in [(20, False)] + ([(22, True)] if joliet else []):
        name = lambda text: text.encode('utf-16-be' if unicode else 'ascii')
        root_body = (record(b'\0', root, SECTOR, True) + record(b'\1', root, SECTOR, True) +
                     record(name('Data' if unicode else 'DATA'), root+1, SECTOR, True) +
                     record(name('BeachLife.exe;1' if unicode else 'BEACHLIFE.EXE;1'), 25, 4))
        folder_body = (record(b'\0', root+1, SECTOR, True) + record(b'\1', root, SECTOR, True) +
                       record(name('Grüsse.txt;1' if unicode else 'HELLO.TXT;1'), 26, 5))
        image[root*SECTOR:root*SECTOR+len(root_body)] = root_body
        image[(root+1)*SECTOR:(root+1)*SECTOR+len(folder_body)] = folder_body
    image[25*SECTOR:25*SECTOR+4] = b'test'
    image[26*SECTOR:26*SECTOR+5] = b'hello'
    return image


def check(executable):
    with tempfile.TemporaryDirectory(prefix='beachlife-iso-') as temporary:
        path = Path(temporary) / 'source.iso'

        def inspect(body, error=None):
            path.write_bytes(body)
            digest = hashlib.sha256(body).digest()
            result = subprocess.run([str(Path(executable).resolve()), '--inspect-iso', str(path)],
                                    capture_output=True, text=True, encoding='utf-8', timeout=10)
            assert hashlib.sha256(path.read_bytes()).digest() == digest, 'The indexer modified the image'
            if error:
                assert result.returncode == 1 and error in result.stderr, (error, result.stdout, result.stderr)
                assert not result.stdout
            else:
                assert result.returncode == 0, result.stderr
                return json.loads(result.stdout)

        result = inspect(fixture())
        assert (result['volume'], result['filesystem'], result['files'], result['directories'], result['file_bytes']) == ('TEST DISC', 'Joliet', 2, 1, 9)
        entries = {e['path']: e for e in result['entries']}
        assert entries['Data']['directory']
        assert entries['Data/Grüsse.txt']['offset'] == 26*SECTOR
        assert entries['BeachLife.exe']['size'] == 4
        body = fixture(); body[17*SECTOR+58:17*SECTOR+72] = bytes(14)
        assert inspect(body)['volume'] == 'TEST DISC'
        result = inspect(fixture(False))
        assert result['filesystem'] == 'ISO 9660'
        assert 'DATA/HELLO.TXT' in [e['path'] for e in result['entries']]
        inspect(b'not an iso', 'truncated')
        inspect(fixture()[:-SECTOR], 'volume size')
        body = fixture(); body[16*SECTOR+1] = 0
        inspect(body, 'not a supported ISO')
        body = fixture(); body[18*SECTOR] = 0
        inspect(body, 'not a supported ISO')
        body = fixture(); body[17*SECTOR+84] = 1
        inspect(body, 'byte-order')
        body = fixture(); body[22*SECTOR+68] = 34
        inspect(body, 'truncated')
        body = fixture(); body[22*SECTOR+68+2:22*SECTOR+68+10] = both(33, 4)
        inspect(body, 'extent is outside')
        body = fixture(); body[22*SECTOR+68+2:22*SECTOR+68+10] = both(22, 4)
        inspect(body, 'cyclic directory')
        body = fixture(); body[23*SECTOR+68+33:23*SECTOR+68+35] = b'\xd8\x00'
        inspect(body, 'invalid text')
        body = fixture(); body[22*SECTOR+68+33:22*SECTOR+68+35] = b'\x00/'
        inspect(body, 'unsafe filename')
        body = fixture(); body[23*SECTOR+68+25] = 0x80
        inspect(body, 'Multi-extent')
        body = fixture(); body[17*SECTOR+128:17*SECTOR+132] = both(1024, 2)
        inspect(body, '2048-byte')
        body = fixture(); body[23*SECTOR+68+32] = 21
        inspect(body, 'invalid Joliet')
        body = fixture(); body[22*SECTOR+68+28:22*SECTOR+68+32] = both(2, 2)
        inspect(body, 'Multi-volume')
        body = fixture(); body[23*SECTOR+68+26] = 1
        inspect(body, 'Interleaved')
        body = fixture()
        duplicate = record('Grüsse.txt;1'.encode('utf-16-be'), 26, 5)
        start = 23*SECTOR+68+body[23*SECTOR+68]
        body[start:start+len(duplicate)] = duplicate
        inspect(body, 'duplicate file paths')
        # A zero-length record skips padding to the next block, not the directory.
        body = fixture(False)
        body[16*SECTOR+166:16*SECTOR+174] = both(2*SECTOR, 4)
        body[20*SECTOR+68] = 0
        body[21*SECTOR:22*SECTOR] = bytes(SECTOR)
        extra = record(b'LATE.TXT;1', 26, 5)
        body[21*SECTOR:21*SECTOR+len(extra)] = extra
        result = inspect(body)
        assert result['files'] == 1 and result['entries'][0]['path'] == 'LATE.TXT'
        # Records may not span a logical sector boundary.
        # Use a valid skipped self record to reach the final short block tail.
        body = fixture(False)
        pos = 20*SECTOR
        while pos + 34 <= 21*SECTOR:
            body[pos:pos+34] = record(b'\0', 20, SECTOR, True)
            pos += 34
        body[pos] = 34
        inspect(body, 'block boundary')
    print('Generated ISO/Joliet, Unicode, bounds and malformed-directory checks passed.')


if __name__ == '__main__':
    check(sys.argv[1])
