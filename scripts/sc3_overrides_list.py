"""SC3's override list for BozoEngine's bozo-externals: every address bozo/port/*.cpp registers
(runtime.registerFunction(<address>, ...)), whatever environment toggle guards it, and every store the store patch
tells apart (a "// bozo:store <constant>" line next to it).

    python scripts/sc3_overrides_list.py <port folder> <out.txt> [--map <map> --reviewed <addresses>]

The port names SLUS_212.16's addresses: with a release's map and reviewed addresses (recomp/pal/), the list carries
their places in that release (scripts/sc3_region_map.py).

The generated code must route every one of them to the runtime: BozoEngine checks at startup that each registered
override is an external of the generated code (bozo::port::undeclared) and refuses to run otherwise.
"""
import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sc3_region_map

CONST = re.compile(r'\b(k[A-Z0-9]\w*)\s*=\s*(?:at\()?(0x[0-9A-Fa-f]+)u?\b')
ARRAY = re.compile(r'\b(k[A-Z0-9]\w*)\[\]\s*=\s*\{([^}]*)\}')
LOOP = re.compile(r'for\s*\(\s*uint32_t\s+(\w+)\s*:\s*(k\w+)\s*\)')
REGISTER = re.compile(r'registerFunction\(\s*([^,]+?)\s*,')
STORE = re.compile(r'//\s*bozo:store\s+(k\w+)')
NATIVE = re.compile(r'//\s*bozo:native\s+(k\w+)')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('folder', type=Path)
    ap.add_argument('out', type=Path)
    ap.add_argument('--map')
    ap.add_argument('--reviewed')
    args = ap.parse_args()
    folder, out = args.folder, args.out
    files = sorted(folder.glob('*.cpp'))
    constants, perFile = {}, {}
    for f in files:
        text = f.read_text(encoding='utf-8')
        local = {}
        for name, value in CONST.findall(text):
            local[name] = int(value, 16)
            constants.setdefault(name, int(value, 16))
        perFile[f] = local
    overrides, stores, native, errors = {}, {}, {}, []

    def resolve(f, expr):
        expr = re.sub(r'^at\((.*)\)$', r'\1', expr.strip())
        if re.fullmatch(r'0x[0-9A-Fa-f]+u?', expr):
            return int(expr.rstrip('u'), 16)
        if expr in perFile[f]:
            return perFile[f][expr]
        if expr in constants:
            return constants[expr]
        errors.append(f'{f.name}: cannot resolve {expr}')
        return None

    for f in files:
        text = f.read_text(encoding='utf-8')
        arrays = {n: [int(v, 16) for v in re.findall(r'0x[0-9A-Fa-f]+', body)] for n, body in ARRAY.findall(text)}
        loops = {var: arrays.get(arr) for var, arr in LOOP.findall(text)}
        for expr in REGISTER.findall(text):
            if loops.get(expr.strip()):
                for a in loops[expr.strip()]:
                    overrides.setdefault(a, f'{f.name}: {expr} (a loop)')
                continue
            a = resolve(f, expr)
            if a is not None:
                overrides.setdefault(a, f'{f.name}: {expr}')
        for name in STORE.findall(text):
            a = resolve(f, name)
            if a is not None:
                stores[a] = f'{f.name}: {name}'
        for name in NATIVE.findall(text):
            if name not in arrays:
                errors.append(f'{f.name}: native instruction array {name} was not found')
            for a in arrays.get(name, []):
                native[a] = f'{f.name}: {name}'
    if args.map:
        region = sc3_region_map.Region(args.map, args.reviewed)

        def placed(found):
            moved = {}
            for a, where in found.items():
                here, why = region.place(a)
                if why:
                    errors.append(f'{where}: 0x{a:x}: {why}')
                else:
                    moved[here] = where
            return moved
        overrides, stores, native = placed(overrides), placed(stores), placed(native)
    if errors:
        sys.exit('\n'.join(errors))
    lines = ['# SC3\'s overrides and patched stores (scripts/sc3_overrides_list.py from bozo/port/*.cpp). Do not edit.']
    lines += [f'override 0x{a:x}  # {where}' for a, where in sorted(overrides.items())]
    lines += [f'store 0x{a:x}  # {where}' for a, where in sorted(stores.items())]
    lines += [f'native 0x{a:x}  # {where}' for a, where in sorted(native.items())]
    text = '\n'.join(lines) + '\n'
    if not out.exists() or out.read_text(encoding='utf-8') != text:
        out.write_text(text, encoding='utf-8', newline='\n')
    print(f'sc3_overrides_list: {len(overrides)} overrides, {len(stores)} patched stores, {len(native)} native instructions -> {out}')


if __name__ == '__main__':
    main()
