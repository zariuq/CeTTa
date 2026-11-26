; Registered differences from SWI-PeTTa that a Prolog goal shows: the cyclic
; terms SWI's own unification builds through translatePredicate.  The
; entries are those of swi_differences.metta, whose numbering they keep.
; 11. CeTTa reads a cyclic term that arrives from Prolog as the term it is,
;     but its own bindings keep terms finite: binding a variable of such a
;     term to a term that contains it again has no answer.
; SWI-PeTTa: ok
!(let* (($_ (translatePredicate (= $c (f $x $c)))) ($x (g $c))) ok)
; 12. A `let` pattern headed by a variable meets its value as a pattern.
;     SWI-PeTTa's translator runs such a pattern as a run-time dispatch,
;     whose data answer may not be cyclic, and runs it before the let's
;     value when that value is computed but after it when the value is a
;     variable: the query below has no answer there, while the same pattern
;     over (index-atom $c 2), the same term, has one.  CeTTa answers both.
;     Recorded: Oruži, 2026-09-29.  The contract is Zar's to set.
; SWI-PeTTa: no answer.
!(let $_ (translatePredicate (= $c (a b $c))) (let ($x $y $z) $c (== $z $c)))
; 13. Writing a term that is not a finite tree raises
;     representation_error(cyclic_term).  SWI-PeTTa's writer never ends on
;     such a term and runs out of stack, so what its catch gives is a
;     resource_error whose context describes the host's stack.  Uncaught,
;     both stop the run with status 2 (tests/petta/rational_terms_text).
;     Recorded: Oruži, 2026-09-29.  The contract is Zar's to set.
; SWI-PeTTa: (Error (resource_error stack) (dict stack_overflow ...))
!(let $_ (translatePredicate (= $c (f $c))) (catch (repr $c)))
; 14. A call uses the declarations present when it starts, including a
;     call whose head is resolved from a variable. SWI's translated caller
;     retains its earlier argument/guard plan: a later direct call sees
;     one signature here, while eval at the same point sees both. A
;     let-bound head can omit the declaration's guards entirely in SWI.
;     These are reference translation bugs; refreshing the call plan on
;     declaration-generation changes is a repair candidate.
;     Decided: Zar, 2026-10-01, preserve live declarations.
; A head held by a variable or a parameter, foldl's step included, also
; meets the declaration's result check.  Stable-fixed SWI-PeTTa c3adb86
; (2026-11-05) answers the unchecked result on these routes, while the
; direct call (visibility-step 1 0) answers nothing in both engines.
; 15. Rows that repeat one declaration are one declaration.  A call answers
;     once for them in both implementations; SWI's get-type lists every row,
;     CeTTa's lists the declaration once, so the version a typed relation
;     keeps does not depend on a repeated row.
;     Decided: Zar, 2026-11-05 ("duplicated declarations should probably be
;     idempotent. Different declarations are another matter").
; 18. A constructor field declared _ is unchecked by get-type, as one
;     declared %Undefined% is, and as an argument declared _ is unguarded in
;     a typed call: _ reads the same to the call protocol and to the type
;     relation.  SWI-PeTTa's get-type reads it there as the type named _, so
;     only a value declared _ fills the field, and (LooseBox "w") is typed by
;     its tuple alone.  The reference's own comment on that check reads "As in
;     typed calls, an argument declared %Undefined% is not checked", and its
;     typed calls leave _ unguarded as they leave %Undefined%; the check tests
;     %Undefined% alone.  Decided by the coherence rule (Zar, 2026-11-06).
; SWI-PeTTa: ((-> _ LooseBoxT) String), ()
(= (dt-p c0 c1) True)
(= (dt-p c1 c1) True)
(= (dt-t $x $y) (let True (dt-i $x $z) (dt-i $z $y)))
(= (dt-i $x $y) (dt-t $y $x))
(= (dt-i $x $y) (dt-p $y $x))
; 26. static-import! of an operand that names no space raises the term
;     add-atom raises for it, once the file is found: every atom of a
;     static file is an expression, so (Error (type_error atom X) (context
;     (: system (/ =.. 2)) _)), and instantiation_error in that context for
;     an unbound operand; nothing is loaded.  SWI-PeTTa's static-import!
;     never reads its operand: it names the facts' predicate by the
;     operand's printed text, so 42, (, &a &b) or an unbound operand gets
;     the file's facts under '42', '[,,&a,&b]' or a variable's name, and the
;     call answers true; a match of 42 then raises.
;     Decided 2026-11-26 by P-ERR-5's law: one law for every space write.
; SWI-PeTTa: true, true, true
!(import! &self (library lib_import))
!(catch (static-import! 42 fixtures/static_import_edges))
!(catch (static-import! (, &a &b) fixtures/static_import_edges))
!(catch (static-import! $operand fixtures/static_import_edges))
