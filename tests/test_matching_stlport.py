"""Synthetic external-header inputs; no compiler, vendor code or retail bytes."""
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import test_matching

matching = test_matching.matching


class MatchingStlportTests(unittest.TestCase):
    def game_case(self):
        case = test_matching.MatchingCompilerOptionsTests()
        case.setUp()
        self.addCleanup(case.doCleanups)
        return case

    def temporary_path(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        return Path(directory.name)

    def test_stlport_identity_and_result_fingerprints(self):
        case = self.game_case()
        include, headers = matching.verify_stlport(case.args.stlport)
        self.assertEqual(include, case.stlport_header.parent)
        self.assertEqual(headers, {case.stlport_header: matching.digest(case.stlport_header)})
        case.target['stlport_sources'] = ['helper.cpp']
        result = case.run_game()
        for path in (case.stlport_header, case.stlport_pin):
            self.assertEqual(result['input_sha256'][case.fingerprint_key(path)],
                             matching.digest(path))
        self.assertEqual(result['stlport_root'], str(case.args.stlport))
        self.assertEqual(result['stlport_sources'], ['helper.cpp'])
        self.assertEqual(case.source_includes, {case.source / 'helper.cpp': [include]})
        flags = case.build_arguments[2][case.source / 'helper.cpp']
        self.assertEqual(flags, ['/D_STLP_NO_OWN_IOSTREAMS=1'])
        self.assertNotIn('/GX', case.build_arguments[1])
        self.assertNotIn('/Ob1', case.build_arguments[1])

    def test_stlport_rejects_changed_tree_before_build(self):
        for change in ['bytes', 'missing', 'extra', 'rename', 'symlink']:
            with self.subTest(change=change):
                case = self.game_case()
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
                with self.assertRaises(ValueError):
                    case.run_game()
                self.assertEqual(case.calls, [])

    def test_stlport_missing_root_has_setup_error(self):
        case = self.game_case()
        case.args.stlport = case.root / 'missing'
        with self.assertRaisesRegex(ValueError, '--stlport or BEACHLIFE_STLPORT'):
            case.run_game()
        self.assertEqual(case.calls, [])

    def test_stlport_changed_during_run_cannot_publish_result(self):
        for phase in ['build', 'compare']:
            for change in ['bytes', 'extra', 'pin']:
                with self.subTest(phase=phase, change=change):
                    self.check_changed_during_run(phase, change)

    def check_changed_during_run(self, phase, change):
        case = self.game_case()
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
            with self.assertRaises(ValueError):
                case.run_game()
        self.assertFalse((case.args.reports / 'result.json').exists())

    def test_stlport_cli_selection(self):
        selections = [
            ([], {}, matching.STLPORT_DEFAULT),
            ([], {'BEACHLIFE_STLPORT': '/environment/vendor'}, Path('/environment/vendor')),
            (['--stlport', '/explicit/vendor'], {'BEACHLIFE_STLPORT': '/environment/vendor'},
             Path('/explicit/vendor')),
        ]
        for arguments, environment, expected in selections:
            with self.subTest(arguments=arguments, environment=environment):
                root = self.temporary_path()

                def prepared(args):
                    args.decomp_root = root
                with patch.dict(os.environ, environment, clear=True), \
                        patch('sys.argv', ['matching.py', 'game', *arguments]), \
                        patch.object(matching, 'prepare_game_source', side_effect=prepared), \
                        patch.object(matching, 'game_target'), \
                        patch.object(matching, 'Toolchain'), \
                        patch.object(matching, 'game') as game:
                    matching.main()
                self.assertEqual(game.call_args.args[1].stlport, expected)

    def test_stlport_is_not_required_for_toolchain_fixtures(self):
        with patch('sys.argv', ['matching.py', 'fixture']), \
                patch.object(matching, 'Toolchain'), \
                patch.object(matching, 'fixture') as fixture, \
                patch.object(matching, 'verify_stlport') as verify:
            matching.main()
        fixture.assert_called_once()
        verify.assert_not_called()

    def test_stlport_opt_in_preserves_other_translation_units(self):
        case = self.game_case()
        case.run_game()
        self.assertIsNone(case.source_includes)
        self.assertEqual(case.build_arguments[2],
                         {case.metadata / 'support/operator_delete.cpp': ['/O1']})
        case.target['stlport_sources'] = ['helper.cpp', 'game.cpp']
        case.target['source_compile_options'] = {'helper.cpp': ['/GX', '/Ob1']}
        case.run_game()
        flags = case.build_arguments[2]
        self.assertEqual(flags[case.source / 'helper.cpp'],
                         ['/GX', '/Ob1', '/D_STLP_NO_OWN_IOSTREAMS=1'])
        self.assertEqual(flags[case.metadata / 'fixtures/game.cpp'],
                         ['/D_STLP_NO_OWN_IOSTREAMS=1'])

    def test_stlport_rejects_invalid_source_selections(self):
        for configured in [True, {}, ['../helper.cpp'], ['missing.cpp'],
                           ['helper.cpp', 'helper.cpp'], [1]]:
            with self.subTest(configured=configured):
                case = self.game_case()
                case.target['stlport_sources'] = configured
                with self.assertRaisesRegex(ValueError, 'STLport|stlport_sources'):
                    case.run_game()
                self.assertEqual(case.calls, [])

    def test_stlport_includes_precede_sdk_and_keep_per_source_options(self):
        root = self.temporary_path()
        sources = [root / 'existing.cpp', root / 'vendor.cpp']
        tool = matching.Toolchain.__new__(matching.Toolchain)
        tool.compiler = root
        with patch.object(tool, 'native', side_effect=str), \
                patch.object(tool, 'run', return_value=''), \
                patch.object(tool, 'response') as response:
            tool.build(root, sources, 'fixture', extra_compile=['/MD', '/G6', '/Ivc6'],
                       source_flags={sources[1]: ['/GX', '/Ob1', '/D_STLP_NO_OWN_IOSTREAMS=1']},
                       source_includes={sources[1]: [root / 'vendor/stlport']})
        existing, vendor, _ = [call.args[1] for call in response.call_args_list]
        include = '/I' + str(root / 'vendor/stlport')
        self.assertNotIn(include, existing)
        self.assertNotIn('/D_STLP_NO_OWN_IOSTREAMS=1', existing)
        self.assertLess(vendor.index(include), vendor.index('/Ivc6'))
        self.assertLess(vendor.index('/Ob0'), vendor.index('/Ob1'))
        self.assertIn('/GX', vendor)
        self.assertNotIn('/GX', existing)

    def test_stlport_sources_merge_from_registration_fragments(self):
        case = self.game_case()
        fragments = case.metadata / 'targets/beachlife-uk-1.1.d'
        fragments.mkdir()
        (fragments / '00401000.json').write_text(json.dumps({'stlport_sources': ['helper.cpp']}))
        self.assertEqual(case.run_game()['stlport_sources'], ['helper.cpp'])
        (fragments / '00402000.json').write_text(json.dumps({'stlport_sources': ['helper.cpp']}))
        case.calls.clear()
        with self.assertRaisesRegex(ValueError, 'unique source basenames'):
            case.run_game()
        self.assertEqual(case.calls, [])


if __name__ == '__main__':
    unittest.main()
