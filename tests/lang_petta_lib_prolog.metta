!(import! &self lib/lib_petta/lib_import)

!(import_prolog_functions_from_file
  "tests/support/foreign_prolog_simple.pl"
  (prolog_probe_echo prolog_probe_fact prolog_probe_dyn))

!(assertEqual (prolog_probe_echo hello) hello)
!(assertEqual (callPredicate (Predicate prolog_probe_fact alpha)) true)
!(assertEqual (callPredicate (Predicate prolog_probe_fact beta)) false)
!(assertEqual (callPredicate (Predicate prolog_probe_dyn foo)) false)
!(assertEqual (assertzPredicate (Predicate prolog_probe_dyn foo)) true)
!(assertEqual (callPredicate (Predicate prolog_probe_dyn foo)) true)
!(assertEqual (retractPredicate (Predicate prolog_probe_dyn foo)) true)
!(assertEqual (callPredicate (Predicate prolog_probe_dyn foo)) false)
