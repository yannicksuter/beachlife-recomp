"""Exercise command-line ISO reads against generated data only."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from check_iso import SECTOR, both, fixture, record


def check(executable, source_test):
    executable = str(Path(executable).resolve())
    with tempfile.TemporaryDirectory(prefix='beachlife-data-') as temporary:
        root = Path(temporary)
        path = root / 'source.iso'
        body = fixture()
        path.write_bytes(body)

        def run(*args, error=None):
            result = subprocess.run([executable, *map(str, args)], capture_output=True, timeout=10)
            if error:
                assert result.returncode == 1 and error in result.stderr.decode(), result
                assert not result.stdout
            else:
                assert result.returncode == 0, result.stderr
                return result.stdout

        assert b'logical/path' in run('--help')
        assert json.loads(run('list', path))['files'] == 2
        assert run('cat', path, 'beachlife.EXE') == b'test'
        assert run('cat', path, '.\\data\\Grüsse.txt') == b'hello'
        assert run('read', path, 'Data/Grüsse.txt', 1, 3) == b'ell'
        assert run('read', path, 'Data/Grüsse.txt', 5, 0) == b''
        for offset, count in [('6', '0'), ('4', '2'), ('0', '18446744073709551615')]:
            run('read', path, 'Data/Grüsse.txt', offset, count, error='outside')
        for value in ['-1', '+1', '1x', '18446744073709551616']:
            run('read', path, 'Data/Grüsse.txt', value, 1, error='unsigned decimal')
        run('cat', path, 'missing', error='not present')
        run('cat', path, 'Data', error='directory')
        run('cat', path, '../BeachLife.exe', error='Invalid relative')
        assert hashlib.sha256(path.read_bytes()).digest() == hashlib.sha256(body).digest()

        # Unicode host paths and binary output survive argument/stdio conversion.
        unicode_path = root / 'Données 日本.iso'
        binary = b'\x00\x0a\x0d\xff'
        body[25*SECTOR:25*SECTOR+4] = binary
        unicode_path.write_bytes(body)
        assert run('cat', unicode_path, 'BEACHLIFE.exe') == binary

        # A zero-byte file is valid, including an extent at the end of the image.
        empty = fixture()
        extra = record('Empty.dat;1'.encode('utf-16-be'), 32, 0)
        pos = 23*SECTOR+68+empty[23*SECTOR+68]
        empty[pos:pos+len(extra)] = extra
        path.write_bytes(empty)
        assert run('cat', path, 'data/empty.dat') == b''

        # Case aliases are ambiguous on a Windows-style source and must fail.
        ambiguous = fixture()
        extra = record('grüsse.TXT;1'.encode('utf-16-be'), 26, 5)
        pos = 23*SECTOR+68+ambiguous[23*SECTOR+68]
        ambiguous[pos:pos+len(extra)] = extra
        path.write_bytes(ambiguous)
        run('cat', path, 'beachlife.exe', error='ambiguous')

        # A file spanning several stream buffers is copied completely.
        large = fixture()
        payload = bytes(range(256))*1024
        large.extend(bytes(128*SECTOR))
        for block in (16, 17):
            large[block*SECTOR+80:block*SECTOR+88] = both(len(large)//SECTOR, 4)
        entry = 23*SECTOR+68
        large[entry+2:entry+10] = both(32, 4)
        large[entry+10:entry+18] = both(len(payload), 4)
        large[32*SECTOR:32*SECTOR+len(payload)] = payload
        path.write_bytes(large)
        assert run('cat', path, 'Data/Grüsse.txt') == payload
        assert run('read', path, 'Data/Grüsse.txt', 65530, 40) == payload[65530:65570]

        path.write_bytes(fixture())
        subprocess.run([str(Path(source_test).resolve()), str(path)], check=True, timeout=10)
    print('Headless ISO list, Unicode/binary reads, ranges, collisions and streaming checks passed.')


if __name__ == '__main__':
    check(sys.argv[1], sys.argv[2])
