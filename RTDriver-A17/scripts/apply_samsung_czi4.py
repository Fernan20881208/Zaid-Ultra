#!/usr/bin/env python3
"""Validate and optionally overlay Samsung A175F CZI4 kernel source changes.
Default is read-only. Requires separately obtained full A175FXXU5CZE9 sources.
Never loads modules or flashes the device.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import shutil
import stat
import tarfile
import time
import zipfile

TARGET = "A175FXXS7CZI4_kernel.tar.gz"
KNOWN_SHA256 = "643bde1afbf8dd17f98962314245818a0057ed46d6a22503f7ddeacd0478495b"

def read_patch(zip_path):
    zhash = hashlib.sha256(zip_path.read_bytes()).hexdigest()
    with zipfile.ZipFile(zip_path) as zf:
        if TARGET not in zf.namelist():
            raise ValueError("No se encontro " + TARGET + " en ZIP")
        archive = zf.read(TARGET)
    found = []
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:gz") as tar:
        for m in tar:
            if m.isdir():
                continue
            if not m.isfile():
                raise ValueError("Entrada no regular en tar: " + m.name)
            parts = PurePosixPath(m.name).parts
            if len(parts) < 3 or parts[0] != "Kernel" or any(c in ("", ".", "..") for c in parts):
                raise ValueError("Ruta inesperada: " + m.name)
            if m.size > 5 * 1024 * 1024:
                raise ValueError("Archivo inesperadamente grande: " + m.name)
            blob = tar.extractfile(m).read()
            if len(blob) != m.size:
                raise ValueError("Error leyendo: " + m.name)
            found.append((Path(*parts[1:]), blob))
    if len(found) < 1:
        raise ValueError("Sin actualizaciones")
    if len(set(p for p, _ in found)) != len(found):
        raise ValueError("Rutas repetidas en archivo")
    return zhash, found

def source_root(path):
    root = path.resolve()
    if root.name != "Kernel":
        root = root / "Kernel"
    for f in ("kernel-6.12/Makefile", "kernel/kernel_device_modules-6.12/scripts/gen_build_config.py"):
        if not (root / f).is_file():
            raise ValueError("Faltan fuentes base A175FXXU5CZE9; se esperaba " + str(root / f))
    return root

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--update-zip", type=Path, required=True)
    ap.add_argument("--base-dir", type=Path, help="Directorio que contiene Kernel/ (o Kernel/ mismo)")
    ap.add_argument("--apply", action="store_true", help="Aplica actualización y hace backup")
    args = ap.parse_args()
    zhash, entries = read_patch(args.update_zip)
    print("SHA256 ZIP:", zhash)
    if zhash != KNOWN_SHA256:
        print("ADVERTENCIA: hash distinto del ZIP aportado; revisar procedencia")
    print("Archivos de actualización:", len(entries))
    if not args.base_dir:
        if args.apply:
            ap.error("--apply exige --base-dir")
        for rel, data in entries:
            print(" ", rel.as_posix(), len(data))
        return
    root = source_root(args.base_dir)
    # Prevent escaping base via preexisting symlinks or parent paths.
    for rel, _ in entries:
        dest = root / rel
        if not dest.resolve().is_relative_to(root.resolve()):
            raise ValueError("Ruta con enlace externo no permitida: " + str(dest))
    existing = sum((root / rel).is_file() for rel, _ in entries)
    print(f"Fuentes base identificadas: {root}")
    print(f"Archivos actualizados que ya existen: {existing}/{len(entries)}")
    if not args.apply:
        print("Modo inspección. No se ha modificado nada; use --apply si confirma el arbol base.")
        return
    backup = root.parent / ("a17-czi4-backup-" + time.strftime("%Y%m%d-%H%M%S", time.gmtime()))
    if backup.exists():
        raise ValueError("Directorio backup ya existe")
    backup.mkdir(parents=True)
    manifest = []
    for rel, data in entries:
        dest = root / rel
        was_file = dest.is_file()
        if dest.exists() and not was_file:
            raise ValueError("Destino no es archivo regular: " + str(dest))
        if was_file:
            saved = backup / rel
            saved.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(dest, saved)
        dest.parent.mkdir(parents=True, exist_ok=True)
        # Samsung's tarball includes read-only source files (e.g. GPU Mali).
        # Give the owner temporary write permission and then restore the original mode.
        old_mode = stat.S_IMODE(dest.stat().st_mode) if was_file else None
        if was_file:
            dest.chmod(old_mode | stat.S_IWUSR)
        try:
            dest.write_bytes(data)
        finally:
            if was_file:
                dest.chmod(old_mode)
        manifest.append({"path": rel.as_posix(), "existed": was_file, "sha256": hashlib.sha256(data).hexdigest()})
    (backup / "update-manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf8")
    print("Actualización aplicada. Backup:", backup)
    print("IMPORTANTE: todavía falta configurar la compilación y generar Module.symvers del kernel exacto.")

if __name__ == "__main__":
    main()
