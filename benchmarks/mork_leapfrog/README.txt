MORK showcase workloads against CeTTa
=====================================

Each generator writes, for one task, an MM2 program for MORK (NAME.mm2), a
PeTTa program for CeTTa (NAME.metta), the output relations (NAME.heads) and
the relation sizes computed by an independent Python oracle
(NAME.expect.json). Most also write the oracle's complete rows for the
value-bearing relations (NAME.values, NAME.oracle_values.json).

The 5 November 2026 measurements used these 36 tasks (OUT is any directory):

    python3 graph_graphalytics/gen_graphalytics.py --graph500 12 --outdir OUT
    python3 graph_graphalytics/gen_graphalytics.py --graph500 14 --outdir OUT
    python3 graph_graphalytics/gen_graphalytics.py --graph500 16 --kernels bfs,wcc,cdlp --outdir OUT
    python3 graph_lubm/gen_lubm.py 1 --outdir OUT        # also 5 and 10
    python3 graph_ldbc_snb/gen_snb.py DATASET --outdir OUT
    python3 bfc/gen_bfc.py 19 OUT
    python3 circuit/gen_circuit.py 4096 --outdir OUT
    python3 alg_cd/gen_cd.py KS 5 --outdir OUT           # also XGK 5 and lukC 6
    python3 alg_ctl/gen_ctl.py 8 --outdir OUT            # also 9
    python3 alg_hamming/gen_hamming.py 100000 --outdir OUT   # also 300000
    python3 alg_clique/gen_clique.py 800 57600 --mm2-order early --outdir OUT
    python3 alg_apriori/gen_apriori.py 50000 --outdir OUT
    python3 kg/gen_kg.py 1000000 --outdir OUT

The Graphalytics kernels run on Graph500 Kronecker graphs; gen_graphalytics.py
also reads an official LDBC Graphalytics dataset directory with --dataset.
gen_snb.py needs an extracted official LDBC SNB dataset
(social_network-sfX-CsvBasic-LongDateFormatter; the measurements used SF0.1
and SF0.3). The million-fact knowledge graph writes several gigabytes.
Generated inputs are not stored in the repository.

Run from the CeTTa checkout:

    python3 benchmarks/mork_leapfrog/run.py OUT/NAME... --mork MORK --cetta ./cetta \
      --output RESULTS [--trials 3] [--unlimited-stack]

MORK runs `mork run NAME.mm2 STORE.mm2 --instrumentation 0`; CeTTa runs
`cetta --profile extended --lang petta NAME.metta`. A trial passes only if the
engine exits normally, every relation size equals the oracle's and, where the
oracle lists rows, the engine's complete row set equals the oracle's. Each row
of RESULTS/results.json records wall time, user and system CPU time and peak
resident set size, and MORK's reported execution time. The measured reference
was MORK built with its experimental leapfrog join (cargo feature leapfrog).
