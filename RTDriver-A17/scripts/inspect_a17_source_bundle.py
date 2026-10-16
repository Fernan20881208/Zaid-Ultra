#!/usr/bin/env python3
"""Read-only inventory of Samsung A17 Open Source ZIP(s), including nested ZIPs.

It does not extract to disk, flash a device, or pretend to build a kernel.
"""
import argparse
import hashlib
import io
import json
import re
import tarfile
import zipfile
from pathlib import Path

MAX_DEPTH = 2
MAX_MEMBER = 80 * 1024 * 1024


def scan_zip(blob, name, depth=0):
    if depth > MAX_DEPTH:
        raise ValueError('Excessive ZIP nesting')
    result = {'name': name, 'sha256': hashlib.sha256(blob).hexdigest(), 'type': 'zip', 'children': []}
    with zipfile.ZipFile(io.BytesIO(blob)) as archive:
        for member in archive.infolist():
            if member.is_dir():
                continue
            if member.file_size > MAX_MEMBER:
                raise ValueError('Oversized entry: ' + member.filename)
            if '..' in Path(member.filename).parts:
                raise ValueError('Unsafe path: ' + member.filename)
            data = archive.read(member)
            if member.filename.lower().endswith('.zip'):
                child = scan_zip(data, member.filename, depth + 1)
            elif member.filename.endswith('.txt'):
                text = data.decode('utf-8', 'replace')
                bases = re.findall(r'Download and unzip the kernel source of\s+(A\d+[A-Z0-9]+)', text)
                child = {'name': member.filename, 'type': 'instructions', 'required_base_versions': sorted(set(bases))}
            elif member.filename.endswith(('.tar.gz', '.tgz')):
                with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as tf:
                    regular = [m.name for m in tf.getmembers() if m.isfile()]
                child = {
                    'name': member.filename,
                    'type': 'kernel-source-tar',
                    'files': len(regular),
                    'sample_paths': regular[:5],
                    'has_kernel_makefile': any(p.endswith('kernel-6.12/Makefile') for p in regular),
                    'has_module_symvers': any(p.endswith('/Module.symvers') or p == 'Module.symvers' for p in regular),
                }
            else:
                child = {'name': member.filename, 'type': 'other', 'bytes': len(data)}
            result['children'].append(child)
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('archive', type=Path)
    args = ap.parse_args()
    if not args.archive.is_file():
        ap.error('Cannot find input file')
    obj = scan_zip(args.archive.read_bytes(), args.archive.name)
    print(json.dumps(obj, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
