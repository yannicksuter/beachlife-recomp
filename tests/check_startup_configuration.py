"""Qualify generated configuration ownership and allocation-failure boundaries.

Both process status and completion evidence are required. Deliberate construction
failures must abort cleanly; an access violation or sanitizer report is a failure.
"""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess


def no_core_dump():
    import resource
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def check(executable, default_executable, emulator=None):
    executable = str(Path(executable).resolve())
    default_executable = str(Path(default_executable).resolve())
    prefix = shlex.split(emulator) if emulator else []
    windows = os.name == 'nt' or Path(executable).suffix.lower() == '.exe'
    expected_abort = 3 if windows else -6
    subprocesses = 0

    def run(binary, *arguments, abort=False):
        nonlocal subprocesses
        command = [*prefix, binary, *map(str, arguments)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=30,
                                **({'preexec_fn': no_core_dump} if os.name != 'nt' else {}))
        subprocesses += 1
        output = result.stdout + result.stderr
        if abort:
            assert result.returncode == expected_abort, (command, result.returncode, output)
            assert 'INJECTED allocation=' in output and 'UNEXPECTED SURVIVAL' not in output, output
            assert not re.search(r'ERROR: (?:AddressSanitizer|LeakSanitizer)|runtime error:', output), output
        else:
            assert result.returncode == 0, (command, result.returncode, output)
        return output

    for binary in (executable, default_executable):
        output = run(binary, 'generated')
        completed = re.search(r'STARTUP_CONFIGURATION_SUCCESS checks=(\d+)', output)
        assert completed and int(completed[1]) > 0, output
    baseline = run(executable, 'fault', 0)
    events = [(int(index), int(size), bool(int(array)), bool(int(published)))
              for index, size, array, published in
              re.findall(r'ALLOCATION (\d+) (\d+) ([01]) ([01])', baseline)]
    metadata = re.search(r'ALLOCATION_BASELINE count=(\d+) owner_bytes=(\d+) token_bytes=(\d+)', baseline)
    assert metadata and events and len(events) == int(metadata[1]), baseline
    assert [event[0] for event in events] == list(range(1, len(events) + 1)), baseline
    first_published = next(index for index, _, _, published in events if published)
    owner_allocation = first_published - 1
    # Actual construction publishes its singleton before its first bridged open.
    # The allocation immediately before that publication is the owner object.
    assert events[owner_allocation - 1][1:] == (int(metadata[2]), False, False), baseline
    for index, _, _, _ in events:
        if index < owner_allocation:
            output = run(executable, 'fault', index)
            assert f'RECOVERED {index}' in output and 'INJECTED allocation=' in output, output
        else:
            run(executable, 'fault', index, abort=True)
    tokens = [index for index, size, array, published in events
              if size == int(metadata[3]) and array and published]
    assert len(tokens) == 1, baseline
    run(executable, 'fault-direct', tokens[0], abort=True)
    output = run(executable, 'fault-exception', owner_allocation)
    assert f'UNRELATED_PROPAGATED {owner_allocation}' in output, output
    for stage in range(1, 4):
        output = run(executable, 'bridge', 'default', stage, 0)
        count = re.search(rf'BRIDGE_BASELINE {stage} (\d+)', output)
        assert count and int(count[1]) >= 2, output
        for index in range(1, int(count[1]) + 1):
            output = run(executable, 'bridge', 'default', stage, index)
            assert f'BRIDGE_RECOVERED {stage} {index}' in output, output
            run(executable, 'bridge', 'strict', stage, index, abort=True)
    print(f'Generated configuration ownership and failure policy passed: '
          f'{len(events)} allocation positions, {subprocesses} subprocesses.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable')
    parser.add_argument('default_executable')
    parser.add_argument('--emulator', help='Optional command prefix for cross-built fixtures, such as wine.')
    args = parser.parse_args()
    check(args.executable, args.default_executable, args.emulator)
