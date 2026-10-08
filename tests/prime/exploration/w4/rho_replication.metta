; Replication through reflection, against a drop-free contrast, in the
; strict-core rho of the draft.  The processes are the ones the metatheory
; names:
;   @0              the name (rho:quote rho:nil)
;   sendZero        @0!(0)
;   D(@0)           for (y <- @0) (@0!(*y) | *y), the duplicator
;   !P              @0!(D(@0) | P) | D(@0), replication of P
;   ping            @0!(0) | for (y <- @0) 0
; Successors are the one-step COMM frontier (lts:rho:transitions).  The
; metatheory states its reductions up to structural congruence, so the
; program compares processes as the metatheory does: parallel composition
; is a bag, the empty process is its unit, and bound names are compared up
; to renaming.

!(import! &self lts:rho)

(= (at-zero) (rho:quote rho:nil))
(= (send-zero) (rho:send (at-zero) rho:nil))
(= (duplicator)
   (rho:recv (at-zero) $y
     (rho:par (rho:send (at-zero) (rho:drop $y)) (rho:drop $y))))
(= (replicate $p) (rho:par (rho:send (at-zero) (rho:par (duplicator) $p)) (duplicator)))
(= (ping) (rho:par (send-zero) (rho:recv (at-zero) $y rho:nil)))

; ---- structural congruence ------------------------------------------------
; A process becomes the bag of its parallel components, each in a form where
; a bound name is replaced by its binding depth.

(= (components $d $p)
   (if (== $p rho:nil) ()
       (if (== (get-metatype $p) Expression)
           (let $h (car-atom $p)
             (if (== $h rho:par)
                 (let $args (cdr-atom $p) (components-of-list $d $args)){$args}
                 (let $c (canon-proc $d $p) ($c)){$c})){$h}
           ($p))))
(= (components-of-list $d $args)
   (if (== $args ()) ()
       (let $h (car-atom $args)
         (let $t (cdr-atom $args)
           (let $hc (components $d $h)
             (let $tc (components-of-list $d $t)
               (concat $hc $tc)){$tc}){$hc}){$t}){$h}))
(= (concat $xs $ys)
   (if (== $xs ()) $ys
       (let $h (car-atom $xs)
         (let $t (cdr-atom $xs)
           (let $r (concat $t $ys) (cons-atom $h $r)){$r}){$t}){$h}))
(= (bag-of $d $p) (let $cs (components $d $p) (cons-atom bag $cs)))
(= (canon-proc $d (rho:send $c $q))
   (let $cn (canon-name $d $c) (let $cq (bag-of $d $q) (send $cn $cq))))
(= (canon-proc $d (rho:recv $c $v $b))
   (let $cn (canon-name $d $c)
     (let $v (bv $d)
       (let $d1 (+ $d 1) (let $cb (bag-of $d1 $b) (recv $cn $cb)))){$v}))
(= (canon-proc $d (rho:drop $n)) (let $cn (canon-name $d $n) (drop $cn)))
(= (canon-name $d (rho:quote $p)) (let $cp (bag-of $d $p) (quote-of $cp)))
(= (canon-name $d (bv $k)) (bv $k))

(= (same $a $b)
   (if (== (get-metatype $a) Expression)
       (if (== (get-metatype $b) Expression)
           (let $ha (car-atom $a)
             (let $hb (car-atom $b)
               (let $ta (cdr-atom $a)
                 (let $tb (cdr-atom $b)
                   (if (== $ha bag)
                       (if (== $hb bag) (bag-eq $ta $tb) False)
                       (if (== $ha $hb) (same-list $ta $tb) False))){$tb}){$ta}){$hb}){$ha}
           False)
       (== $a $b)))
(= (same-list $xs $ys)
   (if (== $xs ()) (== $ys ())
       (if (== $ys ()) False
           (let $x (car-atom $xs)
             (let $y (car-atom $ys)
               (if (same $x $y)
                   (let $rx (cdr-atom $xs) (let $ry (cdr-atom $ys) (same-list $rx $ry)){$ry}){$rx}
                   False)){$y}){$x})))
