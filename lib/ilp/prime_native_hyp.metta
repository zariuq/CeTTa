; Proof-relevant execution for native Prime `hyp` programs.
;
; Quotation keeps a hypothesis program as data while these language-authored
; equations interpret it.  Both the lowercase authored form and the canonical
; DTT application produced by typed erasure feed the same relational helpers.
; The generic evaluator and NIK waist have no `hyp` opcode: a vocabulary
; supplies `hyp:carrier` and `hyp:meaning`, and the resulting relation remains
; nondeterministic.  Sort codes remain distinct from the carrier types they
; denote.
; Prime's language-owned native calculus may compile the exact checked
; proof-program structure without redefining these primitive meanings.  The
; optional typing presentation lives in `prime_native_hyp_types.metta`;
; attaching it cannot redefine these equations.

(= (hyp:run
     (quote
       (hyp:primitive $sorts $primitives
         $source-sort $target-sort $symbol))
     $input)
   (hyp:run-primitive
     $source-sort $target-sort $symbol $input))

(= (hyp:run
     (quote
       (App (App (App (App (App hyp:primitive $sorts)
                           $primitives) $source-sort) $target-sort) $symbol))
     $input)
   (hyp:run-primitive
     $source-sort $target-sort $symbol $input))

(= (hyp:run-primitive $source-sort $target-sort $symbol $input)
   (let $relation
        (hyp:meaning $source-sort $target-sort $symbol)
     (let (rel:edge $target $evidence)
          ($relation $input)
       (hyp:edge $target
         (hyp:primitive-proof $symbol $evidence)))))

(= (hyp:run
     (quote
       (hyp:chain $sorts $primitives
         $source-sort $middle-sort $target-sort $earlier $later))
     $input)
   (hyp:run-chain (quote $earlier) (quote $later) $input))

(= (hyp:run
     (quote
       (App (App (App (App (App (App (App hyp:chain $sorts)
         $primitives) $source-sort) $middle-sort) $target-sort)
         $earlier) $later))
     $input)
   (hyp:run-chain (quote $earlier) (quote $later) $input))

(= (hyp:run-chain $earlier $later $input)
   (let (hyp:edge $middle $earlier-proof)
        (hyp:run $earlier $input)
     (let (hyp:edge $target $later-proof)
          (hyp:run $later $middle)
       (hyp:edge $target
         (hyp:chain-proof $middle $earlier-proof $later-proof)))))
