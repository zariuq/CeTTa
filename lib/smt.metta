; Satisfiability modulo the theories of equality with uninterpreted
; functions and of linear real arithmetic, on the asp library's solver.
;   (smt:solve CLAUSES)   CLAUSES a list of (clause (P ...) (N ...)), each
;                         holding when some atom P is true or some atom N false,
;                         as the sat library reads them, over atoms of four
;                         kinds: (eq S T), the equality of the ground terms S
;                         and T; a bound (le A B), (lt A B), (ge A B) or
;                         (gt A B) over arithmetic terms; a predicate's
;                         application (p T ...), which holds exactly when it
;                         equals the reserved truth term, so equal arguments
;                         give it equal truth; and any other atom, a
;                         proposition.  A term is a symbol, a value or an
;                         application (f T ...).  An arithmetic term is an
;                         integer, a real (a symbol), (add A ...), (sub A ...),
;                         (mul A ...) with at most one factor not constant, or
;                         (div A C) with C a nonzero constant; no real is also a
;                         term's symbol.
;                         (model (A ...)): the atoms true in a model, in order of
;                         first occurrence, and with reals (model (A ...)
;                         ((X V) ...)), each real's exact value, an integer or
;                         (div N D); or (unsat (certificate STEP ...))
; A certificate's steps are (rup ID (LIT ...) (HINT ...)), as the sat library's;
; (euf ID (LIT ...)), a clause that congruence closure entails: the
; equalities its falsification makes true entail one it makes false; and
; (lra ID (LIT ...) (W ...)), a clause whose falsification asserts bounds
; that, weighed by the nonnegative W, sum to a contradiction (Farkas).  Every
; answer is checked before it is given: a model against every clause, by
; congruence closure over its atoms and by exact evaluation of its bounds, a
; refutation by the certificate checker, which recomputes the closure for
; each EUF step and the weighted sum for each LRA step.
;   (smt:solve-text TEXT) an SMT-LIB script (QF_UF, QF_LRA or QF_UFLRA) as a string, read
;                         by the lane's reader: declarations (sorts, Bool and
;                         Real), define-fun, assert and the Boolean
;                         connectives, ite, = and distinct, let and
;                         annotations; over reals + - * / by constants,
;                         <= < >= >, decimals read exactly and term ite through
;                         a fresh real; uninterpreted functions over reals
;                         (QF_UFLRA) by delayed theory combination: each pair of
;                         terms both theories read (real applications in
;                         arithmetic, real or numeral arguments, arithmetic
;                         arguments through a fresh constant) gets an equality
;                         the two theories tie together, and the search
;                         decides how they fall into classes; Int constants
;                         (QF_LIA, QF_LIRA) by branch and bound over the real
;                         relaxation, a value that is not an integer splitting
;                         the problem at its floor: a refutation is the tree
;                         (split X K CERT CERT) of checked certificates, X <= K
;                         and X >= K + 1 on its branches, and past a depth of 64
;                         the answer is unknown;
;                         sat or (unsat (certificate STEP ...));
;                         with one (minimize T) or (maximize T), T a linear
;                         real term: (optimum V CERT), (infimum V CERT) or
;                         (supremum V CERT) when V is only approached,
;                         unbounded, or (unsat CERT), CERT refuting a better
;                         value as smt:minimize's does; several objectives are
;                         lexicographic, as Z3's default: (lexicographic (STAGE
;                         ...)), each stage optimized with the earlier optima
;                         held, ending at the first stage that is unbounded or
;                         only approached, each stage's CERT refuting a better
;                         value with the earlier optima held
;   (smt:minimize CLAUSES TERM) and (smt:maximize CLAUSES TERM): the least or
;                         greatest value of an arithmetic term over the models,
;                         by linear search, each model's reals moved to where
;                         the term is best under its bounds:
;                         (optimum V (A ...) VALUES CERT), (infimum V ...) or
;                         (supremum V ...) when V is approached but not
;                         attained, (unbounded (A ...) VALUES), or
;                         (unsat CERT).  CERT refutes the clauses with one more,
;                         (clause ((lt TERM V)) ()) for a least value (le when
;                         not attained; gt and ge for a greatest), its atom
;                         numbered after the others: no model does better.
(: __cetta_lib_smt_minimize (-> Atom Atom Atom))
(: __cetta_lib_smt_maximize (-> Atom Atom Atom))
(: smt:minimize (-> Atom Atom Expression))
(: smt:maximize (-> Atom Atom Expression))
(= (smt:minimize $clauses $term) (__cetta_lib_smt_minimize $clauses $term))
(= (smt:maximize $clauses $term) (__cetta_lib_smt_maximize $clauses $term))
(: __cetta_lib_smt_solve (-> Atom Atom))
(: __cetta_lib_smt_solve_text (-> Atom Atom))
(: smt:solve (-> Atom Expression))
(: smt:solve-text (-> Atom Atom))
(= (smt:solve $clauses) (__cetta_lib_smt_solve $clauses))
(= (smt:solve-text $text) (__cetta_lib_smt_solve_text $text))
