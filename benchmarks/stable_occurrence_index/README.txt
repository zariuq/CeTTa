Stable occurrence index realization tournament
==============================================

This benchmark compares three physical realizations of one semantic object:
an ordered family of uniquely identified occurrences, contracted by an
occurrence mask and observed through one or more payload indexes.

The realizations are:

  dense-transport
      Index leaves store current ordered coordinates.  Every contraction
      compacts the authoritative rows and transports all index leaves.

  stable-identity
      Index leaves store stable occurrence identities.  Contraction compacts
      the authoritative rows and updates one identity-to-coordinate card;
      index leaves remain unchanged.  Queries project identities through the
      card and filter stale leaves.  A geometric stale/live boundary prunes
      the leaves without affecting correctness.

  rebuild
      Contraction compacts the authoritative rows and reconstructs every
      index leaf family from those rows.

  lazy-rebuild
      Contraction compacts the authoritative rows and invalidates the index.
      The first later query reconstructs it.  Several mutations with no
      intervening observer therefore share one reconstruction.

All realizations must return exactly the same coordinates in the same authored
relative order at every observation.  Equal payloads remain separate through
distinct occurrence identities.  A negative canary rejects reuse of one live
identity.

The workload portfolio varies:

  * selective read-heavy queries;
  * broad read-heavy queries;
  * prefix and suffix churn;
  * duplicate payloads;
  * many derived indexes;
  * mixed batch mutation/query traffic;
  * stale-leaf reclamation pressure.
  * mutation bursts followed by one observing phase.
  * an almost-pure query regime that should expose projection overhead.

The reported work counters are deterministic.  Timings are medians over an
odd number of trials.  peak_payload_bytes counts explicit array capacities;
it is not an RSS or allocator-overhead claim.

Run with:

  benchmarks/stable_occurrence_index/run.sh

An optional odd trial count from 1 through 31 may be supplied.

Interpretation boundary
-----------------------

For n authoritative rows, L index leaves, and q leaf observations between
mutations:

  dense transport   mutation Θ(n + L), observation Θ(q)
  stable identity   mutation Θ(n),     observation Θ(q) with projection
  rebuild           mutation Θ(n + L), observation Θ(q)
  lazy rebuild      mutation Θ(n),     first later observation Θ(n + L + q)

The stable-identity result is a zero-index-leaf-rewrite result, not an O(1)
mutation claim.  Eliminating the remaining row compaction would require a
separate ordered sparse-storage realization and its own query, lifetime,
reclamation, and channelling laws.
