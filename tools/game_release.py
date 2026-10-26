"""The release game must resolve every reconstructed function and host service."""

TRAP_KINDS = ('functions', 'host_services', 'unresolved')
GAME_PROFILES = ('native-game', 'native-game-tweaks')


def trap_counts(report):
    if not isinstance(report, dict) or set(report) != set(TRAP_KINDS) \
            or any(not isinstance(report[name], list) for name in TRAP_KINDS):
        raise ValueError('Invalid generated game trap list')
    return {name: len(report[name]) for name in TRAP_KINDS}


def validate_counts(counts, *, complete=False):
    if not isinstance(counts, dict) or set(counts) != set(TRAP_KINDS) \
            or any(type(value) is not int or value < 0 for value in counts.values()):
        raise ValueError('Missing or invalid game trap counts')
    if complete and any(counts.values()):
        raise ValueError('Release game requires zero game traps, host-service traps and unresolved symbols')


def require_release_game(profile, counts):
    if profile != 'native-game-tweaks':
        raise ValueError('Release packages require the decompiled game and gameplay options')
    validate_counts(counts, complete=True)
