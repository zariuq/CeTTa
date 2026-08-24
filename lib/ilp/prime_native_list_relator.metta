; Proof-relevant relational execution for the native Prime `list` family.
;
; `map-rel:run` lifts any relation view whose ordinary `rel:apply` produces
; `(rel:edge target evidence)`.  Each relation representation authors that
; operation explicitly; logical application `relation source target` remains
; the evidence fibre and is never overloaded as an answer stream.
; It preserves the ordered answer bag: two element proofs yielding the same
; target produce two distinct list proofs.
; The typed family and constructors live in the optional typing presentation;
; these equations remain the raw relational meaning when no native
; realization has been admitted.

(= (map-rel:run $relation $source-list)
   (if (== $source-list ())
       (map-rel:edge () (map-rel:nil-proof))
       (if (== (get-metatype $source-list) Expression)
           (let ($source-head $source-tail) (decons-atom $source-list)
             (let (rel:edge $target-head $head-evidence)
                  (rel:apply $relation $source-head)
               (let (map-rel:edge $target-tail $tail-evidence)
                    (map-rel:run $relation $source-tail)
                 (map-rel:edge
                   (cons-atom $target-head $target-tail)
                   (map-rel:cons-proof
                     $source-head $target-head
                     $head-evidence $tail-evidence)))))
           (superpose ()))))

; Endpoint-directed execution is the same relation with the target retained
; as an input pattern.  Failure to find evidence remains relational failure;
; it is not a type refutation or an authority verdict.
(= (map-rel:check $relation $source-list $target-list)
   (if (== $source-list ())
       (if (== $target-list ())
           (map-rel:nil-proof)
           (superpose ()))
       (if (== $target-list ())
           (superpose ())
           (if (and
                 (== (get-metatype $source-list) Expression)
                 (== (get-metatype $target-list) Expression))
               (let ($source-head $source-tail) (decons-atom $source-list)
                 (let ($target-head $target-tail) (decons-atom $target-list)
                   (let (rel:edge $target-head $head-evidence)
                        (rel:apply $relation $source-head)
                     (let $tail-evidence
                          (map-rel:check
                            $relation $source-tail $target-tail)
                       (map-rel:cons-proof
                         $source-head $target-head
                         $head-evidence $tail-evidence)))))
               (superpose ())))))
