; Moving theorems between the HOTG theory and the bare logic.
;
; Relativization: a theorem phi of the Megalodon HOTG theory becomes, in a
; space with the bare logic, the theorem
;     all In Empty Union Power Sep Repl Eps_set UnivOf.
;       law1 -> ... -> law11 -> phi
; with the eight constants of the HOTG signature as variables, bound under
; their own names, and its eleven laws as hypotheses. Its proof is the HOTG proof with each constant
; read as the corresponding variable, each cited law as the corresponding
; hypothesis, and each cited HOTG theorem as its own relativization applied
; to the variables and the hypotheses. The bare logic checks the result.
;
; Inclusion: a theorem of the bare logic is checked afresh in a HOTG space,
; with the theorems it cites included first.
;
; Instantiation: an included relativization, applied to the HOTG constants
; and to the HOTG laws, proves the original statement again in the HOTG
; space.
;
; Supported proof forms: pf:known, pf:hyp, pf:var, pf:all-intro, pf:all-elim,
; pf:imp-intro, pf:imp-elim, pf:typed, and the logical equality rules. A
; theorem whose statement or proof uses a defined constant, a structured
; proof verb or another form is refused, with the form named.

; ---------------------------------------------------------------------------
; The HOTG signature
; ---------------------------------------------------------------------------

(= (hol-rel:constants)
  (function (return (In Empty Union Power Sep Repl Eps_set UnivOf))))

(= (hol-rel:laws)
  (function (return (extensionality emptyLaw unionLaw powerLaw separationLaw
    replacementLaw setInduction universeIn universeTransitive universeClosed
    universeMinimal))))

; ---------------------------------------------------------------------------
; Tuples
; ---------------------------------------------------------------------------

(= (hol-rel:position $item $items $index)
  (if (== $items ()) None
    (let $first (car-atom $items)
      (if (== $first $item) (Some $index)
        (let $rest (cdr-atom $items) (hol-rel:position $item $rest (+ $index 1)))))))

(= (hol-rel:size $items) (size-atom $items))

(= (hol-rel:member $item $items)
  (case (hol-rel:position $item $items 0)
    (((Some $index) True) (None False))))

; ---------------------------------------------------------------------------
; Terms: constants as bound names, or as proof variables
; ---------------------------------------------------------------------------

(= (hol-rel:binder-name $binder)
  (case (get-metatype $binder)
    ((Symbol $binder)
     (Expression (car-atom $binder))
     ($other $binder))))

; In a proof, HOTG constant number k (of n) is the proof variable that the
; relativization's k-th all-intro binds, `depth` term binders further in.
(= (hol-rel:proof-term $term $depth $shadowed)
  (case (get-metatype $term)
    ((Symbol
       (let $constants (hol-rel:constants)
         (case (hol-rel:position $term $constants 0)
           (((Some $k)
              (if (hol-rel:member $term $shadowed) $term
                (let $n (hol-rel:size $constants)
                  (let $index (+ (- (- $n 1) $k) $depth)
                    (function (return (pf:var $index)))))))
            (None $term)))))
     (Expression (hol-rel:proof-term-expression $term $depth $shadowed))
     ($other $term))))

(= (hol-rel:proof-term-expression $term $depth $shadowed)
  (if (== $term ()) (function (return ()))
    (let $head (car-atom $term)
      (if (== $head pf:var) (function (return $term))
        (if (== $head lam)
          (let $binder (index-atom $term 1)
            (let $body (index-atom $term 2)
              (let $name (hol-rel:binder-name $binder)
                (let $inner (cons-atom $name $shadowed)
                  (let $new-body (hol-rel:proof-term $body $depth $inner)
                    (let $result (cons-atom lam ($binder $new-body))
                      (function (return $result))))))))
          (let $items (hol-rel:proof-term-items $term $depth $shadowed)
            (function (return $items))))))))

