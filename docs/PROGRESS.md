# Reconstruction progress

`tools/decomp_progress.py` generates one standalone HTML file from an audited
split inventory and a fresh matching result. It works offline, with searchable
methods, subsystem filters, reconstructed source files and explicit unmapped
ranges. It does not publish anything or execute the original game.

Keep generated inventories and reports outside tracked source, for example
under `build/progress/`. The renderer and generated-fixture tests are reusable;
the investigation snapshot is a separate input.

## Generate a snapshot

Run the [matching comparison](../decomp/README.md) first. Its `result.json`
records hashes of the prepared reconstructed source, baseline pin, applied
reconstruction patches, comparison configuration and tools.
The renderer rejects results whose inputs have changed.

```sh
python3 tools/decomp_progress.py \
  --splits build/progress/splits.json \
  --comparison orig/analysis/matching/result.json \
  --output build/progress/index.html
```

Open the HTML file directly in a browser. It has no server, external assets,
analytics or automatic refresh. Regenerate it after updating either the audited
inventory or the comparison evidence. `--source` and `--target` can select a
different local checkout and matching-target JSON. By default the renderer uses
the fingerprinted target configuration from the matching export, so newly patched
methods are included. Inventory source paths refer to the prepared C++ files in
that comparison; regenerate the matching export if they have been removed.

## Reading the percentages

- **Matching** counts the full byte extents of game methods with a strict 100%
  normalized comparison, divided by bounded game-method bytes in the inventory.
  Partial instruction-similarity scores do not contribute completed bytes.
  Raw byte equality is reported separately.
- **Linking** counts reconstructed game bytes integrated into the production
  native runtime, over that same bounded-game denominator. Linking into the
  VC6 comparison image and execution in a native test harness are separate
  columns. Neither establishes production game integration.
- **Executable inventory** measures nonoverlapping bounded bodies against
  logical executable-section extents. Its unknown remainder includes methods,
  libraries, tables and padding still to classify. File-alignment tails are
  excluded. An observed address with an unknown end adds no bounded bytes.
- **Source TUs** are current reconstructed C++ files. Their percentages cover
  their registered methods; they are not recovered original translation units.
  Subsystems are descriptive groupings, and unmapped categories stay visible.

These are known-scope metrics. A whole-game completion percentage requires a
complete inventory of code and data dependencies, original boundaries and
runtime reachability. Small fully matched subsets cannot supply that denominator.
Library support and compiler glue remain separate from game-method totals.

## Split inventory contract

The UTF-8 JSON has `schema_version: 1`, a `title`, the matching target's `target` ID
and `original_sha256`, plus `snapshot` with `date` and full reconstructed-source
`revision` (distinct from later host or report-tool commits).
It contains four arrays:

| Array | Fields |
| --- | --- |
| `sections` | `name`, hexadecimal `start`, hexadecimal `end_exclusive`; logical executable sections only |
| `categories` | unique `id`, display `name`, brief `summary` |
| `units` | unique `id`, display `name`, parent `category` ID |
| `functions` | `address`, `size`, `name`, `unit`, `kind`, `source`, `candidate_match`, `comparison_linked`, `native_tested`, `runtime_linked`, `note` |

Function `size` is a verified positive byte length, or `null` until the complete
extent is known. `kind` is `game`, `library`, `compiler` or `unclassified`.
`source` is a repository-relative reconstructed source path, or `null`.
`candidate_match` is `null` or a measured score in `[0, 1)`; never enter 100%
manually. The current comparison result supplies completed scores and raw-byte
status for every method registered in the matching target. Address annotations,
source fingerprints, target identity and method sizes must agree.

Functions with unresolved provenance can use the optional
`unclassified_functions` target and result group. Its records have the same
`name`, `address` and `size` fields as game entries, use `FUNCTION` annotations,
and require split `kind: unclassified`. They pass the same strict comparison;
missing or nonmatching entries fail the run. Raw byte equality is reported
separately. Addresses must be unique across game, library and unclassified
groups. The report shows unclassified matches separately and excludes them
from game and library totals until their classification is established.

The three linkage/execution fields are booleans backed by separate build/test
evidence. Native tests do not imply runtime linkage. `note` states the remaining
work; optional top-level `native_note` describes native test scope. Names are
descriptive until original symbols are recovered. IDs use lowercase letters,
digits, underscores or hyphens. Overlaps, duplicate addresses, invalid scores,
uncovered target methods and stale comparison inputs fail generation.

Audit boundaries before adding entries, retain evidence for classifications and
test/link flags, and keep uncertain ranges unassigned. After a matching commit,
refresh the comparison and snapshot together; expanding the inventory can lower
the known-scope percentage even when completed code has increased.
