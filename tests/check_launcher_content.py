"""Exercise the asynchronous launcher worker with generated ISO/cabinet inputs."""
from pathlib import Path
import subprocess
import sys
import tempfile

from content_fixture import content_files, iso_files


def check(executable):
    with tempfile.TemporaryDirectory(prefix='beachlife-setup-') as directory:
        root = Path(directory)
        image = root/'source.iso'
        image.write_bytes(iso_files(content_files()[0]))
        subprocess.run([str(Path(executable).resolve()), str(image), str(root/'cache')], check=True, timeout=60)


if __name__ == '__main__': check(sys.argv[1])
