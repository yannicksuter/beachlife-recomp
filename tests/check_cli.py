"""Check the actual executable's public command-line and embedded identity."""
import json
import subprocess
import sys
from pathlib import Path


def check(executable, expected):
    executable = str(Path(executable).resolve())
    for arguments in (["--help"],):
        output = subprocess.check_output([executable, *arguments], text=True)
        assert "The game code is not reconstructed yet" in output, output
    version = subprocess.check_output([executable, "--version"], text=True).strip()
    assert version == expected["build_version"], (version, expected)
    info = json.loads(subprocess.check_output([executable, "--build-info"], text=True))
    assert info == expected, (info, expected)
    for arguments in (["--unknown"], ["--help", "extra"]):
        result = subprocess.run([executable, *arguments], capture_output=True, text=True)
        assert result.returncode == 2 and result.stderr, result


if __name__ == "__main__":
    check(sys.argv[1], json.loads(Path(sys.argv[2]).read_text()))
