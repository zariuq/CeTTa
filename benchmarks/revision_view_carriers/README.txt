Revision-view realization tournament
====================================

This standalone benchmark compares physical realizations of one semantic
object: an immutable, revision-keyed, ordered family of uniquely identified
equation occurrences.  A lease observes one frozen revision.  Replacing the
authority's current revision never changes an older lease.  Querying one head
preserves authored order and multiplicity; equal payloads remain distinct
when their occurrence identities differ.

Each concrete-head query includes both its exact-head occurrences and the
variable/wildcard-head occurrences, merged in authored order.  A separate
broad observer remains in the adversarial portfolio; it is not assumed to be
the normal callable-head path.

The comparison factors three independent implementation coordinates:

  representation layout
      How an immutable revision is physically retained.

  candidate index
      How occurrences for one head are selected without changing their
      observable order or multiplicity.

  lifetime policy
      How old representations remain available to outstanding leases and
      when retired storage may be reclaimed.

Four concrete tuples enter the first tournament:

  owned-flat-scan
      One owned flat occurrence vector per revision, scanned at query time;
      immutable roots are reference counted.

  owned-flat-index
      The same flat vector plus materialized per-head coordinate vectors;
      immutable roots are reference counted.

  persistent-head-blocks
      One immutable block per head.  A new revision shares unaffected blocks
      and rebuilds only touched blocks; roots and blocks are reference
      counted.  A broad query pays an explicit authored-order merge cost.

  revision-log
      One append-oriented occurrence log with born/dead revisions and
      per-head entry vectors.  Leases retain revision numbers.  Queries pay
      explicit visibility tests, and reclamation is deferred until no live
      lease can observe a retired entry.

Every query is compared element-for-element with an independent deep-copy
oracle.  Digest equality is reported only after exact identity, head, payload,
and authored-order equality has been checked.  Old leases are deliberately
held across later mutations.

Primary evidence is deterministic work and storage accounting:

  occurrence reads and writes; copied bytes; index reads and writes;
  visibility tests; authored-order merge comparisons; reference operations;
  reclamation scans; allocation calls and bytes; peak live bytes; peak
  retired bytes; authority builds and exact-key reuses; queries and emitted
  candidates.

Median elapsed time is secondary evidence.  It is not used to select a
runtime realization while unrelated evaluator-world reconstruction remains
outside this benchmark.

The workload portfolio includes static query-heavy definitions, small and
medium mutation-heavy attention-like traffic, mostly-static chaining,
single-head churn, unrelated-head churn, long-held old leases, append bursts,
broad queries, and duplicate-payload occurrences with distinct identities.

Three transfer rows replay the scale and observation shape measured from the
official ECAN workloads.  The focused AFRent row is a small static negative
control.  Two dynamicity rows issue the same concrete-head traffic over the
same unchanged equation family.  The source-revision row uses the five-run
median of 316 captures, approximately 196,000 queries, and six data-only
source-key invalidations.  Final authority disposal is teardown, so it does
not cause a replacement build and is excluded.  The equation-projection row
keys the same authority by its ordered equation occurrences and reuses it.
Their difference isolates revision-key work from carrier layout work without
attributing ECAN's nondeterministic scheduler tail to a candidate
representation.  Concentrating all six source-key invalidations on the queried
authority is a conservative upper bound on the carrier benefit.

Run with:

  benchmarks/revision_view_carriers/run.sh

An optional odd trial count from 1 through 31 may be supplied.

Interpretation boundary
-----------------------

This tournament does not model a particular tree library under another
name.  A concrete PathMap-root entrant belongs in a later phase only if it is
linked and measured as PathMap.  Likewise, reference counting and revision
epochs are lifetime policies rather than semantic payload carriers.  Results
are reported by complete realization tuple so that these costs remain
visible.
