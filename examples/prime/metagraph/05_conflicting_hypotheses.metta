; Conflicting hypotheses and unverified claims kept as knowledge data.
;
; A knowledge space holds two hypotheses that contradict each other and claims
; from sources that disagree.  Storing an atom asserts nothing: the space is a
; multiset of atoms, not a theory handed to a solver, so nothing becomes
; inconsistent.  Conflicts are computed by rules over the data and recorded as
; further data.  To reason from a hypothesis the agent copies it into a
; separate working space, where a forward-chaining rule derives consequences.
; Putting both sides of a conflict into one working space is shown twice: a
; space that keeps both and flags them, and a space whose assert refuses the
; second, a consistency discipline chosen for that space alone.
!(import! &self interaction)

!(bind! &kb (new-space))
!(transform &kb ()
   (, (add-atom &kb (hyp h1 (P a)))
      (add-atom &kb (hyp h2 (not (P a))))
      (add-atom &kb (hyp h3 (P b)))
      (add-atom &kb (claim c1 (capital australia sydney) (source forum)))
      (add-atom &kb (claim c2 (capital australia canberra) (source atlas)))
      (add-atom &kb (claim c3 (capital peru lima) (source atlas)))
      (add-atom &kb (functional capital))))

; ---- 1. Conflicts, computed and recorded as data.
; A hypothesis against its negation; one firing records every pair.
!(transform &kb (every (, (read (hyp $i $p)) (read (hyp $j (not $p)))))
   (add-atom &kb (conflict $i $j (on $p))))
; Claims that give a functional relation two values for one argument; a pair of
; claims is found in both orders.
!(transform &kb
   (every (, (read (functional $r)) (read (claim $c1 ($r $x $y) $s1)) (read (claim $c2 ($r $x $z) $s2))
             (let False (== $y $z))))
   (add-atom &kb (conflict $c1 $c2 (on ($r $x)))))
!(sort-atom (collapse (match &kb (conflict $i $j $why) (conflict $i $j $why))))
; Both sides are still answered: the space holds the hypothesis and its
; negation, and unrelated questions are answered as before.
!(sort-atom (collapse (match &kb (hyp $h (P a)) (hypothesis-for $h))))
!(sort-atom (collapse (match &kb (hyp $h (not (P a))) (hypothesis-against $h))))
!(collapse (match &kb (claim $c (capital peru $city) $src) (peru $city $src)))

; ---- 2. Reasoning from h1 in a working space of its own.
; Rules are data in the working space; their variables are schematic, so each
; reading of a rule is a fresh copy.  The chainer is a standing rule: from
; (rule PREMISE CONCLUSION) and (holds PREMISE) it makes (holds CONCLUSION),
; guarded so that it fires once per new conclusion.
(= (rules-into $w)
   (transform $w ()
     (, (add-atom $w (rule (P $x) (Q $x)))
        (add-atom $w (rule (Q $x) (R $x)))
        (add-atom $w (rule (not (P $x)) (S $x))))))
(= (chainer $w)
   (whenever (transform $w
      (, (read (rule $premise $conclusion)) (read (holds $premise)) (absent (holds $conclusion)))
      (add-atom $w (holds $conclusion)))))
(= (assume $w $h)
   (transform &kb (read (hyp $h $p)) (add-atom $w (holds $p))){$h})
(= (held $w) (sort-atom (collapse (match $w (holds $p) $p))))

!(let* (($w (new-space))
        ($_1 (rules-into $w))
        ($_2 (chainer $w))
        ($_3 (assume $w h1)))
   (working-space-of-h1 (held $w)
                        (h2-still-data (collapse (match &kb (hyp h2 $p) $p)))
                        (h2-not-held (collapse (match $w (holds (not (P a))) yes)))))

; ---- 3. Both sides in one working space that keeps both.
; Nothing fails and nothing explodes: each side derives its own consequences,
; and a monitor rule records the contradiction as data.
(= (monitor $w)
   (whenever (transform $w
      (, (read (holds $p)) (read (holds (not $p))) (absent (contradiction $p)))
      (add-atom $w (contradiction $p)))))
!(let* (($w (new-space))
        ($_1 (rules-into $w))
        ($_2 (chainer $w))
        ($_3 (monitor $w))
        ($_4 (assume $w h1))
        ($_5 (assume $w h2)))
   (both-kept (held $w) (flags (collapse (match $w (contradiction $p) $p)))))

; ---- 4. Both sides in a working space whose assert keeps it consistent.
; The discipline belongs to this space's assert, not to storage: an assert
; whose negation already holds is refused, and the refusal is an answer.
(= (negation-of $p) (case $p (((not $q) $q) ($other (not $other)))))
(= (assert-consistent $w $p)
   (let $n (negation-of $p)
     (else (transform $w (absent (holds $n)) (, (add-atom $w (holds $p)) (asserted $p))){$n}
           (refused $p (contradicts $n)))))
(= (assume-consistent $w $h)
   (let $p (transform &kb (read (hyp $h $p)) $p){$h} (assert-consistent $w $p)))
!(let* (($w (new-space))
        ($_1 (rules-into $w))
        ($_2 (chainer $w))
        ($a1 (assume-consistent $w h1))
        ($a2 (assume-consistent $w h2)))
   (consistent-space $a1 $a2 (held $w)))
