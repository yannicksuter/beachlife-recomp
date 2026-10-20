"""Build a standalone, offline progress page from an audited split inventory."""
import argparse
import csv
import hashlib
import html
import json
import math
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
TEMPLATE = Path(__file__).with_suffix('.html')


def address(value):
    if not isinstance(value, str) or not re.fullmatch(r'0x[0-9a-fA-F]{1,8}', value):
        raise ValueError('Addresses must be hexadecimal strings in the 32-bit image.')
    return int(value, 16)


def unique(items, key):
    result = {}
    for item in items:
        value = item[key]
        if value in result:
            raise ValueError(f'Duplicate {key}: {value}')
        result[value] = item
    return result


def percentage(numerator, denominator):
    return 100 * numerator / denominator if denominator else None


def by_address(items):
    if not isinstance(items, list) or any(not isinstance(item, dict) for item in items):
        raise ValueError('Matching function groups must contain lists of records.')
    return unique([{**item, 'address_int': address(item['address'])} for item in items], 'address_int')


def percent(value):
    return '—' if value is None else f'{value:.2f}%'


def escaped(value):
    return html.escape(str(value), quote=True)


def has_metadata_binding(item, comparison, root):
    """Validate an audited source association to a fingerprinted reccmp CSV row."""
    binding = item.get('source_metadata')
    if binding is None:
        return False
    if (not isinstance(binding, dict) or set(binding) != {'path', 'symbol'} or
            not all(isinstance(value, str) and value for value in binding.values()) or
            binding['path'] not in comparison['input_sha256']):
        raise ValueError('Invalid or unfingerprinted source metadata binding.')
    with (root / binding['path']).open(newline='', encoding='utf-8') as stream:
        reader = csv.DictReader(stream)
        if not {'address', 'type', 'size', 'symbol'}.issubset(reader.fieldnames or []):
            raise ValueError('Source metadata CSV is missing required columns.')
        rows = [row for row in reader if address(row['address']) == item['address_int']]
    return (len(rows) == 1 and rows[0]['symbol'] == binding['symbol'] and
            rows[0]['type'] == ('library' if item['kind'] == 'library' else 'function') and
            int(rows[0]['size']) == item['size'])


