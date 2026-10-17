"""Refresh generated identity on every build, writing only changed content."""
import argparse
import json
import re
import subprocess
from pathlib import Path
from project_version import read_version


def git_output(source, *args, git="git"):
    return subprocess.check_output([git, "-C", str(source), *args], text=True).strip()


def identity(source, config, git="git"):
    result = dict(config)
    if result["project_version"] != read_version(source):
        raise ValueError("CMake version changed; reconfigure before building")
    if result["channel"] not in ("dev", "test", "release"):
        raise ValueError("Unknown build channel")
    for field in ("pull_request", "run_id"):
        if not re.fullmatch(r"\d+", str(result[field])):
            raise ValueError(f"Invalid {field}")
        result[field] = int(result[field])
    if result["channel"] == "test" and not result["pull_request"]:
        raise ValueError("A test build requires a pull request number")
    result["commit"] = git_output(source, "rev-parse", "HEAD", git=git)
    if not re.fullmatch(r"[0-9a-f]{40}", result["commit"]):
        raise ValueError("Invalid source revision")
    result["dirty"] = bool(git_output(source, "status", "--porcelain=v1", "--untracked-files=normal", git=git))
    result["commit_time"] = int(git_output(source, "show", "-s", "--format=%ct", "HEAD", git=git))
    result["build_version"] = result["project_version"] + "+g" + result["commit"][:12]
    if result["dirty"]:
        result["build_version"] += ".dirty"
    result["capability"] = "launcher"
    return result


def write_changed(path, content):
    if not path.exists() or path.read_text() != content:
        path.write_text(content)


def generate(source, config, output, git="git"):
    data = identity(source, config, git)
    encoded = json.dumps(data, sort_keys=True, indent=2) + "\n"
    output.mkdir(parents=True, exist_ok=True)
    write_changed(output / "build-info.json", encoded)
    header = ('#pragma once\nnamespace beachlife::build {\n'
              f'inline constexpr char version[] = "{data["build_version"]}";\n'
              f'inline constexpr char json[] = R"buildinfo({encoded})buildinfo";\n'
              '}\n')
    write_changed(output / "build_version.h", header)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--git", default="git")
    args = parser.parse_args()
    generate(args.source, json.loads(args.config.read_text()), args.output, args.git)
