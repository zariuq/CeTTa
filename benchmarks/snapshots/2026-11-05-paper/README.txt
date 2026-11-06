Receipts of the application measurements in the CeTTa paper (5 November 2026)
============================================================================

These records back the appendix "Application Measurements: 5 November 2026".
They replace, for the paper, the application, example and HE records in
../2026-11-05/, which used other binaries, a two-second example cap and a
five-second HE cap.

Identities
- CeTTa: 1.5.0-dev, default build (BUILD=python), sha256 18091c10633b6be2fca3a4bfa14944e6a947315c0be44f1df83dde3633349bd2,
  built from 2ca1c923 with the preview repairs then staged.
- MORK: 1c44f70f34db6305c3ba7bdb2e1434db3387778b with PathMap fb979b423e674ab3729b548946678f5ba8677fa5,
  nightly release builds: default (sha256 88c740354eae0ce6...) and with the leapfrog feature (0e5082db27cba8da...).
- SWI-PeTTa: b74af1bc26aff59636548ba7cde479ec16624577 with local uncommitted fixes, SWI-Prolog 10.1.9.
- Hyperon Experimental: metta-repl 0.2.10 from fork branch "fixes" b1b2206a (upstream 3f76dc46 plus three fixes),
  sha256 32b45f7dbeec7301...
- ai-mathematician: the PeTTa port ran on the NIK line's binary sha256 f11e8311279f25b6...; the preview binary returns
  no answer from (ai-math:search $decls).

Method
- Three fresh processes per engine and case, alternating order; wall time of the whole process, user and system
  CPU and peak RSS from /usr/bin/time, MORK's own execution time, instructions:u (perf stat) in opt-base.tsv only.
  In the application table, CeTTa's and the leapfrog build's times come from opt-base-leapfrog.tsv (no perf stat);
  MORK's default build and SWI-PeTTa come from opt-base.tsv, whose perf stat adds about 7 ms per run. The scale
  ladder (opt-scale.tsv) counted instructions throughout.
- Every engine had an unlimited stack; RAYON_NUM_THREADS=1; each batch pinned to the least busy logical CPU.
- Other agents kept the one-minute load average between about 5 and 24 during the batches; the showcase rows record it.

Files
- opt-base.tsv, opt-base-leapfrog.tsv, opt-scale.tsv: the Opt application suite.
- check-*-complete-sets*.tsv: complete projected result-set comparisons at the small and timed sizes.
- mork-leapfrog.tsv, mork-leapfrog-check.tsv: MORK's showcase workloads with oracle counts and rows.
- he-app.tsv, he-join.tsv, he-path.tsv: HE lane against Hyperon Experimental.
- petta-examples-*.json, petta-examples-summary.tsv: the 164 PeTTa examples.
- nupln.tsv, ai-mathematician-recall.json, independent-oracles.json.

Generators and runners: benchmarks/opt_suite/, benchmarks/mork_leapfrog/, benchmarks/petta_examples/,
benchmarks/nupln/, benchmarks/mam_families/ (HE cases).