(= (bag-eq $xs $ys)
   (if (== $xs ()) (== $ys ())
       (let $x (car-atom $xs)
         (let $rest (cdr-atom $xs)
           (let $left (remove-one $x $ys)
             (if (== $left no-match) False (bag-eq $rest $left))){$left}){$rest}){$x}))
(= (remove-one $x $ys)
   (if (== $ys ()) no-match
       (let $y (car-atom $ys)
         (let $rest (cdr-atom $ys)
           (if (same $x $y) $rest
               (let $r (remove-one $x $rest)
                 (if (== $r no-match) no-match (cons-atom $y $r))){$r})){$rest}){$y}))
(= (congruent $p $q) (let $cp (bag-of 0 $p) (let $cq (bag-of 0 $q) (same $cp $cq))))

; Some member of a list of processes is congruent to the target.
(= (some-congruent $ps $target)
   (if (== $ps ()) False
       (let $p (car-atom $ps)
         (if (congruent $p $target) True
             (let $rest (cdr-atom $ps) (some-congruent $rest $target)){$rest})){$p}))
; Every member of a list of processes has no successor.
(= (all-quiescent $ps)
   (if (== $ps ()) True
       (let $p (car-atom $ps)
         (if (lts:rho:is-normal-form $p)
             (let $rest (cdr-atom $ps) (all-quiescent $rest)){$rest}
             False)){$p}))
; A process mentions a drop.
(= (mentions-drop $p)
   (if (== $p rho:drop) True
       (if (== (get-metatype $p) Expression)
           (if (== $p ()) False
               (let $h (car-atom $p)
                 (if (mentions-drop $h) True
                     (let $t (cdr-atom $p) (mentions-drop $t)){$t})){$h})
           False)))

; ---- the comparison is structural congruence -------------------------------
; Units, reordering and renaming are congruences; changing a component is not.
!(congruent (rho:par (send-zero) rho:nil) (send-zero))
!(congruent (rho:par (duplicator) (send-zero)) (rho:par (send-zero) (duplicator)))
!(congruent (duplicator)
            (rho:recv (at-zero) $z (rho:par (rho:drop $z) (rho:send (at-zero) (rho:drop $z)))))
!(congruent (duplicator)
            (rho:recv (at-zero) $z (rho:par (rho:drop $z) (rho:send (at-zero) rho:nil))))

; ---- replication through reflection ----------------------------------------
; The iterates !P, !P | P, (!P | P) | P of the unfolding, for P = sendZero.
(= (unfolding-0) (replicate (send-zero)))
(= (unfolding-1) (rho:par (unfolding-0) (send-zero)))
(= (unfolding-2) (rho:par (unfolding-1) (send-zero)))
(= (ping-beside) (rho:par (ping) (send-zero)))
(= (successors $p) (collapse (lts:rho:transitions $p)))

; 1. One communication unfolds !P into !P | P
;    (replicate_sendZero_contrast, first part; replicate_unfold).
!(let $p (unfolding-0) (let $next (successors $p) (let $q (unfolding-1) (some-congruent $next $q))))
; 2. The unfolding goes on: !P | P reduces to (!P | P) | P (unfolding_step),
;    so the encoding has an infinite reduction sequence
;    (replicate_runs_forever).
!(let $p (unfolding-1) (let $next (successors $p) (let $q (unfolding-2) (some-congruent $next $q))))

; ---- the drop-free contrast ------------------------------------------------
; 3. ping communicates (ping_reduces): its reduct is the empty process.
!(let $p (ping) (let $next (successors $p) (some-congruent $next rho:nil)))
; 4. and only once (ping_one_step): every reduct is quiescent.
!(let $p (ping) (let $next (successors $p) (all-quiescent $next)))
; 5. A drop-free process never reduces to itself beside sendZero
;    (replicate_sendZero_contrast, second part), so ping is no fixed point of
;    X |-> X | sendZero, while the replicator is one up to one step.
!(let $p (ping) (let $next (successors $p) (let $q (ping-beside) (some-congruent $next $q))))
; 6. The difference is drop (drop_is_essential): the replicator runs its
;    received code with a drop (replicate_not_codeInert); ping has none
;    (ping_dropFree).
!(let $p (unfolding-0) (mentions-drop $p))
!(let $p (ping) (mentions-drop $p))
