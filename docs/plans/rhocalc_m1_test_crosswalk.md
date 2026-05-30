# Rhocalc M1 Test Crosswalk

This note maps the `M1: Strict-Core Runtime Correctness` clauses in
`docs/plans/rhocalc_plan_current.md` to the active public gates in the
current tree.

It is intentionally honest about the one clause that remains primarily
architectural rather than black-box enforced.

| M1 clause | Active coverage | Status | Notes |
| --- | --- | --- | --- |
| Strict-core exposes only the six paper constructors | `test-rhocalc` reject fixtures: `tests/rhocalc/reject_fresh.mrho`, `tests/rhocalc/reject_join.mrho`, `tests/rhocalc/reject_quoted_payload.rho`, `tests/rhocalc/reject_send_continuation.mrho` | Enforced | Guards against widening strict-core surface. |
| Reduction relation is primary; scheduler chooses one trajectory | `tests/rhocalc_scheduler_membership.tsv`, `tests/rhocalc_relation_equivalence.tsv` | Enforced | Scheduler residuals must stay inside the bounded relation image. |
| Deterministic scheduling is policy, not calculus property | Public `--rho-scheduler`; scheduler witnesses in `tests/rhocalc/rotating_scheduler_persistent_branch.mrho` and `tests/rhocalc/rotating_scheduler_primed_diamond.mrho` | Enforced | Canonical and rotating are compared as policies over the same relation. |
| Name equivalence, structural congruence, and alpha-equivalence move together | `tests/rhocalc/name_quote_drop.mrho`, `tests/rhocalc/nested_drop_quote_h5.mrho`, `tests/rhocalc/channel_eq_par_zero_h2.mrho`, `tests/rhocalc/channel_eq_par_comm_h2.mrho`, `tests/rhocalc/channel_eq_par_assoc_h2.mrho`, `tests/rhocalc/channel_eq_alpha_h2.mrho` | Enforced | Covered in both direct run fixtures and equivalence lanes. |
| Lift/output is asynchronous: channel + payload only, never a continuation | `tests/rhocalc/pure_surface.rho`, `tests/rhocalc/reject_quoted_payload.rho`, `tests/rhocalc/reject_send_continuation.mrho` | Enforced | Positive and negative coverage. |
| `rho:par` order does not change enabled COMM behavior modulo structural congruence | `tests/rhocalc/channel_eq_par_zero_h2.mrho`, `tests/rhocalc/channel_eq_par_comm_h2.mrho`, `tests/rhocalc/channel_eq_par_assoc_h2.mrho`, plus relation/equivalence lanes | Enforced | Same behavior under reordered parallel structure. |
| Substitution is capture-safe | `tests/rhocalc/capture_avoidance_alpha_h7.mrho`, `tests/rhocalc/capture_shadow_ok_h1.mrho`, `tests/rhocalc/recv_under_binder_h4.mrho`, `tests/rhocalc/quote_substitution_opaque_h7.mrho` | Enforced | Includes shadowing, alpha-renaming, and quote opacity. |
| Reduction stops at quiescence or reports reduction-limit exhaustion | Quiescent/open-name loop in `test-rhocalc`; `tests/rhocalc_run/paper_divergence_self_recreates_reduction_limit.expected` | Enforced | Public `--lang rhocalc` contract only. |
| Scheduler choice is explicit and documented | `test-help-flags`, public `--rho-scheduler` help text | Enforced | No hidden scheduler surface. |
| No accepted CLI flag may be hidden from `--help` | `test-help-flags` exact parser-vs-help equality check | Enforced | Grep audits are supplementary only. |

Architectural note:

- The separate boundary “all runtime state lives in the residual process;
  indexes/caches are rebuildable views” is tracked elsewhere in the plan and
  code review, but it is not currently a standalone black-box runtime test.
