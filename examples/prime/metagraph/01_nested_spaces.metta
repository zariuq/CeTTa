; Nested spaces through names inside atoms.
;
; A mind keeps one index space.  Its atoms hold the names of other spaces: two
; micro-theories, an episode memory and a sandbox.  Those spaces hold further
; space names, so knowledge, memory and scratch work form a graph of spaces.
; A space is an ordinary value: it is stored as an argument of an atom, matched
; out by a query, and used as the location of the next read, in the same
; firing or in a later one.
;
;   index ── (theory physics P) ── P ── (subtheory mechanics M) ── M ── (subtheory statics S) ── S
;         ── (theory folk-physics F)                                     (law ...)                (law ...)
;         ── (memory episodes E) ── E ── (episode 1 E1) ── E1 ── (notes N1) ── N1 (note ...)
;                                      ── (episode 2 E2)
;         ── (sandbox scratch B) ── B ── (trial alt-gravity T) ── T ── (uses M)   T names the same M as P
;
; Printing: the draft prints a space as its machine address, which changes from
; run to run, so no answer below contains a space.  Every space holds a
; (label NAME) atom and the queries answer with labels.
;
; Steps: (counted Q) runs the query Q and reports its sorted answers with the
; number of firings it committed, read from the agenda's counter.  A firing is
; one step of the candidate step relation: one commit over the spaces it names.
; A plain `match` is a query of the store, not a firing, so it counts zero.
!(import! &self interaction)

(= (fires) (let (agenda $parked $ready $parks $attempts (fires $n) $woken $unchanged) (agenda-stats) $n))
(: counted (-> Atom %Undefined%))
(= (counted $q)
   (let* (($before (fires))
          ($answers (sort-atom (collapse (eval $q))))
          ($after (fires)))
     (answers $answers (steps (- $after $before)))))

(= (labelled $l) (let $s (new-space) (let $_ (add-atom $s (label $l)) $s)))

; ---- building the graph: every space is made fresh and reached only through
;      the index or through a space the index names.
!(bind! &index (labelled index))
!(let* (($statics (labelled statics))
        ($mechanics (labelled mechanics))
        ($physics (labelled physics))
        ($folk (labelled folk-physics))
        ($episodes (labelled episodes))
        ($e1 (labelled episode-1))
        ($e2 (labelled episode-2))
        ($notes (labelled notes-1))
        ($sandbox (labelled sandbox))
        ($trial (labelled trial))
        ($_01 (add-atom $statics (law (sum-of-forces zero))))
        ($_02 (add-atom $mechanics (law (force mass-times-acceleration))))
        ($_03 (add-atom $mechanics (subtheory statics $statics)))
        ($_04 (add-atom $physics (axiom (falls apple))))
        ($_05 (add-atom $physics (subtheory mechanics $mechanics)))
        ($_06 (add-atom $folk (axiom (heavy-falls-faster))))
        ($_07 (add-atom $notes (note (fell straight down))))
        ($_08 (add-atom $e1 (event (saw apple fall))))
        ($_09 (add-atom $e1 (notes $notes)))
        ($_10 (add-atom $e2 (event (dropped feather))))
        ($_11 (add-atom $episodes (episode 1 $e1)))
        ($_12 (add-atom $episodes (episode 2 $e2)))
        ($_13 (add-atom $trial (axiom (all-fall-equally))))
        ($_14 (add-atom $trial (uses $mechanics)))
        ($_15 (add-atom $sandbox (trial alt-gravity $trial)))
        ($_16 (add-atom &index (theory physics $physics)))
        ($_17 (add-atom &index (theory folk-physics $folk)))
        ($_18 (add-atom &index (memory episodes $episodes)))
        ($_19 (add-atom &index (sandbox scratch $sandbox))))
   built)

; ---- 1. One level: the index names four spaces; each name resolves to a space
;      whose own label is read in the same firing.  1 line, 1 step.
!(counted (transform &index (every (, (read ($kind $name $s)) (read $s (label $l)))) ($kind $name $l)))

; ---- 2. Two levels in one firing: every axiom of every theory.  1 line, 1 step.
!(counted (transform &index (every (, (read (theory $name $t)) (read $t (axiom $a)))) (axiom $name $a)))

