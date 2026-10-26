"""Publish verified PR test packages, or the stable CMake version as the merged PR describes it."""
import argparse
import json
import re
import subprocess
import tempfile
from pathlib import Path
from package_build import verify_set
from project_version import read_version


def command(*args):
    subprocess.run(['gh', *map(str, args)], check=True)


def api(endpoint, optional=False, paginate=False):
    args = ['gh', 'api', endpoint]
    if paginate:
        args += ['--paginate', '--slurp']
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode:
        try:
            error = json.loads(result.stdout)
        except json.JSONDecodeError:
            error = {}
        if optional and str(error.get('status')) == '404':
            return None
        raise RuntimeError(result.stderr.strip() or result.stdout)
    return json.loads(result.stdout)


def test_tags(releases, number):
    pattern = re.compile(rf'test-pr{number}-[0-9a-f]{{12}}')
    return [item['tag_name'] for item in releases if pattern.fullmatch(item['tag_name'])]


def releases(repository):
    pages = api(f'repos/{repository}/releases?per_page=100', paginate=True)
    return [item for page in pages for item in page]


def cleanup(repository, number, keep=None):
    for tag in test_tags(releases(repository), number):
        if tag != keep:
            command('release', 'delete', tag, '--repo', repository, '--cleanup-tag', '--yes')


def current_pr(repository, number, sha):
    pr = api(f'repos/{repository}/pulls/{number}')
    return (pr['state'] == 'open' and pr['head']['sha'] == sha
            and pr['head']['ref'] == 'main' and pr['base']['ref'] == 'stable'
            and (pr['head']['repo'] or {}).get('full_name') == repository)


def merged_pr(repository, sha):
    for pr in api(f'repos/{repository}/commits/{sha}/pulls'):
        if (pr.get('merged_at') and pr.get('merge_commit_sha') == sha
                and pr['base']['ref'] == 'stable' and pr['head']['ref'] == 'main'):
            return pr
    raise RuntimeError(f'No merged main -> stable PR has merge commit {sha}; nothing published.')


def publish(repository, kind, directory, sha, run_id, number=0):
    version = read_version()
    channel = 'test' if kind == 'test' else 'release'
    paths = verify_set(directory, version, sha, channel, run_id)
    if kind == 'test':
        if not current_pr(repository, number, sha):
            print('PR is closed, superseded or not a main -> stable promotion; no test release.')
            return
        tag = f'test-pr{number}-{sha[:12]}'
        if api(f'repos/{repository}/releases/tags/{tag}', optional=True):
            command('release', 'delete', tag, '--repo', repository, '--cleanup-tag', '--yes')
        title = f'Beach Life test build: PR #{number}'
        flags = ['--prerelease', '--latest=false']
        notes = (f'Test build of https://github.com/{repository}/pull/{number} at {sha}.\n\n'
                 'The launcher and the experimental decompiled game are still under development.\n\n'
                 'Download the package for your platform, extract it and follow docs/INSTALL.md. '
                 'No original game files are included. SHA256SUMS covers every package.\n')
    else:
        tag = 'v' + version
        # The tag endpoint omits drafts: enumerate releases before checking tags.
        if (any(item['tag_name'] == tag for item in releases(repository))
                or api(f'repos/{repository}/git/ref/tags/{tag}', optional=True)):
            print(f'{tag} already exists; preserving its commit and assets. Bump CMake for a new release.')
            return
        pr = merged_pr(repository, sha)
        title = pr['title']
        flags = ['--latest']
        notes = (pr['body'] or '').strip() + '\n'
    with tempfile.TemporaryDirectory() as temp:
        body = Path(temp) / 'notes.md'
        body.write_text(notes)
        command('release', 'create', tag, '--repo', repository, '--target', sha,
                '--title', title, '--notes-file', body, *flags, *paths)
    if kind == 'test':
        # A close/push racing publication must not leave a stale test build.
        if not current_pr(repository, number, sha):
            command('release', 'delete', tag, '--repo', repository, '--cleanup-tag', '--yes')
        else:
            cleanup(repository, number, keep=tag)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kind', choices=('test', 'stable', 'cleanup'))
    parser.add_argument('--repository', required=True)
    parser.add_argument('--packages', type=Path)
    parser.add_argument('--sha')
    parser.add_argument('--run-id')
    parser.add_argument('--pr', type=int, default=0)
    args = parser.parse_args()
    if not re.fullmatch(r'[\w.-]+/[\w.-]+', args.repository):
        parser.error('Invalid repository')
    if args.kind in ('test', 'cleanup') and args.pr <= 0:
        parser.error('A positive --pr is required')
    if args.kind == 'cleanup':
        cleanup(args.repository, args.pr)
    else:
        if not args.packages or not args.run_id or not re.fullmatch(r'[0-9a-f]{40}', args.sha or ''):
            parser.error('--packages, --run-id and a full --sha are required')
        publish(args.repository, args.kind, args.packages, args.sha, args.run_id, args.pr)
