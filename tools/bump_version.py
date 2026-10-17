"""Update the CMake version before a main -> stable promotion."""
import argparse
import re
from pathlib import Path
from project_version import PROJECT, ROOT, read_version


def bump(source, request):
    source = Path(source)
    old = read_version(source)
    parts = [int(part) for part in old.split(".")]
    if request in ("major", "minor", "patch"):
        index = ("major", "minor", "patch").index(request)
        parts[index] += 1
        parts[index + 1:] = [0] * (2 - index)
        new = ".".join(map(str, parts))
    elif re.fullmatch(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)", request):
        new = request
    else:
        raise ValueError("Use patch, minor, major or x.y.z")
    if tuple(map(int, new.split("."))) <= tuple(map(int, old.split("."))):
        raise ValueError("The new version must be greater than the current version")
    path = source / "CMakeLists.txt"
    path.write_text(PROJECT.sub(lambda m: m[1] + new + m[3], path.read_text(), count=1))
    return old, new


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", help="patch, minor, major or x.y.z")
    args = parser.parse_args()
    try:
        old, new = bump(ROOT, args.version)
    except ValueError as error:
        parser.error(str(error))
    print(f"{old} -> {new}; commit on main, then open the main -> stable PR")
