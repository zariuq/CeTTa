; Arthur's model01 ranks the complete strict-prefix corpus for the held-out
; `biluk` goal.  An explicit top-24 readout populates an ordered queue Space;
; the authored relation searches that queue without changing its members.

!(import! &self ./authored_frontier_chainer.metta)
!(import! &self ./william_ranker.py)

(: smm:formula (u 0))
(: smm:imp (-> smm:formula smm:formula smm:formula))
(: smm:iff (-> smm:formula smm:formula smm:formula))
(: smm:not (-> smm:formula smm:formula))
(: smm:holds (-> smm:formula (u 0)))

(: smm:ph smm:formula)
(: smm:ps smm:formula)
(: smm:ch smm:formula)

!(import! &self ./generated_biluk_prime_corpus.metta)
(= (biluk:goal)
   (app smm:holds
     (smm:iff
       (smm:iff smm:ph smm:ps)
       (smm:iff
         (smm:iff smm:ch smm:ps)
         (smm:iff smm:ph smm:ch)))))

; WILLIAM ranks candidates once at the problem boundary.  It has no checking
; authority: the authored relation returns quoted proof data, and Prime's type
; kernel independently admits or rejects that term.
!(chain
   (william:rank-declarations
     (app smm:holds
       (smm:iff
         (smm:iff smm:ph smm:ps)
         (smm:iff
           (smm:iff smm:ch smm:ps)
           (smm:iff smm:ph smm:ch))))
     (biluk:candidate-declarations))
   $ranked-declarations
   (let $_ (bind! &biluk:ranked-space (new-space queue))
     (let $_
       (proof:load-reference-prefix
            &biluk:ranked-space
            24
            $ranked-declarations)
       (chain
         (once
           (proof:derive
             &biluk:ranked-space (S (S (S (S Z))))
             (app smm:holds
               (smm:iff
                 (smm:iff smm:ph smm:ps)
                 (smm:iff
                   (smm:iff smm:ch smm:ps)
                   (smm:iff smm:ph smm:ch))))))
         $tree
         (chain (proof:reify $tree) $quoted-proof
           (unify $quoted-proof (quote $proof)
             (proof:answer
               $quoted-proof
               (type:check
                 $proof
                 (smm:holds
                   (smm:iff
                     (smm:iff smm:ph smm:ps)
                     (smm:iff
                       (smm:iff smm:ch smm:ps)
                       (smm:iff smm:ph smm:ch))))))
             (proof:invalid-proof-data $quoted-proof)))))))
