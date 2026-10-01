; Query-release behavior across the PeTTa/Prolog boundary (2026-09-30).
;
; The fixture source (fixtures/libpl_query_release.pl) gives setup_call_cleanup
; generators whose teardown cleanup can throw and one-argument raisers the
; import convention can call.  The boundary's release paths are exercised by
; value, not by coincidence: exception terms cross as their exact SWI values,
; so PeTTa catch sees the same term the reference's catch sees.  Lifecycle
; negatives that no MeTTa program reaches (a teardown fault at release with
; unread solutions, PL_S_NOT_INNER ordering) belong to the native gate
; tests/support/check_petta_libpl_query_release_v1.c: MeTTa programs exhaust
; their solutions inside the goal, where SWI reports a teardown failure as
; the goal's own exception (S3 below).

!(callPredicate (Predicate (quote (consult "tests/petta/fixtures/libpl_query_release.pl"))))

!(import_prolog_function qrel_gen)
!(import_prolog_function qrel_raise_body)
!(import_prolog_function qrel_raise_both)

; S1: normal close path — quiet nondeterministic generator, all answers,
; twice (independent queries stay independent).
!(collapse (qrel_gen quiet))
!(collapse (qrel_gen quiet))

; S2: a one-shot body fault crosses as its value (a Prolog /N predicate is
; a MeTTa zero-input function — (qrel_raise_body) is its application); a
; later quiet call still answers (no pending-exception poisoning after
; handled errors).
!(catch (qrel_raise_body))
!(collapse (qrel_gen quiet))

; S3: a teardown cleanup that raises as the generator is exhausted propagates
; as the goal's own exception — PeTTa catch sees its exact term.
!(catch (collapse (qrel_gen throw)))

; S4: when the body already raised, the teardown's cleanup cannot clobber it:
; the body error dominates (SWI runs the teardown during the body's own
; exception and loses the teardown fault by design).  Then a clean call.
!(catch (qrel_raise_both))
!(collapse (qrel_gen quiet))

; S5: cut-path interop stays honest through first-only releases: assert,
; read, retract, read again, then a fresh generator call.
!(assertaPredicate (Predicate (qrel_state one)))
!(collapse (callPredicate (Predicate (qrel_state $what))))
!(retractPredicate (Predicate (quote (qrel_state one))))
!(collapse (callPredicate (Predicate (qrel_state $what))))
!(collapse (qrel_gen quiet))
