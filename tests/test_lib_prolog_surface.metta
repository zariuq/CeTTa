!(import! &self lib_prolog)

!(import-prolog-functions-from-file
  "tests/support/foreign_prolog_simple.pl"
  (prolog_probe_echo prolog_probe_fact prolog_probe_dyn))

!(assertEqual (prolog_probe_echo hello) hello)
!(assertEqual (prolog-call (Predicate prolog_probe_fact alpha)) True)
!(assertEqual (prolog-call (Predicate prolog_probe_fact beta)) False)
!(assertEqual (prolog-call (Predicate prolog_probe_dyn foo)) False)
!(assertEqual (prolog-assertz (Predicate prolog_probe_dyn foo)) True)
!(assertEqual (prolog-call (Predicate prolog_probe_dyn foo)) True)
!(assertEqual (prolog-retract (Predicate prolog_probe_dyn foo)) True)
!(assertEqual (prolog-call (Predicate prolog_probe_dyn foo)) False)