def build_report(inventory, comparison, target, root):
    """Validate identity, extents and current evidence before counting coverage."""
    root = Path(root).resolve()
    if inventory.get('schema_version') != 1:
        raise ValueError('Unsupported split inventory schema.')
    for data in [inventory, comparison]:
        if data['target'] != target['id'] or data['original_sha256'] != target['sha256']:
            raise ValueError('Progress inputs describe different original targets.')
    if not comparison.get('input_sha256'):
        raise ValueError('Comparison has no input fingerprints; rerun tools/matching.py game.')
    for name, expected in comparison['input_sha256'].items():
        path = (root / name).resolve()
        if not path.is_relative_to(root) or not path.is_file():
            raise ValueError(f'Comparison input outside project or missing: {name}')
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise ValueError(f'Stale comparison input: {name}; rerun matching.')
    if 'target_config' in comparison:
        name = comparison['target_config']
        if name not in comparison['input_sha256']:
            raise ValueError('Compared target configuration has no fingerprint.')
        if json.loads((root / name).read_text(encoding='utf-8')) != target:
            raise ValueError('Target configuration differs from the compared export.')
    sections = []
    for section in inventory['sections']:
        start, end = address(section['start']), address(section['end_exclusive'])
        if start >= end:
            raise ValueError('Section range must be nonempty.')
        sections.append({**section, 'start_int': start, 'end_int': end})
    sections.sort(key=lambda s: s['start_int'])
    for left, right in zip(sections, sections[1:]):
        if left['end_int'] > right['start_int']:
            raise ValueError('Overlapping executable sections.')
    if not sections:
        raise ValueError('No executable section denominator.')
    categories = unique(inventory['categories'], 'id')
    units = unique(inventory['units'], 'id')
    for item in [*categories.values(), *units.values()]:
        if not re.fullmatch(r'[a-z0-9_-]+', item['id']):
            raise ValueError('Category/unit IDs must be plain identifiers.')
    if any(unit['category'] not in categories for unit in units.values()):
        raise ValueError('Unknown category in unit inventory.')
    functions = []
    for record in inventory['functions']:
        start = address(record['address'])
        size = record['size']
        if size is not None and (type(size) is not int or size <= 0):
            raise ValueError('A bounded function needs a positive integer size.')
        if not any(s['start_int'] <= start and start + (size or 1) <= s['end_int'] for s in sections):
            raise ValueError(f'Function is outside executable sections: {record["name"]}')
        if record['unit'] not in units or record['kind'] not in ['game', 'library', 'compiler', 'unclassified']:
            raise ValueError('Unknown unit or code classification.')
        score = record.get('candidate_match')
        if score is not None and (type(score) not in [int, float] or not math.isfinite(score) or not 0 <= score < 1):
            raise ValueError('Candidate score must be finite and below strict 100%.')
        for field in ['comparison_linked', 'native_tested', 'runtime_linked']:
            if type(record[field]) is not bool:
                raise ValueError(f'{field} must be boolean.')
        if record['runtime_linked'] and not record.get('source'):
            raise ValueError('Runtime linkage needs reconstructed source.')
        if record.get('source'):
            path = (root / record['source']).resolve()
            if not path.is_relative_to(root) or not path.is_file():
                raise ValueError('Reconstructed source is missing or outside the project.')
        functions.append({**record, 'address_int': start, 'score': score,
                          'registered': False, 'raw_exact': False,
                          'category': units[record['unit']]['category'],
                          'status': 'candidate' if score is not None else 'mapped' if size else 'entry'})
    functions.sort(key=lambda f: f['address_int'])
    indexed = unique(functions, 'address_int')
    bounded = [f for f in functions if f['size'] is not None]
    for left, right in zip(bounded, bounded[1:]):
        if left['address_int'] + left['size'] > right['address_int']:
            raise ValueError('Overlapping function extents would double-count progress.')
    # Address-only observations must not create a second method inside a known body.
    for item in functions:
        if item['size'] is None and any(f['address_int'] <= item['address_int'] < f['address_int'] + f['size'] for f in bounded):
            raise ValueError('Address-only entry is inside an already bounded function.')
    groups = [('functions', 'game'), ('library_functions', 'library'),
              ('unclassified_functions', 'unclassified')]
    expected_groups, observed_groups = {}, {}
    for data, indexed_groups in [(target, expected_groups), (comparison, observed_groups)]:
        seen = set()
        for key, _ in groups:
            indexed_groups[key] = by_address(data.get(key, []))
            if seen.intersection(indexed_groups[key]):
                raise ValueError('Duplicate function address across matching groups.')
            seen.update(indexed_groups[key])
    for key, kind in groups:
        expected = expected_groups[key]
        observed = observed_groups[key]
        if expected.keys() != observed.keys():
            raise ValueError('Comparison does not cover the registered target inventory.')
        for row in observed.values():
            start = address(row['address'])
            item = indexed.get(start)
            target_item = expected[start]
            if (not item or type(row['bytes']) is not int or row['bytes'] <= 0 or
                    type(target_item['size']) is not int or
                    item['size'] != row['bytes'] or row['bytes'] != target_item['size'] or
                    item['kind'] != kind):
                raise ValueError('Comparison method is missing or inconsistent in split inventory.')
            score = row['normalized_match']
            if type(score) not in [int, float] or not math.isfinite(score) or not 0 <= score <= 1:
                raise ValueError('Invalid comparison score.')
            source = item.get('source')
            if not source or source not in comparison['input_sha256']:
                raise ValueError('Compared method source is not covered by comparison fingerprints.')
            marker = 'LIBRARY' if kind == 'library' else 'FUNCTION'
            annotations = re.findall(r'//\s*' + marker + r':\s+BEACHLIFE' +
                                     r'\s+(0x[0-9a-fA-F]+)', (root / source).read_text(encoding='utf-8'))
            if (start not in [int(a, 16) for a in annotations] and
                    not has_metadata_binding(item, comparison, root)):
                raise ValueError('Method source has no matching address annotation or metadata binding.')
            if type(row['exact_bytes']) is not bool or row['exact_bytes'] and score != 1:
                raise ValueError('Raw-exact evidence requires a strict match.')
            item.update(registered=True, score=score, raw_exact=row['exact_bytes'],
                        comparison_linked=True, status='matched' if score == 1 else 'candidate')
    game = [f for f in functions if f['kind'] == 'game']
    matched = [f for f in game if f['status'] == 'matched']
    total_bytes = sum(s['end_int'] - s['start_int'] for s in sections)
    mapped_bytes = sum(f['size'] for f in bounded)
    game_bytes = sum(f['size'] or 0 for f in game)
    matched_bytes = sum(f['size'] for f in matched)
    runtime_bytes = sum(f['size'] or 0 for f in game if f['runtime_linked'])
    gaps = []
    for section in sections:
        cursor = section['start_int']
        for f in bounded:
            if not section['start_int'] <= f['address_int'] < section['end_int']:
                continue
            if cursor < f['address_int']:
                gaps.append({'start': cursor, 'end': f['address_int']})
            cursor = f['address_int'] + f['size']
        if cursor < section['end_int']:
            gaps.append({'start': cursor, 'end': section['end_int']})
    assert sum(g['end'] - g['start'] for g in gaps) + mapped_bytes == total_bytes
    return {'inventory': inventory, 'functions': functions, 'categories': categories,
            'units': units, 'sections': sections, 'gaps': gaps,
            'metrics': {'methods': len(functions), 'bounded_methods': len(bounded),
                        'game_methods': len(game), 'game_bytes': game_bytes,
                        'matched_methods': len(matched), 'matched_bytes': matched_bytes,
                        'raw_methods': sum(f['raw_exact'] for f in matched),
                        'raw_bytes': sum(f['size'] for f in matched if f['raw_exact']),
                        'runtime_methods': sum(f['runtime_linked'] for f in game),
                        'runtime_bytes': runtime_bytes, 'executable_bytes': total_bytes,
                        'mapped_bytes': mapped_bytes, 'unknown_bytes': total_bytes - mapped_bytes,
                        'native_tested_matches': sum(f['native_tested'] for f in matched),
                        'comparison_linked_matches': sum(f['comparison_linked'] for f in matched),
                        'library_matched_bytes': sum(f['size'] for f in functions if f['kind'] == 'library' and f['status'] == 'matched'),
                        'unclassified_matched_methods': sum(f['kind'] == 'unclassified' and f['status'] == 'matched' for f in functions),
                        'unclassified_matched_bytes': sum(f['size'] for f in functions if f['kind'] == 'unclassified' and f['status'] == 'matched')}}