; ---- 3. Three levels in one firing: index -> physics -> mechanics -> statics.
;      Each source binds the space the next source reads.  1 line, 1 step.
!(counted (transform &index
   (, (read (theory physics $p)) (read $p (subtheory mechanics $m))
      (read $m (subtheory statics $s)) (read $s (law $l)))
   (statics-law $l)))

; ---- 4. Three descents into episode memory: index -> episodes -> episode 1 ->
;      its notes.  1 line, 1 step.
!(counted (transform &index
   (, (read (memory episodes $e)) (read $e (episode 1 $x)) (read $x (notes $n)) (read $n (note $t)))
   (note-on-1 $t)))

; ---- 5. The same three-level descent as plain nested `match`: no firing at all,
;      4 lines, every answer.
!(counted (match &index (theory physics $p)
            (match $p (subtheory mechanics $m)
              (match $m (subtheory statics $s)
                (match $s (law $l) (statics-law $l))))))

; ---- 6. The same descent as a chain of separate firings, each handing a space
;      to the next as a location.  A location may be a name of the scope around,
;      so no crossing set is needed for the handle.  5 lines, 4 steps.
!(counted (let* (($p (transform &index (read (theory physics $p)) $p))
                 ($m (transform $p (read (subtheory mechanics $m)) $m))
                 ($s (transform $m (read (subtheory statics $s)) $s)))
            (transform $s (read (law $l)) (statics-law $l))))

; ---- 7. A generic walk along a path of (relation name) steps.  The relation and
;      the name come from the path, so they reach the pattern only through the
;      crossing set {$rel $name}: under the draft's default scope profile
;      (lexical-fresh) a name written in a pattern is the pattern's own.
(= (walk $s $path)
   (if (== $path ())
       $s
       (let (($rel $name) $rest) (decons-atom $path)
         (walk (transform $s (read ($rel $name $next)) $next){$rel $name} $rest))))
(= (label-of $s) (transform $s (read (label $l)) $l))
!(counted (label-of (walk &index ((theory physics) (subtheory mechanics) (subtheory statics)))))
!(counted (label-of (walk &index ((sandbox scratch) (trial alt-gravity)))))

; ---- 8. Without the crossing set the pattern's $rel and $name are fresh: every
;      edge of the index matches, and with `every` the walk's first hop reaches
;      all four spaces instead of one.  No warning is given.
(= (hop-all-fresh $s $rel $name) (transform $s (every (read ($rel $name $next))) (label-of $next)))
(= (hop-all-crossed $s $rel $name) (transform $s (every (read ($rel $name $next))) (label-of $next)){$rel $name})
!(sort-atom (collapse (hop-all-fresh &index theory physics)))
!(sort-atom (collapse (hop-all-crossed &index theory physics)))

; ---- 9. A modification in a space reached only through the index: revise the
;      note three spaces down, in one firing that takes the old note and makes
;      the new one.  1 step.
!(counted (transform &index
   (, (read (memory episodes $e)) (read $e (episode 1 $x)) (read $x (notes $n)) (take $n (note $t)))
   (add-atom $n (note (revised $t)))))
!(counted (transform &index
   (, (read (memory episodes $e)) (read $e (episode 1 $x)) (read $x (notes $n)) (every (read $n (note $t))))
   (note-on-1 $t)))

; ---- 10. Shared structure: the sandbox's trial theory names the same mechanics
;       space as physics does.  A law added through the sandbox path is seen
;       through the physics path: the graph shares spaces, it does not copy them.
!(counted (transform &index
   (, (read (sandbox scratch $b)) (read $b (trial alt-gravity $t)) (read $t (uses $m)))
   (add-atom $m (law (momentum mass-times-velocity)))))
!(counted (transform &index
   (, (read (theory physics $p)) (read $p (subtheory mechanics $m)) (every (read $m (law $l))))
   (mechanics-law $l)))
; Identity of spaces is ordinary unification: one variable for the end of both
; paths finds a row only because both paths reach the same space.
!(counted (transform &index
   (, (read (theory physics $p)) (read $p (subtheory mechanics $m))
      (read (sandbox scratch $b)) (read $b (trial alt-gravity $t)) (read $t (uses $m)))
   (one-space-at-the-end-of-both-paths)))
!(collapse (transform &index
   (, (read (theory physics $p)) (read $p (subtheory mechanics $m)) (read $m (subtheory statics $s))
      (read (sandbox scratch $b)) (read $b (trial alt-gravity $t)) (read $t (uses $s)))
   (statics-at-the-end-of-both-paths)))
