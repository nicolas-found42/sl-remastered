"""Hash-bound structural TUFA branch inventory, never runtime branch coverage.

The independent Stock-derived TUFA reader supplies nodes. Only explicit relative
branch opcodes 14/15/16 are enumerated. Exception/native/dynamic-call behavior and
data-driven branches remain outside this inventory and explicitly unresolved.
"""
import hashlib
from pathlib import Path


def tree_branches(nodes):
    rows = []
    for index, (op, line, operand) in enumerate(nodes):
        if op not in (0x14, 0x15, 0x16):
            continue
        if type(operand) is not int or not 0 <= operand <= 0xffffffff:
            raise ValueError('branch has no valid raw relative operand')
        offset = operand if operand < 0x80000000 else operand - 0x100000000
        target = index + offset
        if not 0 <= target < len(nodes):
            raise ValueError(f'branch {index} targets {target} outside tree of {len(nodes)} nodes')
        successors = [{'edge': 'taken', 'node': target, 'runtime_evidence': []}]
        if op != 0x14:
            successors.append({'edge': 'fallthrough', 'node': index + 1,
                               'exits_tree': index + 1 == len(nodes), 'runtime_evidence': []})
        rows.append({'node': index, 'opcode': op, 'source_line': line,
                     'operand_u32': operand, 'relative_target': target,
                     'conditional': op != 0x14, 'successors': successors,
                     'semantic_assertion': 'pending', 'paired_proofs': []})
    return rows


def inventory(game, manifest, loader, decode_error):
    game = Path(game).resolve(strict=True)
    files = []
    totals = {'files': 0, 'decoded_files': 0, 'classes': 0, 'trees': 0,
              'nodes': 0, 'branches': 0, 'conditional_branches': 0, 'edges': 0,
              'runtime_covered_edges': 0, 'decode_failures': 0}
    for relative, identity in sorted(manifest['files'].items()):
        if Path(relative).suffix.lower() != '.class':
            continue
        path = game / relative
        row = {'path': relative, **identity, 'classes': [], 'status': 'pending'}
        totals['files'] += 1
        try:
            if any((game / Path(*Path(relative).parts[:n])).is_symlink()
                   for n in range(1, len(Path(relative).parts) + 1)):
                raise ValueError('script path includes symlink')
            if path.stat().st_size != identity['size'] or identity['size'] > 16 * 1024 * 1024:
                raise ValueError('changed or oversized script')
            before = path.read_bytes()
            if hashlib.sha256(before).hexdigest() != identity['sha256']:
                raise ValueError('script changed after manifest')
            classes = loader(path)
            if path.read_bytes() != before:
                raise ValueError('script changed while decoding')
            for class_index, cls in enumerate(classes):
                trees = []
                for tree_index, nodes in enumerate(cls['trees']):
                    branches = tree_branches(nodes)
                    owners = [{'kind': kind, 'group': group, 'record': ordinal,
                               'flags': rec[0], 'name_constant': rec[1], 'descriptor_constant': rec[2]}
                              for kind, groups in (('method', cls.get('mthd', [])), ('field', cls.get('fild', [])))
                              for group, records in enumerate(groups) for ordinal, rec in enumerate(records)
                              if rec[3] == tree_index and not (kind == 'method' and rec[0] & 0x40)]
                    trees.append({'tree': tree_index, 'nodes': len(nodes), 'owners': owners,
                                  'branches': branches, 'runtime_evidence': []})
                row['classes'].append({'class_index': class_index, 'trees': trees})
            row['status'] = 'decoded_structural_branches'
            totals['decoded_files'] += 1
            for cls in row['classes']:
                totals['classes'] += 1
                for tree in cls['trees']:
                    totals['trees'] += 1
                    totals['nodes'] += tree['nodes']
                    for branch in tree['branches']:
                        totals['branches'] += 1
                        totals['conditional_branches'] += branch['conditional']
                        totals['edges'] += len(branch['successors'])
        except (OSError, ValueError, IndexError, TypeError, KeyError, decode_error) as error:
            row['classes'] = []
            row['status'] = 'inconclusive'
            row['reason'] = str(error)
            totals['decode_failures'] += 1
        files.append(row)
    return {'schema': 1, 'status': 'structural_script_inventory', 'files': files,
            'summary': totals, 'explicit_branch_decode_complete': totals['decode_failures'] == 0,
            'semantic_branch_inventory_complete': False,
            'limits': ['static explicit TUFA relative branches only; not reachable-path or behavior coverage',
                       'exception, native, dynamic-call and data-driven alternatives remain unenumerated',
                       'every enumerated edge awaits runtime Stock evidence and paired proof']}