(= (hol-rel:proof-term-items $items $depth $shadowed)
  (if (== $items ()) (function (return ()))
    (let $first (car-atom $items)
      (let $rest (cdr-atom $items)
        (let $new-first (hol-rel:proof-term $first $depth $shadowed)
          (let $new-rest (hol-rel:proof-term-items $rest $depth $shadowed)
            (let $whole (cons-atom $new-first $new-rest)
              (function (return $whole)))))))))

; ---------------------------------------------------------------------------
; Statements
; ---------------------------------------------------------------------------

; law1 -> ... -> law11 -> phi.  The constants keep their names: bound by the
; generalization below, they are variables there.
(= (hol-rel:hypotheses $source $laws $statement)
  (if (== $laws ()) (function (return $statement))
    (let $law (car-atom $laws)
      (let $rest (cdr-atom $laws)
        (let $stated (hol:known-proposition $source $law)
          (let $inner (hol-rel:hypotheses $source $rest $statement)
            (let $whole (cons-atom imp ($stated $inner))
              (function (return $whole)))))))))

; all c1 ... cn. body, binding each constant under its own name, with its
; HOTG type.
(= (hol-rel:generalize $source $constants $body)
  (if (== $constants ()) (function (return $body))
    (let $constant (car-atom $constants)
      (let $rest (cdr-atom $constants)
        (let $type (hol:of $source $constant)
          (let $inner (hol-rel:generalize $source $rest $body)
            (let $binder (cons-atom $constant (: $type))
              (let $family (cons-atom lam ($binder $inner))
                (let $whole (cons-atom all ($type $family))
                  (function (return $whole)))))))))))

(= (hol-rel:statement $source $statement)
  (let $laws (hol-rel:laws)
    (let $body (hol-rel:hypotheses $source $laws $statement)
      (let $constants (hol-rel:constants)
        (hol-rel:generalize $source $constants $body)))))

; ---------------------------------------------------------------------------
; Proofs
; ---------------------------------------------------------------------------
;
; vars, hyps: the term binders and hypotheses the proof has introduced so
; far, beneath the relativization's own.

(= (hol-rel:proof $source $target $proof $vars $hyps)
  (case (get-metatype $proof)
    ((Expression (hol-rel:proof-node $source $target $proof $vars $hyps))
     ($other (function (return (HOL:RelativizationUnsupported $proof)))))))

(= (hol-rel:proof-node $source $target $proof $vars $hyps)
  (let $head (car-atom $proof)
    (case $head
      ((pf:known (let $name (index-atom $proof 1)
                   (hol-rel:cite $source $target $name $vars $hyps)))
       (pf:hyp (function (return $proof)))
       (pf:var (function (return $proof)))
       (pf:all-intro
         (let $body (index-atom $proof 1)
           (let $new (hol-rel:proof $source $target $body (+ $vars 1) $hyps)
             (function (return (pf:all-intro $new))))))
       (pf:imp-intro
         (let $body (index-atom $proof 1)
           (let $new (hol-rel:proof $source $target $body $vars (+ $hyps 1))
             (function (return (pf:imp-intro $new))))))
       (pf:all-elim
         (let $major (index-atom $proof 1)
           (let $witness (index-atom $proof 2)
             (let $new-major (hol-rel:proof $source $target $major $vars $hyps)
               (let $new-witness (hol-rel:proof-term $witness $vars ())
                 (function (return (pf:all-elim $new-major $new-witness))))))))
       (pf:imp-elim
         (let $major (index-atom $proof 1)
           (let $minor (index-atom $proof 2)
             (let $new-major (hol-rel:proof $source $target $major $vars $hyps)
               (let $new-minor (hol-rel:proof $source $target $minor $vars $hyps)
                 (function (return (pf:imp-elim $new-major $new-minor))))))))
       (pf:typed
         (let $statement (index-atom $proof 1)
           (let $body (index-atom $proof 2)
             (let $new-statement (hol-rel:proof-term $statement $vars ())
               (let $new-body (hol-rel:proof $source $target $body $vars $hyps)
                 (function (return (pf:typed $new-statement $new-body))))))))
       ($other
         (if (hol-rel:member $head (pf:eq-refl pf:beta pf:eq-symm pf:eq-trans
                                    pf:eq-prop-elim pf:eq-app pf:eq-app-arg))
           (hol-rel:equality $source $target $proof $vars $hyps)
           (function (return (HOL:RelativizationUnsupported $head)))))))))

