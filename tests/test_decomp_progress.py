import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from decomp_progress import build_report, render


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


if __name__ == '__main__':
    unittest.main()
