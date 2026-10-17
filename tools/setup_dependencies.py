"""Fetch only the submodule revisions pinned by the project index."""
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

if __name__ == '__main__':
    subprocess.run(['git', 'submodule', 'update', '--init', '--depth', '1', '--',
                    'extern/sdl', 'extern/imgui', 'extern/stb'], cwd=ROOT, check=True)