; Equality rules: proof operands are proofs, the others are terms or types.
(= (hol-rel:equality $source $target $proof $vars $hyps)
  (let $head (car-atom $proof)
    (let $operands (cdr-atom $proof)
      (let $proof-positions (hol-rel:proof-positions $head)
        (let $new (hol-rel:equality-operands $source $target $operands $proof-positions 1 $vars $hyps)
          (let $whole (cons-atom $head $new)
            (function (return $whole))))))))

(= (hol-rel:proof-positions $head)
  (case $head
    ((pf:eq-refl ()) (pf:beta ()) (pf:eq-symm (1)) (pf:eq-trans (1 2))
     (pf:eq-prop-elim (1)) (pf:eq-app (3)) (pf:eq-app-arg (4)))))

(= (hol-rel:equality-operands $source $target $operands $positions $index $vars $hyps)
  (if (== $operands ()) (function (return ()))
    (let $first (car-atom $operands)
      (let $rest (cdr-atom $operands)
        (let $new-first
               (if (hol-rel:member $index $positions)
                 (hol-rel:proof $source $target $first $vars $hyps)
                 (hol-rel:proof-term $first $vars ()))
          (let $new-rest (hol-rel:equality-operands $source $target $rest $positions
                           (+ $index 1) $vars $hyps)
            (let $whole (cons-atom $new-first $new-rest)
              (function (return $whole)))))))))

; A cited name: a law is the corresponding hypothesis; another theorem is
; its relativization, applied to the variables and the hypotheses.
(= (hol-rel:cite $source $target $name $vars $hyps)
  (let $laws (hol-rel:laws)
    (case (hol-rel:position $name $laws 0)
      (((Some $j)
         (let $m (hol-rel:size $laws)
           (let $index (+ (- (- $m 1) $j) $hyps)
             (function (return (pf:hyp $index))))))
       (None
         (let $relativized (hol-rel:relativize $source $target $name)
           (case (get-metatype $relativized)
             ((Symbol (hol-rel:apply $relativized $vars $hyps))
              ($refused (function (return $relativized)))))))))))

; (pf:known r) applied to every constant variable, then to every law
; hypothesis.
(= (hol-rel:apply $name $vars $hyps)
  (let $constants (hol-rel:constants)
    (let $laws (hol-rel:laws)
      (let $known (cons-atom pf:known ($name))
        (let $with-vars (hol-rel:apply-vars $known 0 (hol-rel:size $constants) $vars)
          (hol-rel:apply-hyps $with-vars 0 (hol-rel:size $laws) $hyps))))))

(= (hol-rel:apply-vars $proof $k $n $vars)
  (if (== $k $n) (function (return $proof))
    (let $index (+ (- (- $n 1) $k) $vars)
      (let $applied (cons-atom pf:all-elim ($proof (pf:var $index)))
        (hol-rel:apply-vars $applied (+ $k 1) $n $vars)))))

(= (hol-rel:apply-hyps $proof $j $m $hyps)
  (if (== $j $m) (function (return $proof))
    (let $index (+ (- (- $m 1) $j) $hyps)
      (let $applied (cons-atom pf:imp-elim ($proof (pf:hyp $index)))
        (hol-rel:apply-hyps $applied (+ $j 1) $m $hyps)))))

