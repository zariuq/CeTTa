; Nil Geisweiller's bounded backward-via-forward Hilbert search, followed by
; independent Prime DTT elaboration and exact type checking of every emitted
; proof occurrence.
;
; The search producer remains the original four-clause relation.  Its compact
; I/mpi continuation term is lowered mechanically to an ordinary Hilbert
; modus-ponens tree.  hilbert:infer reconstructs each axiom instantiation from
; that fixed tree and emits explicit dependent arguments; check-type remains
; the sole acceptance boundary.

(: p Type)
(: q Type)
(: r Type)
(: imp (-> Type Type Type))
(: neg (-> Type Type))

(: ax1-explicit
   (-> (: $ph Type) (: $ps Type)
       (imp $ph (imp $ps $ph))))

(: ax2-explicit
   (-> (: $ph Type) (: $ps Type) (: $ch Type)
       (imp
         (imp $ph (imp $ps $ch))
         (imp (imp $ph $ps) (imp $ph $ch)))))

(: ax3-explicit
   (-> (: $ph Type) (: $ps Type)
       (imp
         (imp (neg $ph) (neg $ps))
         (imp $ps $ph))))

(: mp (-> (imp $ph $ps) $ph $ps))
(chaining-rule mp)

; Original bounded BFC producer.
(= (ofc $depth $hypcnt $tgt (: $x $a))
   (if (< 0 $hypcnt)
       (if (and (< 0 $depth) (<= $hypcnt $depth))
           (ofc-rec (- $depth 1) (- $hypcnt 1) $tgt (: $x $a))
           (empty))
       (let (: $x $a) $tgt (: $x $a))))

(= (ofc-rec $depth $hypcnt $tgt
            (: $f (-> (imp $ph (imp $ps $ph)) $b)))
   (ofc $depth $hypcnt $tgt (: ($f ax1) $b)))

(= (ofc-rec $depth $hypcnt $tgt
            (: $f
               (->
                 (imp
                   (imp $ph (imp $ps $ch))
                   (imp (imp $ph $ps) (imp $ph $ch)))
                 $b)))
   (ofc $depth $hypcnt $tgt (: ($f ax2) $b)))

(= (ofc-rec $depth $hypcnt $tgt
            (: $f
               (->
                 (imp (imp (neg $ph) (neg $ps)) (imp $ps $ph))
                 $b)))
   (ofc $depth $hypcnt $tgt (: ($f ax3) $b)))

(= (ofc-rec $depth $hypcnt $tgt (: $f (-> $b $c)))
   (ofc
     $depth (+ $hypcnt 2) $tgt
     (: (mpi $f) (-> (imp $a $b) (-> $a $c)))))

(= (obfc $depth (: $x $a))
   (ofc $depth 1 (: $x $a) (: I (-> $a $a))))

; Flatten the applicative continuation spine into its pending arguments.
(= (bfc:lower $term) (bfc:lower-spine $term ()))

(= (bfc:lower-spine $term $arguments)
   (if (== $term I)
       (bfc:lower-I $arguments)
       (let ($head $argument) $term
         (if (== $head mpi)
             (bfc:lower-mpi $argument $arguments)
             (bfc:lower-spine
               $head (cons-atom $argument $arguments))))))

(= (bfc:lower-I ($proof)) $proof)

(= (bfc:lower-mpi $continuation $arguments)
   (let ($implication-proof $after-implication)
        (decons-atom $arguments)
     (let ($antecedent-proof $rest) (decons-atom $after-implication)
       (bfc:lower-spine
         $continuation
         (cons-atom
           (mp $implication-proof $antecedent-proof)
           $rest)))))

; Infer a fixed direct tree while replacing each implicit axiom occurrence by
; an explicitly instantiated dependent constructor.
(= (hilbert:infer ax1)
   (: (ax1-explicit $ph $ps)
      (imp $ph (imp $ps $ph))))

(= (hilbert:infer ax2)
   (: (ax2-explicit $ph $ps $ch)
      (imp
        (imp $ph (imp $ps $ch))
        (imp (imp $ph $ps) (imp $ph $ch)))))

(= (hilbert:infer ax3)
   (: (ax3-explicit $ph $ps)
      (imp
        (imp (neg $ph) (neg $ps))
        (imp $ps $ph))))

(= (hilbert:infer (mp $implication $antecedent))
   (let (: $implication-proof $implication-type)
        (hilbert:infer $implication)
     (let (: $antecedent-proof $antecedent-type)
          (hilbert:infer $antecedent)
       (unify $implication-type
              (imp $antecedent-type $result-type)
              (: (mp $implication-proof $antecedent-proof) $result-type)
              (empty)))))

(= (hilbert:elaborate $continuation $goal)
   (let $direct (bfc:lower $continuation)
     (let (: $explicit $inferred-goal) (hilbert:infer $direct)
       (unify $inferred-goal $goal $explicit (empty)))))

(= (hilbert:certify (: $continuation $goal))
   (let $explicit (hilbert:elaborate $continuation $goal)
     (case (check-type &self $explicit $goal 100000000)
      (((he-accept (exact $goal))
         (Certified $goal))
        ($verdict
         (Rejected $verdict))))))

!(collapse
   (let $occurrence
        (obfc 13 (: $x (imp (imp (imp p q) r) (imp q r))))
     (hilbert:certify $occurrence)))
