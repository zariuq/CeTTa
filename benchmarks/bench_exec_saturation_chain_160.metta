; Transitive closure of a 160-node chain by exec:saturate (HE, --profile
; extended): 158 rounds, 12720 reach facts.  Semi-naive rounds derive each fact
; once, so the chain sizes 160 and 320 differ by the output's 4x; with
; CETTA_EXEC_SATURATION_REFERENCE=1 the authored rounds re-derive every fact
; in every round.
!(import! &self lib_exec)
(= (chain-edges $g $i $n)
   (if (< $i $n)
       (let $next (+ $i 1)
          (let $_ (add-atom $g (edge $i $next)) (chain-edges $g $next $n)))
       ()))
!(bind! &g (new-space))
!(chain-edges &g 1 160)
!(exec:saturate &g ((exec base (, (edge $x $y)) (, (reach $x $y)))
                    (exec step (, (reach $x $y) (edge $y $z)) (, (reach $x $z)))))
!(let $reach (collapse (match &g (reach $x $y) $x)) (size-atom $reach))
