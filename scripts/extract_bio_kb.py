#!/usr/bin/env python3
"""Extract chr16 pilot evidence from JSON caches → MeTTa atoms.

Reads the pre-computed JSON caches from hypothesis-generation-demo/scripts/cache/
and emits a MeTTa file with one (evidence-for SNP Gene eqtl+ eqtl- abc+ abc- reg+ reg-)
atom per (SNP, Gene) pair, zero-filling missing channels.

Usage:
    python3 scripts/extract_bio_kb.py > tests/bio_kb_chr16.metta
"""

import json
import os
import sys
from collections import defaultdict

CACHE = "/home/zar/claude/hypothesis-generation-demo/scripts/cache"

def load_json(name):
    with open(os.path.join(CACHE, name)) as f:
        return json.load(f)

def main():
    snps = load_json("snp_coords.json")
    eqtl_data = load_json("gtex_eqtl.json")
    abc_data = load_json("abc_features.json")

    # Collect all (snp, gene) → {channel: (n+, n-)} evidence
    evidence = defaultdict(lambda: {"eqtl": (0, 0), "abc": (0, 0), "reg": (0, 0)})

    # eQTL: entries dict keyed by "rsXXX|ensgYYY"
    eqtl_entries = eqtl_data.get("entries", {})
    for key, val in eqtl_entries.items():
        parts = key.split("|")
        if len(parts) != 2:
            continue
        snp, gene = parts
        # Count tissues as evidence
        if isinstance(val, dict):
            tissues = val.get("tissues", [])
            n_plus = len(tissues) if tissues else 1
            n_minus = max(0, 49 - n_plus)  # 49 GTEx tissues total
        elif isinstance(val, list):
            n_plus = len(val)
            n_minus = max(0, 49 - n_plus)
        else:
            n_plus, n_minus = 1, 0
        evidence[(snp, gene)]["eqtl"] = (n_plus, n_minus)

    # ABC: keyed by "rsXXX|ensgYYY", value has n_plus, n_minus
    for key, val in abc_data.items():
        parts = key.split("|")
        if len(parts) != 2:
            continue
        snp, gene = parts
        n_plus = val.get("n_plus", 0)
        n_minus = val.get("n_minus", 0)
        evidence[(snp, gene)]["abc"] = (n_plus, n_minus)

    # Regulatory: check for TFBS/enhancer overlap
    # For now, mark as (1, 0) if any ABC or eQTL exists (proxy for regulatory)
    # TODO: load actual regulatory data from genehancer
    for (snp, gene), channels in evidence.items():
        if channels["eqtl"] != (0, 0) or channels["abc"] != (0, 0):
            evidence[(snp, gene)]["reg"] = (1, 0)

    # Emit MeTTa
    print("; Bio KB chr16 pilot: evidence atoms for PLN aggregation benchmark")
    print(f"; {len(snps)} SNPs, {len(evidence)} (SNP, Gene) pairs")
    print(f"; Channels: eqtl (GTEx tissue count), abc (contact count), reg (presence)")
    print(f"; Format: (evidence-for SNP Gene eqtl+ eqtl- abc+ abc- reg+ reg-)")
    print()

    for (snp, gene), channels in sorted(evidence.items()):
        ep, en = channels["eqtl"]
        ap, an = channels["abc"]
        rp, rn = channels["reg"]
        print(f"(evidence-for {snp} {gene} {ep} {en} {ap} {an} {rp} {rn})")

    print(f"\n; Total: {len(evidence)} evidence atoms", file=sys.stderr)

if __name__ == "__main__":
    main()