def render(report):
    m, inventory = report['metrics'], report['inventory']
    functions, categories, units = report['functions'], report['categories'], report['units']
    status_labels = {'matched': 'Matched', 'candidate': 'Candidate', 'mapped': 'Mapped', 'entry': 'Boundary open'}
    def meter(value, style='matched'):
        return f'<div class="meter {style}" aria-hidden="true"><span style="width:{value or 0:.5f}%"></span></div>'
    def link_cell(value):
        return '<span class="yes">Yes</span>' if value else '<span class="no">—</span>'
    cards = []
    for category in categories.values():
        members = [f for f in functions if f['category'] == category['id']]
        known = sum(f['size'] or 0 for f in members)
        exact = sum(f['size'] or 0 for f in members if f['status'] == 'matched')
        matched_count = sum(f['status'] == 'matched' for f in members)
        value = percentage(exact, known)
        count_label = f'{matched_count} / {len(members)} known methods matched' if members else 'No methods inventoried yet'
        cards.append(f'<button class="category-card" data-pick="{category["id"]}"><span class="category-name">{escaped(category["name"])}</span>'
                     f'<span class="category-score">{percent(value) if members else "Unmapped"}</span>{meter(value)}'
                     f'<span class="category-count">{count_label}</span>'
                     f'<span class="category-note">{escaped(category["summary"])}</span></button>')
    rows = []
    for f in functions:
        unit = units[f['unit']]
        source = f.get('source') or ('Uncommitted candidate' if f['status'] == 'candidate' else 'Source missing')
        score = percent(100 * f['score']) if f['score'] is not None else '—'
        if f['score'] is not None and f['score'] < 1 and score == '100.00%':
            score = '&lt;100%'
        search = escaped(' '.join([f['name'], f['address'], source, unit['name'], f['note']]).lower())
        rows.append(f'<tr data-category="{f["category"]}" data-status="{f["status"]}" data-search="{search}">'
                    f'<td><strong>{escaped(f["name"])}</strong><span class="secondary mono">{escaped(f["address"])} · {str(f["size"]) + " B" if f["size"] else "extent pending"}</span>'
                    f'<span class="method-note">{escaped(f["note"])}</span></td>'
                    f'<td>{escaped(unit["name"])}<span class="secondary source">{escaped(source)}</span><span class="kind">{escaped(f["kind"])}</span></td>'
                    f'<td><span class="badge {f["status"]}">{status_labels[f["status"]]}</span><span class="score">{score}</span>'
                    f'{"<span class=raw>Raw bytes exact</span>" if f["raw_exact"] else ""}</td>'
                    f'<td>{link_cell(f["comparison_linked"])}</td><td>{link_cell(f["native_tested"])}</td><td>{link_cell(f["runtime_linked"])}</td></tr>')
    tu_rows = []
    for source in sorted({f['source'] for f in functions if f.get('source')}):
        members = [f for f in functions if f.get('source') == source]
        bounded = sum(f['size'] or 0 for f in members)
        exact = sum(f['size'] or 0 for f in members if f['status'] == 'matched')
        statuses = {f['status'] for f in members}
        status = 'matched' if statuses == {'matched'} else 'candidate'
        category = members[0]['category']
        tu_rows.append(f'<tr data-category="{category}" data-status="{status}" data-search="{escaped(source.lower())}">'
                       f'<td><strong class="source">{escaped(source)}</strong><span class="secondary">{escaped(categories[category]["name"])}</span></td>'
                       f'<td>{len(members)} registered</td><td>{percent(percentage(exact, bounded))}</td>'
                       f'<td>{sum(f["comparison_linked"] for f in members)}/{len(members)}</td>'
                       f'<td>{sum(f["native_tested"] for f in members)}/{len(members)}</td>'
                       f'<td>{sum(f["runtime_linked"] for f in members)}/{len(members)}</td></tr>')
    section_rows = ''.join(f'<tr><td>{escaped(s["name"])}</td><td class="mono">{s["start_int"]:#010x}–{s["end_int"]:#010x}</td><td>{s["end_int"]-s["start_int"]:,} B</td></tr>' for s in report['sections'])
    gap_rows = ''.join(f'<tr><td class="mono">{g["start"]:#010x}–{g["end"]:#010x}</td><td>{g["end"]-g["start"]:,} B</td></tr>' for g in report['gaps'])
    values = {
        'TITLE': escaped(inventory.get('title', 'Decompilation progress')),
        'DATE': escaped(inventory['snapshot']['date']), 'REVISION': escaped(inventory['snapshot']['revision'][:12]),
        'MATCH_PERCENT': percent(percentage(m['matched_bytes'], m['game_bytes'])),
        'MATCH_DETAIL': f'{m["matched_bytes"]:,} / {m["game_bytes"]:,} bounded game bytes',
        'MATCH_COUNT': f'{m["matched_methods"]} verified methods',
        'RUNTIME_PERCENT': percent(percentage(m['runtime_bytes'], m['game_bytes'])),
        'RUNTIME_COUNT': f'{m["runtime_methods"]} integrated game methods',
        'RUNTIME_DETAIL': f'{m["runtime_bytes"]:,} / {m["game_bytes"]:,} bounded game bytes',
        'MAP_PERCENT': percent(percentage(m['mapped_bytes'], m['executable_bytes'])),
        'MAP_DETAIL': f'{m["mapped_bytes"]:,} / {m["executable_bytes"]:,} executable bytes',
        'NATIVE_COUNT': f'{m["native_tested_matches"]}<small> / {m["matched_methods"]}</small>',
        'COMPARISON_COUNT': f'{m["comparison_linked_matches"]} / {m["matched_methods"]}',
        'RAW_DETAIL': f'{m["raw_methods"]} methods · {m["raw_bytes"]:,} bytes',
        'UNKNOWN_DETAIL': f'{m["unknown_bytes"]:,} bytes · {percent(percentage(m["unknown_bytes"], m["executable_bytes"]))}',
        'TEXT_MATCH': percent(percentage(m['matched_bytes'], m['executable_bytes'])),
        'LIBRARY_DETAIL': f'{m["library_matched_bytes"]:,} matched bytes',
        'UNCLASSIFIED_DETAIL': f'{m["unclassified_matched_methods"]} matched methods · {m["unclassified_matched_bytes"]:,} bytes',
        'BOUNDED_DETAIL': f'{m["bounded_methods"]} bounded bodies · {m["methods"]-m["bounded_methods"]} entry points awaiting full extents',
        'CATEGORY_CARDS': ''.join(cards),
        'CATEGORY_OPTIONS': ''.join(f'<option value="{c["id"]}">{escaped(c["name"])}</option>' for c in categories.values()),
        'METHOD_ROWS': ''.join(rows), 'TU_ROWS': ''.join(tu_rows),
        'METHOD_COUNT': str(len(functions)), 'TU_COUNT': str(len(tu_rows)),
        'SECTION_ROWS': section_rows, 'GAP_ROWS': gap_rows,
        'MAPPED_WIDTH': f'{percentage(m["mapped_bytes"], m["executable_bytes"]):.5f}',
        'SOURCE_IDENTITY': escaped(inventory['original_sha256']),
        'NATIVE_NOTE': escaped(inventory.get('native_note', 'Native tests exercise source outside the game runtime.')),
    }
    def replace(match):
        key = match.group(1)
        if key not in values:
            raise ValueError(f'Unfilled HTML template field: {key}')
        return values[key]
    return re.sub(r'\{\{([A-Z_]+)\}\}', replace, TEMPLATE.read_text(encoding='utf-8'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--splits', type=Path, required=True)
    parser.add_argument('--comparison', type=Path, required=True)
    parser.add_argument('--target', type=Path, help='defaults to the compared target configuration')
    parser.add_argument('--source', type=Path, default=ROOT)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        comparison = json.loads(args.comparison.read_text(encoding='utf-8'))
        target_path = args.target or args.source / comparison.get(
            'target_config', 'decomp/targets/beachlife-uk-1.1.json')
        report = build_report(json.loads(args.splits.read_text(encoding='utf-8')),
                              comparison, json.loads(target_path.read_text(encoding='utf-8')),
                              args.source)
        output = render(report)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding='utf-8')
        print(json.dumps({'output': str(args.output), **report['metrics']}, indent=2))
    except (ValueError, KeyError, OSError) as error:
        parser.exit(1, f'Progress report: {error}\n')


if __name__ == '__main__':
    main()
