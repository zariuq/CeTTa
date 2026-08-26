; A depth-bounded chainer is an ordinary authored relation over an explicitly
; ordered Space.  Prime's kernel, not the search relation, checks its result.
!(import! &self ../../experiments/inference_guidance/authored_frontier_chainer.metta)

(: proof:A (u 0))
(: proof:B (u 0))
(: proof:C (u 0))
(: proof:D (u 0))

(: proof:a proof:A)
(: proof:a-to-b (-> proof:A proof:B))
(: proof:b-to-c (-> proof:B proof:C))
(: proof:c-to-d (-> proof:C proof:D))

(: proof:P (-> proof:A (u 0)))
(: proof:indexed-base
   (-> (x : proof:A) (proof:P x)))
(: proof:indexed-step
   (-> (x : proof:A)
       (evidence : (proof:P x))
       (proof:P x)))

(= (proof:ranked-declarations)
   ((: (proof:constructor proof:c-to-d ()) (-> proof:C proof:D))
    (: (proof:constructor proof:b-to-c ()) (-> proof:B proof:C))
    (: (proof:constructor proof:a-to-b ()) (-> proof:A proof:B))
    (: (proof:constructor proof:a ()) proof:A)))

!(let $_ (bind! &proof:ordered-declarations (new-space queue))
   (let $_
        (proof:load-reference-space
          &proof:ordered-declarations
          (proof:ranked-declarations))
     (chain
       (once
         (proof:derive
           &proof:ordered-declarations
           (S (S (S Z)))
           proof:D))
       $tree
       (chain (proof:reify $tree) $quoted-proof
         (unify $quoted-proof (quote $proof)
           (type:check $proof proof:D)
           False)))))

; The same goal has no proof within depth two.
!(let $_ (bind! &proof:shallow-declarations (new-space queue))
   (let $_
        (proof:load-reference-space
          &proof:shallow-declarations
          (proof:ranked-declarations))
     (case
       (once
         (proof:derive
           &proof:shallow-declarations
           (S (S Z))
           proof:D))
       (($tree (proof:unexpected $tree)))
       proof:not-found)))

; The kernel independently rejects a composition with the wrong middle type.
!(type:check
   (proof:c-to-d (proof:a-to-b proof:a))
   proof:D)

; Dependent indices are ordinary constructor parameters, not a second
; relational namespace or a native inhabitation command.
!(let $_ (bind! &proof:indexed-declarations (new-space queue))
   (let $_
        (proof:load-reference-space
          &proof:indexed-declarations
          ((: (proof:constructor proof:indexed-step (proof:a))
              (-> (app proof:P proof:a) (app proof:P proof:a)))
           (: (proof:constructor proof:indexed-base (proof:a))
              (app proof:P proof:a))))
     (chain
       (once
         (proof:derive
           &proof:indexed-declarations
           (S Z)
           (app proof:P proof:a)))
       $tree
       (chain (proof:reify $tree) $quoted-proof
         (unify $quoted-proof (quote $proof)
           (type:check $proof (proof:P proof:a))
           False)))))
