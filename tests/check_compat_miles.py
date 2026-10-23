"""Check generated WAV/MP3 playback and explicit invalid-sample failures."""
import os
import resource
import signal
import subprocess
import sys


def main():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    environment = dict(os.environ, BEACHLIFE_BOOT_QUIET='1')
    executable = sys.argv[1]
    subprocess.run([executable], env=environment, check=True, timeout=15)
    result = subprocess.run([executable, '--invalid-sample'], env=environment,
                            capture_output=True, text=True, timeout=15)
    if (result.returncode != -signal.SIGABRT or 'AIL_set_named_sample_file' not in result.stderr or
            'invalid or truncated WAVE or MP3 file' not in result.stderr):
        raise RuntimeError('Invalid sample was not reported explicitly: '
                           f'{result.returncode}\n{result.stdout}{result.stderr}')
    print('Invalid sample loading stops explicitly.')


if __name__ == '__main__':
    main()
