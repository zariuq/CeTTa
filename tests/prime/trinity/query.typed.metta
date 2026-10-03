; A query with several answers, typed.  A space holds four edges between
; three nodes, one edge twice.  Run, `hop a` gives the targets of the edges
; that leave a: b, c and b again.  Typed, the query is a type: the type of its
; answers.  The space declares its edges typed,
;
;     (type:stored (edge node node))
;
; so that every stored edge is an occurrence: the occurrences are the type
; edge@occurrence, with one constant for each stored atom, named by its
; content (edge@a@b, edge@a@c, edge@b@a, and edge@a@b#2 for the second copy
; of the first), and the source and the target of an occurrence are
; edge@arg1 and edge@arg2, with one equation for each stored atom.  These are
; declarations of the space for every `type:` and `set:` judgment on it.  The
; answers of `hop x` are an occurrence with the evidence that its source is x,
;
;     (sigma (o : edge@occurrence) (id node (app edge@arg1 o) x))
;
; and an answer returns the target of its occurrence.  An edge stored twice is
; two occurrences, so two answers that return one value.
!(bind! &kb (new-space))
!(add-atom &kb (: node (u 0)))
!(add-atom &kb (: a node))
!(add-atom &kb (: b node))
!(add-atom &kb (: c node))
!(add-atom &kb (type:stored (edge node node)))
!(add-atom &kb (edge a b))
!(add-atom &kb (edge a c))
!(add-atom &kb (edge b a))
!(add-atom &kb (edge a b))
(= (hop $x) (match &kb (edge $x $y) $y))

; ---- Run: a bag of answers, b twice.
!(hop a)

; ---- Typed: the type of the query at a is the type of its answers.
!(type:of (match &kb (edge a $y) $y))
; Every stored occurrence is checked against it.  The first, the second and
; the fourth are answers, each a pair of the occurrence with reflexivity, and
; each returns the target of its occurrence: b, c and b.  The third leaves b
; and is not an answer.  Established here is the success constructor of
; `try` around the typing verdict, taken apart to show the answers; it is not
; a stored proof.
!(let (Established (PrimeTypedQuery $type $answers $others))
      (try (type:of (match &kb (edge a $y) $y)))
   $answers)
!(let (Established (PrimeTypedQuery $type $answers $others))
      (try (type:of (match &kb (edge a $y) $y)))
   $others)

; ---- hop c: no edge leaves c.  The run gives nothing, the answer type is
; formed, and no occurrence gives an answer: each leaves a or b, so no
; closed pair of an occurrence and a proof that its source is c exists.
!(collapse (hop c))
!(type:of (match &kb (edge c $y) $y))
!(let (Established (PrimeTypedQuery $type $answers $others))
      (try (type:of (match &kb (edge c $y) $y)))
   ($answers $others))

; ---- The key may be at any position: the edges that arrive at b.
!(let (Established (PrimeTypedQuery $type $answers $others))
      (try (type:of (match &kb (edge $x b) $x)))
   ($type $answers))

; ---- A key that is no node is no query of this relation; a stored atom of
; the wrong type makes the declaration false, and the judgment is refuted.
!(try (type:of (match &kb (edge d $y) $y)))
!(bind! &bad (new-space))
!(add-atom &bad (: node (u 0)))
!(add-atom &bad (: a node))
!(add-atom &bad (type:stored (edge node node)))
!(add-atom &bad (edge a node))
!(try (type:of (match &bad (edge a $y) $y)))

; ---- Negative control: in a space that declares nothing, the same query
; has only a dynamic type, as before.
!(bind! &plain (new-space))
!(add-atom &plain (edge a b))
!(add-atom &plain (edge a b))
!(collapse (match &plain (edge a $y) $y))
!(try (type:of (match &plain (edge a $y) $y)))

; ---- The occurrences are part of the space.  Each has its type, the maps
; compute by their equations, and an answer written by hand checks against
; the query's type: the occurrence of either copy of a to b, with the
; reflexivity of a.  An answer whose occurrence leaves b is refuted, and so
; is one that claims the wrong key.
!(type:of &kb edge@a@b)
!(type:of &kb edge@a@b#2)
!(type:of &kb edge@arg2)
!(type:eq &kb (edge@arg2 edge@a@c) c)
!(type:eq &kb (edge@arg2 edge@a@c) b)
!(type:check &kb (pair edge@a@b (refl a))
             (sigma (o : edge@occurrence) (id node (edge@arg1 o) a)))
!(type:check &kb (pair edge@a@b#2 (refl a))
             (sigma (o : edge@occurrence) (id node (edge@arg1 o) a)))
!(try (type:check &kb (pair edge@b@a (refl a))
                  (sigma (o : edge@occurrence) (id node (edge@arg1 o) a))))
!(try (type:check &kb (pair edge@a@b (refl b))
                  (sigma (o : edge@occurrence) (id node (edge@arg1 o) a))))
!(set:check &kb edge@b@a edge@occurrence)

; ---- Names follow content: removing the edge from a to c renames no other
; occurrence, and that one is gone.
!(remove-atom &kb (edge a c))
!(type:of &kb edge@a@b#2)
!(type:of &kb edge@b@a)
!(try (type:of &kb edge@a@c))
!(let (Established (PrimeTypedQuery $type $answers $others))
      (try (type:of (match &kb (edge a $y) $y)))
   $answers)

