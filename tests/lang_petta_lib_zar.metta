!(import! &self lib/lib_zar)

!(bind! &counter (new-state 0))
!(assertEqual (get-state &counter) 0)
!(let $_ (adjust-state-additive &counter 5) ())
!(assertEqual (get-state &counter) 5)
!(import_prolog_functions_from_file_nocheck
  "tests/support/foreign_prolog_simple.pl"
  (prolog_probe_echo))
!(assertEqual (prolog_probe_echo 7) 7)
