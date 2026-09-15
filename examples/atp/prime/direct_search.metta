; Example: direct proof search over ordinary Prime typing declarations.
;
; This module is an untrusted proof producer.  It reads declarations of the
; form (: proof proposition) and rules marked by (chaining-rule rule), builds
; candidate proof terms with ordinary MeTTa matching/unification, and admits a
; result only after check-type accepts it exactly or structurally.

!(import! &self list)

; Split a nonempty expression into its initial segment and last element.
(= (atp:direct:split-last $items)
   (if (== (size-atom $items) 1)
       (ATP.Split () (car-atom $items))
       (let* (($head (car-atom $items))
              ($tail (cdr-atom $items))
              ((ATP.Split $initial $last)
               (atp:direct:split-last $tail)))
         (ATP.Split (cons-atom $head $initial) $last))))

; Interpret one marked typing declaration as an applicable rule.  Ordinary
; unify supplies the rule instance; no ATP-specific unifier is involved.
(= (atp:direct:arrow-candidate $goal $rule $type)
   (if (== (get-metatype $type) Expression)
       (if (== (car-atom $type) ->)
           (let (ATP.Split $premises $conclusion)
                (atp:direct:split-last (cdr-atom $type))
             (unify $conclusion $goal
                    (ATP.Rule $rule $premises $goal)
                    (empty)))
           (empty))
       (empty)))

(= (atp:direct:rule-candidate $knowledge $goal)
   (match $knowledge (chaining-rule $rule)
     (match $knowledge (: $rule $type)
       (atp:direct:arrow-candidate $goal $rule $type))))

(= (atp:direct:closed-items () ) True)
(= (atp:direct:closed-items $items)
   (let ($head $tail) (decons-atom $items)
     (and (atp:direct:closed $head)
          (atp:direct:closed-items $tail))))

(= (atp:direct:closed $atom)
   (let $meta (get-metatype $atom)
     (if (== $meta Variable)
         False
         (if (== $meta Expression)
             (atp:direct:closed-items $atom)
             True))))

(= (atp:direct:fact-instance $knowledge $goal $proof $type)
   (if (atp:direct:closed $type)
       (unify $type $goal (: $proof $goal) (empty))
       (match $knowledge (type-scheme $proof)
         (unify $type $goal (: $proof $goal) (empty)))))

(= (atp:direct:fact $knowledge $goal)
   (match $knowledge (: $proof $type)
     (atp:direct:fact-instance $knowledge $goal $proof $type)))

(= (atp:direct:prove-premises $knowledge () $depth)
   (ATP.Proofs ()))

(= (atp:direct:prove-premises $knowledge $premises $depth)
   (let ($premise $rest) (decons-atom $premises)
     (let* (((: $proof $premise)
             (atp:direct:prove-judgment $knowledge $premise $depth))
            ((ATP.Proofs $proofs)
             (atp:direct:prove-premises $knowledge $rest $depth)))
       (ATP.Proofs (cons-atom $proof $proofs)))))

(= (atp:direct:use-rule $knowledge $goal $depth)
   (let (ATP.Rule $rule $premises $goal)
        (atp:direct:rule-candidate $knowledge $goal)
     (let (ATP.Proofs $proofs)
          (atp:direct:prove-premises $knowledge $premises (- $depth 1))
       (: (cons-atom $rule $proofs) $goal))))

(= (atp:direct:prove-judgment $knowledge $goal $depth)
   (if (<= $depth 0)
       (atp:direct:fact $knowledge $goal)
       (superpose
         ((atp:direct:fact $knowledge $goal)
          (atp:direct:use-rule $knowledge $goal $depth)))))

(= (atp:direct:prove $knowledge $goal $depth)
   (atp:direct:prove-judgment $knowledge $goal $depth))

; Gradual, rejected, or otherwise non-exact outcomes are not certificates.
(= (atp:direct:certify $proof $goal $verdict)
   (case $verdict
     (((he-accept (exact $checked))
       (ATP.Checked $proof $goal (exact $checked)))
      ((he-accept (structural $checked))
       (ATP.Checked $proof $goal (structural $checked)))
      ($other (empty)))))

(= (atp:direct:prove-checked $knowledge $goal $depth $fuel)
   (let (: $proof $goal)
        (atp:direct:prove-judgment $knowledge $goal $depth)
     (atp:direct:certify
       $proof $goal (check-type $knowledge $proof $goal $fuel))))

; Committed first-result convenience over the same proof stream.  This is an
; ordinary MeTTa traversal choice, not a distinct proof calculus.
(= (atp:direct:first-checked $knowledge $goal $depth $fuel)
   (select (atp:direct:prove-checked $knowledge $goal $depth $fuel)))
