Equation-aware occurrence postings
==================================

Run from the CeTTa root:

  sh benchmarks/equational_postings/run.sh 0  # correctness only
  sh benchmarks/equational_postings/run.sh 5  # verify, then five timing rounds
  python3 benchmarks/equational_postings/runtime.py ./cetta

The standalone C experiment indexes an already reconciled catalogue. The
outer relation has stable, globally ordered positions and a raw class key.
One Range/3 recipe describes each key in the experiment. A lookup starts at
the bound type coordinate, rejects the wrong bounds, then seeks the outer
postings. Equal payloads at different positions remain two occurrences.
Recipe edits and row edits are separate events. An edit is complete before
the next read begins.

Every indexed method is compared with a raw scan on the same deterministic
trace. COUNT, FIRST, and the complete ordered list of positions are checked,
not only a digest. There are also controls for sparse-to-dense-to-sparse
conversion, reusing an emptied page, and retracting a recipe from a cached
count. Only then do timing rounds run.

The physical posting choices are 32-bit sorted runs, a bitmap, and blocks
that start with 16-bit offsets and convert to bitmaps when dense. Count
summaries can be maintained for all keys, or installed after repeat demand
and maintained by relevant deltas. A routed method also lets broad ordered
reads prefer a scan over a many-bucket merge. Its simple work estimate is
an experiment, not a globally optimal policy.

CSV times are separated into setup, writes and each observer. Peak posting
bytes counts dynamically allocated posting data and block directories
including their capacity. It excludes allocator overhead, common source
rows, output buffers, and fixed directories. Counters and raw ordered
answers, not small timing differences, are the basis for checking work.

runtime.py measures the current engine on the same selective Schema/Range
shape, checking the answer after each unrelated insertion. The inner Range
term is closed, which the current engine supports. The literal spelling
route is another measurement of the same answer on this fixture, not a
general modulo match. The script reports process timings with a no-sample
baseline; a subtraction at the timer/noise floor can be zero.

Neither program implements arbitrary congruence updates, compaction or a
new CeTTa execution route. The standalone costs do not establish an
end-to-end speedup for CeTTa.
