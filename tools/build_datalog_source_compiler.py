#!/usr/bin/env python3
"""Build the D0 source provider using the common in-process Lean runtime."""
import argparse
from pathlib import Path
from build_lean_source_compiler import build_provider
from build_lean_source_compilers import PROVIDERS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lean-root', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--skip-build', action='store_true')
    parser.add_argument('--package-output', type=Path)
    args = parser.parse_args()
    module, prefix, name, _, _ = next(provider for provider in PROVIDERS if provider[1] == 'datalog')
    build_provider(args.lean_root, module, prefix, name, args.output, args.skip_build, args.package_output)


if __name__ == '__main__':
    main()
