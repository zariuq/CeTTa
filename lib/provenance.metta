; Why-provenance of derivations in rule programs.
;
; A program is data:
;   (fact PRED A ...)                          a base fact
;   (rule LABEL (lit HEAD V ...) (LIT ...))    the head holds when the body does;
;                                              each LIT is (lit PRED V ...), and
;                                              each V a variable index of the rule
; A predicate is its name and its arity, from 1 to 8, as in Prolog.  A
; predicate that heads a rule is derived and has no facts; every other
; predicate is base.  Labels, predicates and arguments are ground terms; a
; rule's head variables occur in its body.
;
;   (provenance:supports FACTS RULES GOALS BOUND)
;
; GOALS holds (goal PRED A ...), each A ground or a variable for any.  The
; answer is the sorted list of the supports of the goals' derivations, each the
; sorted list of the labels the derivation uses, once:
;   (steps K)   derivations of at most K rule instances;
;   (labels K)  derivations of any size that use at most K distinct labels.
; A support is the provenance of a derivation in the why-semiring: the union of
; its rules' labels; alternatives join as a set.  With (steps K) a derivation
; is built from strictly smaller ones, so no fixpoint is needed; with
; (labels K) the families are a least fixpoint, finite because supports are
; bounded sets of the program's labels.
(: __cetta_lib_provenance_supports (-> Atom Atom Atom Atom Atom))
(: provenance:supports (-> Atom Atom Atom Atom Expression))
(= (provenance:supports $facts $rules $goals $bound)
   (__cetta_lib_provenance_supports $facts $rules $goals $bound))
; (provenance:supports-each FACTS RULES ((GOAL ...) ...) BOUND) computes once
; and answers one such list per goal group (the examples of a problem, say).
(: __cetta_lib_provenance_supports_each (-> Atom Atom Atom Atom Atom))
(: provenance:supports-each (-> Atom Atom Atom Atom Expression))
(= (provenance:supports-each $facts $rules $groups $bound)
   (__cetta_lib_provenance_supports_each $facts $rules $groups $bound))
