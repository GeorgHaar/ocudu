#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
# SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
# SPDX-License-Identifier: BSD-3-Clause-Open-MPI

"""
Generate the seed corpus of the fuzz harnesses of one or more layers.

Each layer describes its seeds in a seeds.py next to its harnesses, which defines CORPORA: a dict
mapping each fuzz target to the name of its corpus sub-directory and to its seeds, a dict of file
name to file content.

Usage:
    python3 tests/fuzz/gen_corpus.py SEEDS_FILE... [--out-dir DIR] [--zip-dir DIR]

With --out-dir, the seeds of each target are written to DIR/<sub-directory>/, the layout that
run_fuzzers.sh expects in FUZZ_CORPUS_DIR. The build does this for every layer with the fuzz_corpus
target, into <build>/tests/fuzz/corpus/.

With --zip-dir, the seeds of each target are written to DIR/<target>_seed_corpus.zip, the
convention expected by OSS-Fuzz.
"""

import argparse
import importlib.util
import pathlib
import sys
import zipfile

# The seeds files are imported from the source tree, which must stay clean.
sys.dont_write_bytecode = True


def load_corpora(seeds_file: pathlib.Path) -> dict:
    spec = importlib.util.spec_from_file_location(seeds_file.parent.name + "_seeds", seeds_file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.CORPORA


def write_seeds(seeds: dict, out_dir: pathlib.Path) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, data in seeds.items():
        path = out_dir / name
        path.write_bytes(data)
        print(f"  wrote {path} ({len(data)} bytes)")


def write_zip(seeds: dict, zip_path: pathlib.Path) -> None:
    zip_path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for name, data in seeds.items():
            zf.writestr(name, data)
    print(f"  wrote zip {zip_path} ({zip_path.stat().st_size} bytes)")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("seeds_files", metavar="SEEDS_FILE", type=pathlib.Path, nargs="+",
                        help="seeds.py of a layer")
    parser.add_argument("--out-dir", metavar="DIR", type=pathlib.Path,
                        help="directory to write the per-target seed sub-directories to")
    parser.add_argument("--zip-dir", metavar="DIR", type=pathlib.Path,
                        help="directory to write per-target zips to (for OSS-Fuzz)")
    args = parser.parse_args()
    if not args.out_dir and not args.zip_dir:
        parser.error("at least one of --out-dir and --zip-dir is required")

    for seeds_file in args.seeds_files:
        print(f"Generating fuzz corpus seeds of {seeds_file}...")
        for target, (subdir, seeds) in load_corpora(seeds_file).items():
            if args.out_dir:
                write_seeds(seeds, args.out_dir / subdir)
            if args.zip_dir:
                write_zip(seeds, args.zip_dir / f"{target}_seed_corpus.zip")
    print("Done.")


if __name__ == "__main__":
    main()
