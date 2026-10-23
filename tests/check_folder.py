"""Keep generated writable VFS fixtures separate from project/game data."""
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix='beachlife-folder-') as temporary:
    subprocess.run([str(Path(sys.argv[1]).resolve()), temporary], check=True, timeout=30)
