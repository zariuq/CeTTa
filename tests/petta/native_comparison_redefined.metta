; A comparison is read as a test of a match's rows only while it is an
; operation of the session: != is PeTTa's library function, and a file the
; program consults may redefine it, after which its spellings ask the
; redefinition, as the ordinary path does.
!(bind! &b (new-space set))
!(bind! &c (new-space set))
!(add-atom &b (seen 1))
!(add-atom &b (seen 2))
(= (present $x) (if (!= () (collapse (once (match &b (seen $x) t)))) yes no))
!(present 1)
!(present 3)
!(size-atom (collapse (match &b (seen $x) (if (!= (collapse (match &c (got $x) t)) ()) (empty) (add-atom &c (got $x))))))
!(sort-atom (collapse (match &c (got $x) $x)))
!(callPredicate (Predicate (quote (consult "tests/petta/fixtures/native_comparison_redefined.pl"))))
!(!= 1 0)
!(present 1)
!(present 3)
!(size-atom (collapse (match &b (seen $x) (if (!= (collapse (match &c (got $x) t)) ()) (empty) (add-atom &c (redone $x))))))
!(sort-atom (collapse (match &c (redone $x) $x)))