; all-intro for every constant, imp-intro for every law, around the body.
(= (hol-rel:wrap $body $count $form)
  (if (== $count 0) (function (return $body))
    (let $inner (hol-rel:wrap $body (- $count 1) $form)
      (let $whole (cons-atom $form ($inner))
        (function (return $whole))))))

; The first proof form the relativization could not read, if any.
(= (hol-rel:unsupported $proof)
  (case (get-metatype $proof)
    ((Expression
       (if (== $proof ()) None
         (let $head (car-atom $proof)
           (if (== $head HOL:RelativizationUnsupported)
             (let $form (index-atom $proof 1) (Some $form))
             (hol-rel:unsupported-items $proof)))))
     ($other None))))

(= (hol-rel:unsupported-items $items)
  (if (== $items ()) None
    (let $first (car-atom $items)
      (case (hol-rel:unsupported $first)
        (((Some $form) (Some $form))
         (None (let $rest (cdr-atom $items) (hol-rel:unsupported-items $rest))))))))

; ---------------------------------------------------------------------------
; Relativize, include, instantiate
; ---------------------------------------------------------------------------

; A pattern variable bound outside a match is fresh inside it, so each record
; kind has its own lookup with the head written out.
(= (hol-rel:find $space hol-rel:relativized $name)
  (hol-rel:find-in $name (collapse (match $space (hol-rel:relativized $source $result)
                                     ($source $result)))))
(= (hol-rel:find $space hol-rel:included $name)
  (hol-rel:find-in $name (collapse (match $space (hol-rel:included $source $result)
                                     ($source $result)))))

(= (hol-rel:find-in $name $pairs)
  (if (== $pairs ()) None
    (let $first (car-atom $pairs)
      (let ($source $result) $first
        (if (== $source $name) (Some $result)
          (let $rest (cdr-atom $pairs) (hol-rel:find-in $name $rest)))))))

; The relativization of a HOTG theorem, published in the target; its name,
; or a refusal.
(= (hol-rel:relativize $source $target $name)
  (case (hol-rel:find $target hol-rel:relativized $name)
    (((Some $known) $known)
     (None
       (let $statement (hol:known-proposition $source $name)
         (let $proof (hol:known-proof $source $name)
           (let $body (hol-rel:proof $source $target $proof 0 0)
             (case (hol-rel:unsupported $body)
               (((Some $form) (function (return (HOL:RelativizationRefused $name (unsupported $form)))))
                (None (hol-rel:publish-relativization $source $target $name $statement $body)))))))))))

(= (hol-rel:publish-relativization $source $target $name $statement $body)
             (let $laws (hol-rel:laws)
               (let $constants (hol-rel:constants)
                 (let $with-hyps (hol-rel:wrap $body (hol-rel:size $laws) pf:imp-intro)
                   (let $whole-proof (hol-rel:wrap $with-hyps (hol-rel:size $constants) pf:all-intro)
                     (let $relativized (hol-rel:statement $source $statement)
                       (let $new-name (parse (format-args "rel:{}" ($name)))
                         (let $qs (cons-atom quote ($relativized))
                           (let $qp (cons-atom quote ($whole-proof))
                             (let $published (hol:theorem $target $new-name $qs $qp)
                               (if (== $published $new-name)
                                 (let $marked (add-atom $target (hol-rel:relativized $name $new-name))
                                   $new-name)
                                 (let $why (try (hol:theorem $target $new-name $qs $qp))
                                   (function (return (HOL:RelativizationRefused $name $why)))))))))))))))

