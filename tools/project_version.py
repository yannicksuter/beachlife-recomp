"""Read the sole editable release version from the root CMake project."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROJECT = re.compile(r"^(project\(beachlife_recomp VERSION )(\d+\.\d+\.\d+)( LANGUAGES C CXX\))$", re.MULTILINE)


def read_version(source=ROOT):
    matches = list(PROJECT.finditer((Path(source) / "CMakeLists.txt").read_text()))
    if len(matches) != 1:
        raise ValueError("Require one project(beachlife_recomp VERSION x.y.z LANGUAGES C CXX) line")
    version = matches[0][2]
    if any(str(int(part)) != part for part in version.split(".")):
        raise ValueError("Version components must not contain leading zeroes")
    return version


if __name__ == "__main__":
    print(read_version())
