"""Sign a macOS app or disk image for release.

With the Developer ID secrets in the environment the item is signed with that
identity and the hardened runtime, then notarized by Apple and stapled, so
Gatekeeper opens it without the Privacy & Security override. Without them
(local and fork builds) an app is signed ad hoc as before.

MACOS_CERTIFICATE           base64 of the Developer ID Application .p12
MACOS_CERTIFICATE_PASSWORD  its export password
MACOS_NOTARY_KEY            an App Store Connect API key (.p8 text)
MACOS_NOTARY_KEY_ID, MACOS_NOTARY_ISSUER  that key's ID and issuer ID
MACOS_NOTARIZE              set to notarize; otherwise only signed
"""
import argparse
import base64
import json
import os
from pathlib import Path
import secrets
import subprocess
import tempfile


def run(*args, **options):
    subprocess.run([str(arg) for arg in args], check=True, **options)


def output(*args):
    return subprocess.check_output([str(arg) for arg in args], text=True)


class Keychain:
    """A temporary keychain holding only the Developer ID certificate."""

    def __init__(self, folder, certificate, password):
        self.path = Path(folder) / 'signing.keychain-db'
        self.password = secrets.token_hex(16)
        self.previous = [line.strip().strip('"') for line in
                         output('security', 'list-keychains', '-d', 'user').splitlines() if line.strip()]
        p12 = Path(folder) / 'certificate.p12'
        p12.write_bytes(base64.b64decode(certificate))
        run('security', 'create-keychain', '-p', self.password, self.path)
        try:
            run('security', 'set-keychain-settings', '-lut', '21600', self.path)
            run('security', 'unlock-keychain', '-p', self.password, self.path)
            run('security', 'import', p12, '-k', self.path, '-P', password, '-T', '/usr/bin/codesign')
            run('security', 'set-key-partition-list', '-S', 'apple-tool:,apple:,codesign:', '-s',
                '-k', self.password, self.path, stdout=subprocess.DEVNULL)
            run('security', 'list-keychains', '-d', 'user', '-s', self.path, *self.previous)
        except BaseException:
            self.close()
            raise
        finally:
            p12.unlink()
        identities = output('security', 'find-identity', '-v', '-p', 'codesigning', self.path)
        found = [line.split()[1] for line in identities.splitlines() if 'Developer ID Application' in line]
        if not found:
            self.close()
            raise SystemExit('The certificate is not a Developer ID Application identity')
        self.identity = found[0]

    def close(self):
        subprocess.run(['security', 'list-keychains', '-d', 'user', '-s', *self.previous], check=False)
        subprocess.run(['security', 'delete-keychain', str(self.path)], check=False)


def notarize(item, folder):
    """Submits `item` (an app, zipped first, or a disk image) and staples the ticket."""
    key = os.environ.get('MACOS_NOTARY_KEY')
    if not key or not os.environ.get('MACOS_NOTARIZE'):
        print('Signed with the Developer ID; not notarized (needs MACOS_NOTARIZE and the notary key).')
        return
    key_file = Path(folder) / 'notary.p8'
    key_file.write_text(key)
    submission = item
    if item.suffix == '.app':
        submission = Path(folder) / (item.stem + '.zip')
        run('ditto', '-c', '-k', '--keepParent', item, submission)
    credentials = ['--key', key_file, '--key-id', os.environ['MACOS_NOTARY_KEY_ID'],
                   '--issuer', os.environ['MACOS_NOTARY_ISSUER']]
    result = json.loads(output('xcrun', 'notarytool', 'submit', submission, *credentials,
                               '--wait', '--timeout', '60m', '--output-format', 'json'))
    if result.get('status') != 'Accepted':
        subprocess.run(['xcrun', 'notarytool', 'log', result.get('id', ''), *map(str, credentials)])
        raise SystemExit(f"Notarization {result.get('status')}: {result.get('message', '')}")
    run('xcrun', 'stapler', 'staple', item)
    run('xcrun', 'stapler', 'validate', item)
    kind = 'execute' if item.suffix == '.app' else 'open'
    extra = [] if kind == 'execute' else ['--context', 'context:primary-signature']
    run('spctl', '--assess', '--type', kind, *extra, '--verbose=2', item)


def sign(item):
    certificate = os.environ.get('MACOS_CERTIFICATE')
    if not certificate:
        if item.suffix != '.app':
            print(f'No Developer ID certificate: {item.name} stays unsigned.')
            return
        print('No Developer ID certificate: ad-hoc signature; Gatekeeper asks the player to allow it.')
        run('codesign', '--force', '--sign', '-', item)
        run('codesign', '--verify', '--deep', '--strict', item)
        return
    with tempfile.TemporaryDirectory() as folder:
        keychain = Keychain(folder, certificate, os.environ.get('MACOS_CERTIFICATE_PASSWORD', ''))
        try:
            options = ['--options', 'runtime'] if item.suffix == '.app' else []
            run('codesign', '--force', '--timestamp', *options, '--keychain', keychain.path,
                '--sign', keychain.identity, item)
            run('codesign', '--verify', '--strict', '--verbose=2', item)
        finally:
            keychain.close()
        notarize(item, folder)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('item', type=Path, help='an .app bundle or a .dmg')
    sign(parser.parse_args().item.resolve())
