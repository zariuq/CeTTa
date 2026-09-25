!(import! &self ../../../lib/lib_tptp.metta)

(= (boundary:identity $value) $value)
(= (boundary:wrap $value) (BoundaryValue $value))
(= (boundary:unwrap (BoundaryValue $value)) $value)
(= (collision $x) BoundaryMustNotExecute)
(= (fof $name $role $formula) RecordMustNotExecute)
(= (boundary:formula $records)
   (let $record (car-atom $records)
     (let (TPTP:RecordFormula $formula) (tptp:record-formula $record)
       $formula)))

; Input names may coincide with builtins or equations in the receiving space.
; They remain data through reading, ordinary wrappers and printing.
!(println! (tptp:read-text "fof(a,axiom,and(X,n0))."))
!(println! (boundary:identity
             (tptp:read-text "fof(a,axiom,and(X,n0)).")))
!(println! (boundary:unwrap (boundary:wrap
             (tptp:read-text "fof(a,axiom,and(X,n0))."))))
!(println! (tptp:print
             (tptp:read-text "fof(a,axiom,and(X,n0)).")))
!(println! (tptp:print
             (tptp:read-text "fof(a,axiom,collision(X)).")))
!(println! (tptp:print
             (tptp:read-text "fof(a,axiom,'+'(1,2)).")))
!(println! (tptp:print
             (tptp:read-text "fof(a,axiom,let(X,p,q)).")))

; A collection's later elements must be protected too, not just its head.
(= (boundary:roundtrip $records)
   (let $printed (tptp:print $records)
     (let $reparsed (tptp:read-text $printed)
       (BoundaryRoundTrip (== $records $reparsed)
         (tptp:record-statistics $reparsed)))))

!(println! (boundary:roundtrip
  (tptp:read-text "fof(a,axiom,and(X,n0)).fof(b,axiom,collision(X)).fof(c,axiom,let(X,p,q)).fof(d,axiom,or(not(X),quote(Y))).fof(e,axiom,'+'(1,2)).")))
!(println! (tptp:print
  (tptp:read-text "fof(a,axiom,and(X,n0)).fof(b,axiom,collision(X)).fof(c,axiom,let(X,p,q)).fof(d,axiom,or(not(X),quote(Y))).fof(e,axiom,'+'(1,2)).")))

; Negative control: ordinary explicit evaluation still invokes user code.
!(println! (collision X))
!(println! (fof explicit axiom p))

!(let $formula
      (boundary:formula (tptp:read-text "fof(a,axiom,and(X,n0))."))
   (let $kind (tptp:expression-kind $formula)
     (let $children (tptp:expression-children $formula)
       (let $free (tptp:free-variables $formula)
         (println! (BoundaryInspection $kind $children $free))))))
!(let $formula
      (boundary:formula (tptp:read-text "fof(a,axiom,collision(X))."))
   (let $kind (tptp:expression-kind $formula)
     (let $children (tptp:expression-children $formula)
       (let $free (tptp:free-variables $formula)
         (println! (BoundaryInspection $kind $children $free))))))

; A data-owned call still evaluates its strict handle operand.
!(let $program
      (langdef:load-deterministic-equations
        (LangDef:EquationSources
          "langdef/bnf/plain_bnf_denotation_v1.metta"
          "langdef/tptp/official_records_to_text_v1.metta"))
   (let $records (tptp:read-text "fof(a,axiom,collision(X)).")
     (let $_ (println!
       (__cetta_lib_deterministic_equations_run_data
         (boundary:identity $program)
         (LangDef:DeterministicCall tptp-print-v1:document $records)))
       (langdef:close-deterministic-equations $program))))