; A theorem of the bare logic, checked afresh in another space, after the
; theorems its proof cites.
(= (hol-rel:include $source $target $name)
  (case (hol-rel:find $target hol-rel:included $name)
    (((Some $known) $known)
     (None
       (let $statement (hol:known-proposition $source $name)
         (let $proof (hol:known-proof $source $name)
           (let $cited (hol-rel:cited $proof)
             (let $included (hol-rel:include-all $source $target $cited)
               (let $renamed-proof (hol-rel:rename-citations $target $proof)
                 (let $new-name (parse (format-args "inc:{}" ($name)))
                   (let $qs (cons-atom quote ($statement))
                     (let $qp (cons-atom quote ($renamed-proof))
                       (let $published (hol:theorem $target $new-name $qs $qp)
                         (if (== $published $new-name)
                           (let $marked (add-atom $target (hol-rel:included $name $new-name))
                             $new-name)
                           (let $why (try (hol:theorem $target $new-name $qs $qp))
                             (function (return (HOL:InclusionRefused $name $why))))))))))))))))))

; The names a proof cites with pf:known.
(= (hol-rel:cited $proof)
  (case (get-metatype $proof)
    ((Expression
       (if (== $proof ()) ()
         (let $head (car-atom $proof)
           (if (== $head pf:known)
             (let $name (index-atom $proof 1) ($name))
             (hol-rel:cited-items $proof)))))
     ($other ()))))

(= (hol-rel:cited-items $items)
  (if (== $items ()) ()
    (let $first (car-atom $items)
      (let $rest (cdr-atom $items)
        (let $a (hol-rel:cited $first)
          (let $b (hol-rel:cited-items $rest)
            (union-atom $a $b)))))))

(= (hol-rel:include-all $source $target $names)
  (if (== $names ()) ()
    (let $first (car-atom $names)
      (let $rest (cdr-atom $names)
        (let $done (hol-rel:include $source $target $first)
          (hol-rel:include-all $source $target $rest))))))

; Citations of included theorems use their included names.
(= (hol-rel:rename-citations $target $proof)
  (case (get-metatype $proof)
    ((Expression
       (if (== $proof ()) (function (return ()))
         (let $head (car-atom $proof)
           (if (== $head pf:known)
             (let $name (index-atom $proof 1)
               (case (hol-rel:find $target hol-rel:included $name)
                 (((Some $included) (function (return (pf:known $included))))
                  (None (function (return $proof))))))
             (hol-rel:rename-citations-items $target $proof)))))
     ($other $proof))))

(= (hol-rel:rename-citations-items $target $items)
  (if (== $items ()) (function (return ()))
    (let $first (car-atom $items)
      (let $rest (cdr-atom $items)
        (let $new-first (hol-rel:rename-citations $target $first)
          (let $new-rest (hol-rel:rename-citations-items $target $rest)
            (let $whole (cons-atom $new-first $new-rest)
              (function (return $whole)))))))))

; Apply an included relativization to the HOTG constants and laws: the
; original statement, proved again in the HOTG space.
(= (hol-rel:instantiate $hotg $included $name $statement)
  (let $constants (hol-rel:constants)
    (let $laws (hol-rel:laws)
      (let $known (cons-atom pf:known ($included))
        (let $at-constants (hol-rel:apply-constants $known $constants)
          (let $proof (hol-rel:apply-laws $at-constants $laws)
            (let $qs (cons-atom quote ($statement))
              (let $qp (cons-atom quote ($proof))
                (let $published (hol:theorem $hotg $name $qs $qp)
                  (if (== $published $name) $name
                    (let $why (try (hol:theorem $hotg $name $qs $qp))
                      (function (return (HOL:InstantiationRefused $name $why))))))))))))))

(= (hol-rel:apply-constants $proof $constants)
  (if (== $constants ()) (function (return $proof))
    (let $first (car-atom $constants)
      (let $rest (cdr-atom $constants)
        (let $applied (cons-atom pf:all-elim ($proof $first))
          (hol-rel:apply-constants $applied $rest))))))

(= (hol-rel:apply-laws $proof $laws)
  (if (== $laws ()) (function (return $proof))
    (let $first (car-atom $laws)
      (let $rest (cdr-atom $laws)
        (let $law (cons-atom pf:known ($first))
          (let $applied (cons-atom pf:imp-elim ($proof $law))
            (hol-rel:apply-laws $applied $rest)))))))
