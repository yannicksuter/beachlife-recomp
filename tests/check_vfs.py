"""Prepare and mount only generated ISO/cabinet data, including interrupted imports."""
import hashlib
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

from content_fixture import content_files, iso_files, RECORDS


def identity(image):
    digest = hashlib.sha256(image).hexdigest()
    return hashlib.sha256(f'beachlife-content-1\n{digest}\nuk-english-v1\nen\n'.encode()).hexdigest()


def check(executable, api_test):
    executable, api_test = map(lambda x: str(Path(x).resolve()), (executable, api_test))
    windows_guest = subprocess.check_output([api_test, '--platform'], timeout=10).strip() == b'windows'
    # Wine's Linux symlinks are not Windows reparse points. Exercise links made
    # through the guest API in folder_source_tests, and host links on native CI.
    host_links = os.name == 'nt' or not windows_guest
    with tempfile.TemporaryDirectory(prefix='beachlife-vfs-') as temporary:
        base = Path(temporary)
        files, expected = content_files()
        original = iso_files(files)
        image = base/'Données 日本.iso'
        image.write_bytes(original)
        cache = base/'Données 日本'

        def run(*args, fails=False):
            result = subprocess.run([executable, *map(str, args)], capture_output=True, timeout=30)
            if fails:
                assert result.returncode == 1 and result.stderr and not result.stdout, result
            else:
                assert result.returncode == 0, result.stderr.decode(errors='replace')
                return result.stdout

        prepared = json.loads(run('prepare', image, cache))
        # Use the independently calculated host path also under Wine wrappers.
        root = cache/identity(original)
        assert prepared['identity'] == root.name and prepared['files'] == 6
        assert prepared['written'] == 6 and prepared['reused'] == 0
        manifest = json.loads(run('vfs-list', root))
        assert manifest['source_sha256'] == hashlib.sha256(original).hexdigest()
        assert manifest['total_bytes'] == prepared['bytes'] == sum(map(len, expected.values()))
        for item in manifest['files']:
            assert item['sha256'] == hashlib.sha256(expected[item['path']]).hexdigest()
        assert {p.relative_to(root/'files').as_posix() for p in (root/'files').rglob('*') if p.is_file()} == set(expected)
        run('verify', root)
        assert image.read_bytes() == original

        # A prepared mount does not need its source ISO to exist.
        renamed = base/'renamed.iso'
        image.rename(renamed)
        rng = random.Random(53)
        for path, body in expected.items():
            assert run('vfs-cat', root, path.swapcase().replace('/', '\\')) == body
            for _ in range(4):
                start = rng.randrange(len(body)+1)
                count = rng.randrange(len(body)-start+1)
                assert run('vfs-read', root, path, start, count) == body[start:start+count]
        for path in ('../BeachLife.ini', '/BeachLife.ini', 'Setup.exe', 'Data', 'manifest.json'):
            run('vfs-cat', root, path, fails=True)
        run('vfs-read', root, 'BeachLife.ini', 0, 9999, fails=True)
        times = {p: (root/'files'/p).stat().st_mtime_ns for p in expected}
        repeated = json.loads(run('prepare', renamed, cache))
        assert repeated['identity'] == root.name and repeated['reused'] == 6 and repeated['written'] == 0
        assert times == {p: (root/'files'/p).stat().st_mtime_ns for p in expected}

        # Same source filename, different contents => separate prepared set.
        changed = bytearray(original); changed[-1] ^= 1
        renamed.write_bytes(changed)
        other = json.loads(run('prepare', renamed, cache))
        assert other['identity'] != root.name and (root/'manifest.json').is_file()
        run('verify', root)
        renamed.write_bytes(original)

        for mode in ('same-size', 'truncated', 'missing'):
            asset = root/'files/Data/Sprites/test.dat'
            if mode == 'same-size':
                damaged = bytearray(asset.read_bytes()); damaged[100] ^= 1; asset.write_bytes(damaged)
            elif mode == 'truncated': asset.write_bytes(b'bad')
            else: asset.unlink()
            run('verify', root, fails=True)
            fixed = json.loads(run('prepare', renamed, cache))
            assert fixed['reused'] == 5 and fixed['written'] == 1
            assert asset.read_bytes() == expected['Data/Sprites/test.dat']
            run('verify', root)

        valid_manifest = (root/'manifest.json').read_bytes()
        for field, value in [('format', 999), ('source_sha256', '0'*64), ('total_bytes', 0)]:
            invalid = dict(manifest); invalid[field] = value
            (root/'manifest.json').write_text(json.dumps(invalid))
            run('vfs-list', root, fails=True)
        (root/'manifest.json').write_bytes(valid_manifest)

        # Never adopt someone else's populated directory, or follow content links.
        foreign = base/'foreign'/root.name; foreign.mkdir(parents=True)
        (foreign/'saves').write_bytes(b'keep me')
        run('prepare', renamed, foreign.parent, fails=True)
        assert (foreign/'saves').read_bytes() == b'keep me'
        sentinel = base/'sentinel'; sentinel.write_bytes(expected['Data/loose.txt'])
        target = root/'files/Data/loose.txt'
        target.unlink()
        if host_links:
            try:
                target.symlink_to(sentinel)
            except OSError:
                print('File symlink check unavailable: host lacks link privileges.')
            else:
                run('vfs-cat', root, 'Data/loose.txt', fails=True)
                run('prepare', renamed, cache, fails=True)
                assert sentinel.read_bytes() == expected['Data/loose.txt']
                target.unlink()
        target.write_bytes(expected['Data/loose.txt'])
        target.unlink()
        os.link(sentinel, target)
        run('prepare', renamed, cache, fails=True)
        run('vfs-cat', root, 'Data/loose.txt', fails=True)
        target.unlink(); target.write_bytes(expected['Data/loose.txt'])

        stage = root/'.stage'
        stage.rmdir()
        outside = base/'outside'; outside.mkdir()
        if host_links:
            try:
                stage.symlink_to(outside, target_is_directory=True)
            except OSError:
                print('Directory symlink check unavailable: host lacks link privileges.')
            else:
                run('prepare', renamed, cache, fails=True)
                assert not list(outside.iterdir())
                stage.unlink()
        else:
            print('Host symlink checks skipped for a Windows executable under a POSIX host.')
        stage.mkdir()

        # Corrupt/missing cabinet data can leave verified entries, never readiness.
        for case in ('missing-volume', 'truncated-volume', 'bad-checksum'):
            bad = {key: bytearray(value) for key, value in files.items()}
            if case == 'missing-volume': del bad['data2.cab']
            elif case == 'truncated-volume': bad['data2.cab'] = bad['data2.cab'][:84]
            else: struct.pack_into('<I', bad['data1.hdr'], RECORDS+2*0x57+26, 0)
            image.write_bytes(iso_files(bad))
            failed_cache = base/case
            run('prepare', image, failed_cache, fails=True)
            assert not list(failed_cache.rglob('manifest.json'))
            assert not list(failed_cache.rglob('*.part'))

        # API cancellation is deterministic, including cancellation mid-write.
        ascii_image = base/'source.iso'; ascii_image.write_bytes(original)
        api_cache = base/'api-cache'
        subprocess.run([api_test, str(ascii_image), str(api_cache)], check=True, timeout=30)
        api_root = api_cache/root.name
        held = subprocess.Popen([api_test, 'hold', str(api_root)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            assert held.stdout.readline().strip() == b'locked'
            run('prepare', ascii_image, api_cache, fails=True)
            run('verify', api_root)  # two readers can share a mount
        finally:
            held.terminate(); held.communicate(timeout=10)
        assert json.loads(run('prepare', ascii_image, api_cache))['reused'] == 6

        # System ancestors can be aliases, as with macOS /var -> /private/var.
        # Public preparation resolves them before using the low-level storage API.
        if host_links:
            actual = base/'api-real'; actual.mkdir()
            alias = base/'api-alias'
            try:
                alias.symlink_to(actual, target_is_directory=True)
            except OSError:
                print('Ancestor alias check unavailable: host lacks link privileges.')
            else:
                (actual/'source.iso').write_bytes(original)
                subprocess.run([api_test, str(alias/'source.iso'), str(alias/'cache')],
                               check=True, timeout=30)
    print('ISO preparation, identity, offline VFS, repair, path safety and process-lock recovery passed.')


if __name__ == '__main__': check(*sys.argv[1:])
