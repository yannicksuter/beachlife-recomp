import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from decomp_progress import badges, build_report, render


class ProgressTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / 'fixture.cpp'
        self.source.write_text('// FUNCTION: BEACHLIFE 0x1000\n// LIBRARY: BEACHLIFE 0x1020\n')
        self.target = {'id': 'generated-target', 'sha256': 'a' * 64,
                       'functions': [{'address': '0x1000', 'size': 16}],
                       'library_functions': [{'address': '0x1020', 'size': 8}]}
        self.comparison = {'target': self.target['id'], 'original_sha256': self.target['sha256'],
                           'input_sha256': {'fixture.cpp': self.digest()},
                           'functions': [{'address': '0x1000', 'bytes': 16,
                                          'normalized_match': 1, 'exact_bytes': True}],
                           'library_functions': [{'address': '0x1020', 'bytes': 8,
                                                  'normalized_match': 1, 'exact_bytes': False}]}
        self.inventory = {'schema_version': 1, 'target': self.target['id'],
                          'original_sha256': self.target['sha256'], 'title': 'Generated fixture',
                          'snapshot': {'date': '2026-01-01', 'revision': 'a' * 40},
                          'sections': [{'name': '.text', 'start': '0x1000', 'end_exclusive': '0x1100'}],
                          'categories': [{'id': 'images', 'name': 'Images', 'summary': 'Test group'},
                                         {'id': 'unmapped', 'name': 'Unmapped', 'summary': 'No boundaries'}],
                          'units': [{'id': 'image', 'name': 'Image helpers', 'category': 'images'}],
                          'functions': [
                              self.method('0x1000', 16, source='fixture.cpp', native_tested=True),
                              self.method('0x1020', 8, source='fixture.cpp', kind='library'),
                              self.method('0x1040', 32, candidate_match=0.99, comparison_linked=True),
                              self.method('0x1080', None)]}

    def digest(self):
        return hashlib.sha256(self.source.read_bytes()).hexdigest()

    def method(self, address, size, **changes):
        return {'address': address, 'size': size, 'name': 'Generated ' + address,
                'unit': 'image', 'kind': 'game', 'source': None, 'candidate_match': None,
                'comparison_linked': False, 'native_tested': False, 'runtime_linked': False,
                'note': 'Missing dependency', **changes}

    def report(self):
        return build_report(self.inventory, self.comparison, self.target, self.root)

    def test_denominators_and_linkage_are_separate(self):
        report = self.report()
        metrics = report['metrics']
        self.assertEqual(metrics['matched_bytes'], 16)
        self.assertEqual(metrics['library_matched_bytes'], 8)
        self.assertEqual(metrics['unclassified_matched_methods'], 0)
        self.assertEqual(metrics['unclassified_matched_bytes'], 0)
        self.assertEqual(metrics['game_bytes'], 48)
        self.assertEqual(metrics['mapped_bytes'], 56)
        self.assertEqual(metrics['unknown_bytes'], 200)
        self.assertEqual(metrics['bounded_methods'], 3)
        self.assertEqual(metrics['comparison_linked_matches'], 1)
        self.assertEqual(metrics['native_tested_matches'], 1)
        self.assertEqual(metrics['runtime_bytes'], 0)
        self.assertEqual(sum(g['end'] - g['start'] for g in report['gaps']), 200)
        self.inventory['functions'][0]['runtime_linked'] = True
        self.assertEqual(self.report()['metrics']['runtime_bytes'], 16)

    def test_badges_floor_matching_and_report_decompiled_share(self):
        documents = badges(self.report()['metrics'])
        self.assertEqual(documents, {'matched.json': {'schemaVersion': 1, 'label': 'byte-matched',
                                                      'message': '33.3%', 'color': 'yellow'}})
        for item, boot in zip(self.inventory['functions'], ('in_tree', None, 'in_tree', 'trap')):
            item['boot'] = boot
        documents = badges(self.report()['metrics'])
        self.assertEqual(documents['decompiled.json']['message'], '100%')
        self.inventory['functions'][2]['boot'] = 'trap'
        self.assertEqual(badges(self.report()['metrics'])['decompiled.json']['message'], '33.3%')

    def test_partial_comparison_is_never_completed(self):
        self.comparison['functions'][0].update(normalized_match=0.9999999, exact_bytes=False)
        report = self.report()
        self.assertEqual(report['metrics']['matched_methods'], 0)
        self.assertEqual(report['functions'][0]['status'], 'candidate')
        self.assertEqual(report['metrics']['library_matched_bytes'], 8)
        self.assertIn('&lt;100%', render(report))

    def test_reject_stale_or_missing_source_evidence(self):
        self.source.write_text('changed source\n')
        with self.assertRaisesRegex(ValueError, 'Stale comparison'):
            self.report()
        self.comparison['input_sha256']['fixture.cpp'] = self.digest()
        with self.assertRaisesRegex(ValueError, 'address annotation'):
            self.report()
        self.comparison['input_sha256'] = {}
        with self.assertRaisesRegex(ValueError, 'no input fingerprints'):
            self.report()

    def test_reject_incomplete_or_misassigned_comparison(self):
        self.inventory['functions'][0]['size'] = 15
        with self.assertRaisesRegex(ValueError, 'inconsistent'):
            self.report()
        self.inventory['functions'][0]['size'] = 16
        self.comparison['functions'] = []
        with self.assertRaisesRegex(ValueError, 'registered target inventory'):
            self.report()

    def test_fingerprinted_csv_can_bind_an_unannotated_source(self):
        self.source.write_text('// LIBRARY: BEACHLIFE 0x1020\nvoid Generated() {}\n')
        self.comparison['input_sha256']['fixture.cpp'] = self.digest()
        metadata = self.root / 'metadata.csv'
        metadata.write_text('address,type,size,symbol\n0x1000,function,16,?Generated@@YAXXZ\n')
        self.comparison['input_sha256']['metadata.csv'] = hashlib.sha256(metadata.read_bytes()).hexdigest()
        self.inventory['functions'][0]['source_metadata'] = {
            'path': 'metadata.csv', 'symbol': '?Generated@@YAXXZ'}
        self.assertEqual(self.report()['metrics']['matched_bytes'], 16)
        del self.comparison['input_sha256']['metadata.csv']
        with self.assertRaisesRegex(ValueError, 'unfingerprinted source metadata'):
            self.report()

    def test_csv_binding_rejects_wrong_extent_symbol_kind_or_duplicate_address(self):
        self.source.write_text('// LIBRARY: BEACHLIFE 0x1020\nvoid Generated() {}\n')
        self.comparison['input_sha256']['fixture.cpp'] = self.digest()
        self.inventory['functions'][0]['source_metadata'] = {
            'path': 'metadata.csv', 'symbol': '?Generated@@YAXXZ'}
        metadata = self.root / 'metadata.csv'
        for rows in ['0x1000,function,15,?Generated@@YAXXZ\n',
                     '0x1000,function,16,?Different@@YAXXZ\n',
                     '0x1000,library,16,?Generated@@YAXXZ\n',
                     '0x1000,function,16,?Generated@@YAXXZ\n' * 2]:
            with self.subTest(rows=rows):
                metadata.write_text('address,type,size,symbol\n' + rows)
                self.comparison['input_sha256']['metadata.csv'] = hashlib.sha256(metadata.read_bytes()).hexdigest()
                with self.assertRaisesRegex(ValueError, 'metadata binding'):
                    self.report()

    def test_reject_overlaps_alias_duplicates_and_unknown_extents(self):
        for extra, pattern in [
                (self.method('0x1008', 32), 'Overlapping function'),
                (self.method('0x1008', None), 'inside an already bounded'),
                (self.method('0x00001000', 16), 'Duplicate'),
                (self.method('0x1100', None), 'outside executable'),
                (self.method('0x1090', -1), 'positive integer')]:
            with self.subTest(extra=extra):
                self.inventory['functions'].append(extra)
                with self.assertRaisesRegex(ValueError, pattern):
                    self.report()
                self.inventory['functions'].pop()
        for field in ['functions', 'library_functions']:
            duplicate = {**self.comparison[field][0], 'address': '0x0000' + self.comparison[field][0]['address'][2:]}
            self.comparison[field].append(duplicate)
            with self.assertRaisesRegex(ValueError, 'Duplicate'):
                self.report()
            self.comparison[field].pop()

    def test_reject_false_completion_and_invalid_scores(self):
        for value in [1, float('nan'), float('inf'), -0.1, True]:
            self.inventory['functions'][2]['candidate_match'] = value
            with self.assertRaisesRegex(ValueError, 'Candidate score'):
                self.report()
        self.inventory['functions'][2]['candidate_match'] = None
        self.comparison['functions'][0]['normalized_match'] = 0.9
        with self.assertRaisesRegex(ValueError, 'Raw-exact'):
            self.report()
        self.comparison['functions'][0]['normalized_match'] = float('nan')
        with self.assertRaisesRegex(ValueError, 'Invalid comparison score'):
            self.report()

    def test_reject_identity_and_source_escape(self):
        self.inventory['target'] = 'different'
        with self.assertRaisesRegex(ValueError, 'different original targets'):
            self.report()
        self.inventory['target'] = self.target['id']
        self.comparison['input_sha256']['../outside.cpp'] = 'a' * 64
        with self.assertRaisesRegex(ValueError, 'outside project or missing'):
            self.report()

    def test_escape_content_and_render_offline_without_scripts_needed(self):
        self.inventory['title'] = '<script>alert("x")</script>'
        self.inventory['functions'][0]['name'] = '<img src=x onerror=alert(1)>'
        document = render(self.report())
        self.assertIn('&lt;script&gt;', document)
        self.assertIn('&lt;img src=x onerror=alert(1)&gt;', document)
        self.assertNotIn('<img src=x', document)
        self.assertIn('default-src \'none\'', document)
        self.assertNotIn('<script src=', document)
        self.assertNotIn('{{', document)
        self.assertIn('33.33%', document)
        self.assertIn('0.00%', document)
        self.assertIn('Unmapped', document)
        self.assertIn('Boundary open', document)
        self.assertEqual(self.inventory['functions'][0]['comparison_linked'], False)

    def test_literal_template_tokens_stay_literal(self):
        self.inventory['title'] = '{{DATE}}'
        self.inventory['functions'][0]['name'] = '{{MATCH_DETAIL}}'
        self.inventory['native_note'] = '{{UNKNOWN_FIELD}}'
        document = render(self.report())
        self.assertIn('<title>{{DATE}} · reconstruction progress</title>', document)
        self.assertIn('<strong>{{MATCH_DETAIL}}</strong>', document)
        self.assertIn('{{UNKNOWN_FIELD}}', document)

    def test_cli_preserves_utf8_in_a_non_utf8_locale(self):
        self.inventory['title'] = 'Beach Life · été'
        arguments = []
        for flag, data in [('splits', self.inventory), ('comparison', self.comparison),
                           ('target', self.target)]:
            path = self.root / (flag + '.json')
            path.write_text(json.dumps(data, ensure_ascii=False), encoding='utf-8')
            arguments.extend(['--' + flag, str(path)])
        output = self.root / 'index.html'
        script = Path(__file__).resolve().parents[1] / 'tools/decomp_progress.py'
        environment = {**os.environ, 'LC_ALL': 'C', 'PYTHONUTF8': '0', 'PYTHONCOERCECLOCALE': '0'}
        subprocess.run([sys.executable, str(script), *arguments, '--source', str(self.root),
                        '--output', str(output)], env=environment, check=True, capture_output=True)
        self.assertIn('Beach Life · été', output.read_text(encoding='utf-8'))

    def test_cli_uses_fingerprinted_prepared_target(self):
        target = self.root / 'prepared/targets/fixture.json'
        target.parent.mkdir(parents=True)
        target.write_text(json.dumps(self.target), encoding='utf-8')
        name = target.relative_to(self.root).as_posix()
        self.comparison['target_config'] = name
        self.comparison['input_sha256'][name] = hashlib.sha256(target.read_bytes()).hexdigest()
        self.assertEqual(self.report()['metrics']['matched_methods'], 1)
        script = Path(__file__).resolve().parents[1] / 'tools/decomp_progress.py'
        arguments = []
        for flag, data in [('splits', self.inventory), ('comparison', self.comparison)]:
            path = self.root / (flag + '.json')
            path.write_text(json.dumps(data), encoding='utf-8')
            arguments.extend(['--' + flag, str(path)])
        output = self.root / 'index.html'
        subprocess.run([sys.executable, str(script), *arguments, '--source', str(self.root),
                        '--output', str(output)], check=True, capture_output=True)
        self.assertTrue(output.is_file())
        self.target['functions'] = []
        with self.assertRaisesRegex(ValueError, 'differs from the compared export'):
            self.report()
        del self.comparison['input_sha256'][name]
        with self.assertRaisesRegex(ValueError, 'no fingerprint'):
            self.report()

    def add_unclassified_fixture(self):
        source = self.root / 'unclassified.cpp'
        source.write_text('// FUNCTION: BEACHLIFE 0x10a0\n')
        self.comparison['input_sha256']['unclassified.cpp'] = hashlib.sha256(source.read_bytes()).hexdigest()
        self.target['unclassified_functions'] = [{'address': '0x10a0', 'size': 12}]
        self.comparison['unclassified_functions'] = [
            {'address': '0x10a0', 'bytes': 12, 'normalized_match': 1, 'exact_bytes': True}]
        self.inventory['functions'].append(self.method('0x10a0', 12, kind='unclassified',
                                                       source='unclassified.cpp', native_tested=True,
                                                       runtime_linked=True))
        return source

    def test_unclassified_match_has_separate_totals_and_visible_raw_status(self):
        self.add_unclassified_fixture()
        report = self.report()
        metrics = report['metrics']
        for field, value in [('matched_methods', 1), ('matched_bytes', 16),
                             ('raw_methods', 1), ('raw_bytes', 16), ('game_bytes', 48),
                             ('library_matched_bytes', 8), ('runtime_methods', 0),
                             ('runtime_bytes', 0), ('native_tested_matches', 1),
                             ('comparison_linked_matches', 1), ('mapped_bytes', 68),
                             ('unknown_bytes', 188), ('unclassified_matched_methods', 1),
                             ('unclassified_matched_bytes', 12)]:
            self.assertEqual(metrics[field], value, field)
        row = report['functions'][-1]
        self.assertEqual(row['kind'], 'unclassified')
        self.assertEqual(row['status'], 'matched')
        self.assertTrue(row['registered'])
        self.assertTrue(row['raw_exact'])
        self.assertTrue(row['comparison_linked'])
        self.assertTrue(row['native_tested'])
        self.assertTrue(row['runtime_linked'])
        document = render(report)
        self.assertIn('Unclassified code <b>1 matched methods · 12 bytes</b>', document)
        self.assertIn('<span class="kind">unclassified</span>', document)
        self.assertEqual(document.count('Raw bytes exact'), 2)
        self.assertIn('16 / 48 bounded game bytes', document)

    def test_partial_unclassified_result_is_visible_but_adds_no_completed_bytes(self):
        self.add_unclassified_fixture()
        self.comparison['unclassified_functions'][0].update(normalized_match=0.9999999, exact_bytes=False)
        report = self.report()
        self.assertEqual(report['metrics']['unclassified_matched_methods'], 0)
        self.assertEqual(report['metrics']['unclassified_matched_bytes'], 0)
        self.assertEqual(report['metrics']['matched_bytes'], 16)
        self.assertEqual(report['functions'][-1]['status'], 'candidate')
        self.assertIn('&lt;100%', render(report))

    def test_unclassified_target_result_and_split_classifications_must_agree(self):
        self.add_unclassified_fixture()
        self.inventory['functions'][-1]['kind'] = 'game'
        with self.assertRaisesRegex(ValueError, 'inconsistent'):
            self.report()
        self.inventory['functions'][-1]['kind'] = 'unclassified'
        row = self.comparison['unclassified_functions'].pop()
        with self.assertRaisesRegex(ValueError, 'registered target inventory'):
            self.report()
        self.comparison['library_functions'].append(row)
        with self.assertRaisesRegex(ValueError, 'registered target inventory'):
            self.report()
        self.comparison['library_functions'].pop()
        self.comparison['unclassified_functions'].append(row)
        del self.target['unclassified_functions']
        with self.assertRaisesRegex(ValueError, 'registered target inventory'):
            self.report()

    def test_cross_group_address_duplicates_are_rejected_in_target_and_result(self):
        self.add_unclassified_fixture()
        for data in [self.target, self.comparison]:
            for other in ['functions', 'library_functions']:
                duplicate = data[other][0] | {'address': '0x0000' + data[other][0]['address'][2:]}
                data['unclassified_functions'].append(duplicate)
                with self.assertRaisesRegex(ValueError, 'Duplicate function address'):
                    self.report()
                data['unclassified_functions'].pop()

    def test_unclassified_records_retain_shape_extent_and_score_validation(self):
        self.add_unclassified_fixture()
        for data in [self.target, self.comparison]:
            saved = data['unclassified_functions']
            for malformed in [None, {}, True, 'function', [None], [[]]]:
                data['unclassified_functions'] = malformed
                with self.assertRaisesRegex(ValueError, 'lists of records'):
                    self.report()
            data['unclassified_functions'] = saved
            field = 'size' if data is self.target else 'bytes'
            for size in [True, 12.0, '12', 0, -1, 13]:
                data['unclassified_functions'][0][field] = size
                with self.assertRaisesRegex(ValueError, 'inconsistent'):
                    self.report()
            data['unclassified_functions'][0][field] = 12
        row = self.comparison['unclassified_functions'][0]
        for value in [True, float('nan'), float('inf'), -0.1, 1.1]:
            row['normalized_match'] = value
            with self.assertRaisesRegex(ValueError, 'Invalid comparison score'):
                self.report()
        row['normalized_match'] = 0.9
        with self.assertRaisesRegex(ValueError, 'Raw-exact'):
            self.report()

    def test_unclassified_source_requires_current_fingerprint_and_function_annotation(self):
        source = self.add_unclassified_fixture()
        source.write_text('// LIBRARY: BEACHLIFE 0x10a0\n')
        with self.assertRaisesRegex(ValueError, 'Stale comparison input'):
            self.report()
        self.comparison['input_sha256']['unclassified.cpp'] = hashlib.sha256(source.read_bytes()).hexdigest()
        with self.assertRaisesRegex(ValueError, 'address annotation'):
            self.report()
        del self.comparison['input_sha256']['unclassified.cpp']
        with self.assertRaisesRegex(ValueError, 'source is not covered'):
            self.report()


if __name__ == '__main__':
    unittest.main()
