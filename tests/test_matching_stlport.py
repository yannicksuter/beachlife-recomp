"""Synthetic external-header inputs; no compiler, vendor code or retail bytes."""
import json
import os
from pathlib import Path
from unittest.mock import patch

import pytest
import test_matching

matching = test_matching.matching


@pytest.fixture
def game_case():
    case = test_matching.MatchingCompilerOptionsTests()
    case.setUp()
    try:
        yield case
    finally:
        case.doCleanups()


def test_stlport_identity_and_result_fingerprints(game_case):
    case = game_case
    include, headers = matching.verify_stlport(case.args.stlport)
    assert include == case.stlport_header.parent
    assert headers == {case.stlport_header: matching.digest(case.stlport_header)}
    case.target['stlport_sources'] = ['helper.cpp']
    result = case.run_game()
    for path in (case.stlport_header, case.stlport_pin):
        assert result['input_sha256'][case.fingerprint_key(path)] == matching.digest(path)
    assert result['stlport_root'] == str(case.args.stlport)
    assert result['stlport_sources'] == ['helper.cpp']
    assert case.source_includes == {case.source / 'helper.cpp': [include]}
    flags = case.build_arguments[2][case.source / 'helper.cpp']
    assert flags == ['/D_STLP_NO_OWN_IOSTREAMS=1']
    assert '/GX' not in case.build_arguments[1] and '/Ob1' not in case.build_arguments[1]


@pytest.mark.parametrize('change', ['bytes', 'missing', 'extra', 'rename', 'symlink'])
def test_stlport_rejects_changed_tree_before_build(game_case, change):
    case = game_case
    header = case.stlport_header
    if change == 'bytes':
        header.write_text('// altered input\n')
    elif change == 'missing':
        header.unlink()
    elif change == 'extra':
        (header.parent / 'extra').write_text('// added input\n')
    elif change == 'rename':
        header.rename(header.parent / 'list')
    else:
        other = case.root / 'outside'
        header.rename(other)
        header.symlink_to(other)
    with pytest.raises(ValueError):
        case.run_game()
    assert case.calls == []


def test_stlport_missing_root_has_setup_error(game_case):
    case = game_case
    case.args.stlport = case.root / 'missing'
    with pytest.raises(ValueError, match='--stlport or BEACHLIFE_STLPORT'):
        case.run_game()
    assert case.calls == []


@pytest.mark.parametrize('phase', ['build', 'compare'])
@pytest.mark.parametrize('change', ['bytes', 'extra', 'pin'])
def test_stlport_changed_during_run_cannot_publish_result(game_case, phase, change):
    case = game_case
    original = case.tool.build if phase == 'build' else case.tool.run

    def tamper(*args, **kwargs):
        result = original(*args, **kwargs)
        if change == 'bytes':
            case.stlport_header.write_text('// changed during run\n')
        elif change == 'extra':
            (case.stlport_header.parent / 'extra').write_text('// newly shadowed include\n')
        else:
            case.stlport_pin.write_text(case.stlport_pin.read_text() + ' ')
        return result

    with patch.object(case.tool, 'build' if phase == 'build' else 'run', side_effect=tamper):
        with pytest.raises(ValueError):
            case.run_game()
    assert not (case.args.reports / 'result.json').exists()


@pytest.mark.parametrize('arguments,environment,expected', [
    ([], {}, matching.STLPORT_DEFAULT),
    ([], {'BEACHLIFE_STLPORT': '/environment/vendor'}, Path('/environment/vendor')),
    (['--stlport', '/explicit/vendor'], {'BEACHLIFE_STLPORT': '/environment/vendor'},
     Path('/explicit/vendor')),
])
def test_stlport_cli_selection(arguments, environment, expected, tmp_path):
    def prepared(args):
        args.decomp_root = tmp_path
    with patch.dict(os.environ, environment, clear=True), \
            patch('sys.argv', ['matching.py', 'game', *arguments]), \
            patch.object(matching, 'prepare_game_source', side_effect=prepared), \
            patch.object(matching, 'game_target'), \
            patch.object(matching, 'Toolchain'), \
            patch.object(matching, 'game') as game:
        matching.main()
    assert game.call_args.args[1].stlport == expected


def test_stlport_is_not_required_for_toolchain_fixtures():
    with patch('sys.argv', ['matching.py', 'fixture']), \
            patch.object(matching, 'Toolchain'), \
            patch.object(matching, 'fixture') as fixture, \
            patch.object(matching, 'verify_stlport') as verify:
        matching.main()
    fixture.assert_called_once()
    verify.assert_not_called()


def test_stlport_opt_in_preserves_other_translation_units(game_case):
    case = game_case
    case.run_game()
    assert case.source_includes is None
    assert case.build_arguments[2] == {case.metadata / 'support/operator_delete.cpp': ['/O1']}
    case.target['stlport_sources'] = ['helper.cpp', 'game.cpp']
    case.target['source_compile_options'] = {'helper.cpp': ['/GX', '/Ob1']}
    case.run_game()
    flags = case.build_arguments[2]
    assert flags[case.source / 'helper.cpp'] == ['/GX', '/Ob1', '/D_STLP_NO_OWN_IOSTREAMS=1']
    assert flags[case.metadata / 'fixtures/game.cpp'] == ['/D_STLP_NO_OWN_IOSTREAMS=1']


@pytest.mark.parametrize('configured', [True, {}, ['../helper.cpp'], ['missing.cpp'],
                                        ['helper.cpp', 'helper.cpp'], [1]])
def test_stlport_rejects_invalid_source_selections(game_case, configured):
    case = game_case
    case.target['stlport_sources'] = configured
    with pytest.raises(ValueError, match='STLport|stlport_sources'):
        case.run_game()
    assert case.calls == []


def test_stlport_includes_precede_sdk_and_keep_per_source_options(tmp_path):
    sources = [tmp_path / 'existing.cpp', tmp_path / 'vendor.cpp']
    tool = matching.Toolchain.__new__(matching.Toolchain)
    tool.compiler = tmp_path
    with patch.object(tool, 'native', side_effect=str), \
            patch.object(tool, 'run', return_value=''), \
            patch.object(tool, 'response') as response:
        tool.build(tmp_path, sources, 'fixture', extra_compile=['/MD', '/G6', '/Ivc6'],
                   source_flags={sources[1]: ['/GX', '/Ob1', '/D_STLP_NO_OWN_IOSTREAMS=1']},
                   source_includes={sources[1]: [tmp_path / 'vendor/stlport']})
    existing, vendor, _ = [call.args[1] for call in response.call_args_list]
    include = '/I' + str(tmp_path / 'vendor/stlport')
    assert include not in existing and '/D_STLP_NO_OWN_IOSTREAMS=1' not in existing
    assert vendor.index(include) < vendor.index('/Ivc6')
    assert vendor.index('/Ob0') < vendor.index('/Ob1')
    assert '/GX' in vendor and '/GX' not in existing


def test_stlport_sources_merge_from_registration_fragments(game_case):
    case = game_case
    fragments = case.metadata / 'targets/beachlife-uk-1.1.d'
    fragments.mkdir()
    (fragments / '00401000.json').write_text(json.dumps({'stlport_sources': ['helper.cpp']}))
    assert case.run_game()['stlport_sources'] == ['helper.cpp']
    (fragments / '00402000.json').write_text(json.dumps({'stlport_sources': ['helper.cpp']}))
    case.calls.clear()
    with pytest.raises(ValueError, match='unique source basenames'):
        case.run_game()
    assert case.calls == []
