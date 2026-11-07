Opt application and scale benchmarks
===================================

Nine application tasks pair an MM2 program with an ordinary PeTTa program over
the same generated facts: two-hop/triangle materialization, five-clique
enumeration, odd-even sorting, backward proof search, Hexlife, ancestry,
path-producing reachability, a unary counter machine and persistent process
communication. The MM2 programs are MORK's own benchmark, resource and
tutorial programs; the PeTTa programs are idiomatic solutions of the same
tasks, not translations, and the counter machine, backward proof search and
Hexlife use different algorithms from their MM2 counterparts. The graph task
computes distinct two-hop pairs and directed closed triples; despite the
historical transitive_* case names it is not fixed-point transitive closure.

Run from the CeTTa repository:

    python3 benchmarks/opt_suite/generate.py OUT/base
    python3 benchmarks/opt_suite/run.py OUT/base OUT/results \
      --cetta ./cetta --mork /path/to/mork --petta-root ../PeTTa --unlimited-stack
    python3 benchmarks/opt_suite/summarize.py OUT/results/results.json

generate.py --help lists every case: the nine base cases, small controls
(*_small), the scale ladder (transitive_200k/1m/5m, oddeven_2000/5000,
clique5_300x8100, bc_3000, hexlife_28_200_30, ancestry_3k, reach_1500,
counter_1000, pc_500) and clique5_200x3600_early, the five-clique query with
its ten conditions ordered so that each new vertex's edges are checked as soon
as it is introduced. MORK's default join follows the written order of an exec
body; MORK's own benchmark uses the lexicographic order of clique5_200x3600.

run.py records, per process trial, whole-process wall time, user and system
CPU time, peak RSS (from /usr/bin/time), MORK's own execution time and, with
--instructions, instructions:u. Engines alternate order across trials;
--engines selects a subset of mork,cetta,swi (mork is the reference and is
always included). --unlimited-stack gives every engine an unlimited stack:
MORK recurses over term depth and aborts on the unary-clock programs with the
default stack. --timeout and --budget bound the work and never turn an
incomplete run into a pass; failures stay in results.json.

Observers
---------

Every timed run checks each result relation's size against MORK's final store.
With --observe the generator also prints every result row, and run.py compares
the complete alpha-normalized sets, renaming the process calculus's !, ? and |
to the PeTTa constructors send, recv and par:

    python3 benchmarks/opt_suite/generate.py OUT/observe --observe
    python3 benchmarks/opt_suite/run.py OUT/observe OUT/checks \
      --cetta ./cetta --mork /path/to/mork --petta-root ../PeTTa --trials 1 --observe --unlimited-stack

This check exposed an error in the original PeTTa counter program: it stored
an unevaluated (peano $ic) in the instruction counter, because an atom being
added is data. Row counts matched; the state rows did not. The corrected
generator evaluates the numeral first; earlier counter timings measured the
faulty program.

oracles.py has independent Python computations: two-hop pairs and triangles
(bit sets, or hash sets for large graphs), five-cliques of the ordered graph,
and the counter machine as an integer machine.

MORK's ordinary execution summary reports its execution time separately from
loading its input and writing its final store; the MeTTa programs print only
their relation sizes. Report both MORK columns. MORK's --timing flag is not
used: it adds timing facts to the store.

SWI-PeTTa uses the direct bag plan where rows are already distinct; recursive
duplicate-producing relations use an explicit set insert keyed by
variant_sha1. These are declared program adapters for a system without set
spaces.

Large graph series
------------------

The larger graph series uses NumPy PCG64 with seed 0:

    python3 benchmarks/opt_suite/generate_large_graph.py OUT/large graph150 375000 7500000
    python3 benchmarks/opt_suite/generate_large_graph.py OUT/large graph600 1500000 30000000

These reproduce the 29 October 2026 input series; graph150 has 149,892,361
two-hop pairs and 8,897 triangles by oracles.two_hop_counts_sparse.
