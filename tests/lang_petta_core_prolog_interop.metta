!(pl-consult "tests/support/foreign_prolog_simple.pl")
!(import_prolog_function prolog_probe_echo)
!(assertEqual (prolog_probe_echo 7) 7)
!(assertEqual (callPredicate (Predicate prolog_probe_fact alpha)) true)
