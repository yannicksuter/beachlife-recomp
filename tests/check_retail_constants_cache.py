"""Exercise the existing cache route using generated data, never original code."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from content_fixture import content_files, iso_files


def check(executable):
    with tempfile.TemporaryDirectory(prefix='beachlife-constants-cache-') as directory:
        base = Path(directory)
        files, expected = content_files()
        original = iso_files(files)
        image = base/'source.iso'
        image.write_bytes(original)
        digest = hashlib.sha256(original).hexdigest()
        identity = hashlib.sha256(
            f'beachlife-content-1\n{digest}\nuk-english-v1\nen\n'.encode()).hexdigest()
        cache = base/'cache'
        root = cache/identity
        (root/'.stage').mkdir(parents=True)
        (root/'.lock').write_bytes(b'')  # PreparedSource uses an existing real lock file.
        (root/'.owner').write_text('beachlife-content\n'+identity+'\n', encoding='ascii')
        # The pre-upgrade application prepared Data/INI from the same disc,
        # with exactly this format and identity, but excluded the executable.
        legacy = {path: body for path, body in expected.items() if path != 'BeachLife.exe'}
        entries = []
        for path, body in legacy.items():
            target = root/'files'/path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(body)
            entries.append({'path': path, 'size': len(body),
                            'sha256': hashlib.sha256(body).hexdigest()})
        manifest = {'format': 1, 'source_sha256': digest, 'profile': 'uk-english-v1',
                    'language': 'en', 'total_bytes': sum(map(len, legacy.values())),
                    'files': entries}
        (root/'manifest.json').write_text(json.dumps(manifest), encoding='ascii')
        result = subprocess.run(
            [str(Path(executable).resolve()), str(image), str(cache), identity],
            capture_output=True, timeout=30)
        sys.stdout.buffer.write(result.stdout)
        sys.stderr.buffer.write(result.stderr)
        if result.returncode:
            return result.returncode
        assert (base/'disconnected.iso').read_bytes() == original
        assert not image.exists()
        repaired = json.loads((root/'manifest.json').read_text(encoding='utf-8'))
        assert repaired['format'] == 1 and repaired['source_sha256'] == digest
        assert {entry['path'] for entry in repaired['files']} == set(expected)
        for path, body in expected.items():
            assert (root/'files'/path).read_bytes() == body
        assert not (root/'files/Setup.exe').exists()
        return 0


if __name__ == '__main__':
    sys.exit(check(sys.argv[1]))
