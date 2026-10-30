!(import! &self pat)
!(bind! &facts (new-space))
!(bind! &events (new-space))
!(add-atom &facts (edge a b))
!(add-atom &facts (edge a b))
(= (selected-space) (let $_ (add-atom &events selected) &facts))

; The declared argument protocol enters the space producer once. Duplicate
; facts are still two answers of the same query.
!(collapse (pat:match% (selected-space) (edge a $y) (destination $y)))
!(collapse (get-atoms &events))

; A rejected live declaration must not enter the matching/template work.
; Its strict space operand still runs exactly once before the type guard.
!(remove-atom &self (: pat:match% $type))
!(add-atom &self (: pat:match% (-> Number Atom Atom %Undefined%)))
!(collapse (pat:match% (selected-space) (edge a $y)
             (add-atom &events unexpected)))
!(collapse (get-atoms &events))

; Another query head retains its own declared protocol and shared variables.
!(collapse
   (let (pat:hit $rows $w) (pat:query &facts match% (edge a $y))
      (pat:apply $w (destination $y))))

; Pattern controls retain held branches while consuming prepared subjects.
!(pat:unify% (tag $x) (tag a) (kept $x) (add-atom &events unexpected))
!(pat:unify% (tag a) (other a) (add-atom &events unexpected) rejected)
!(pat:let% (tag $x)
    (let $_ (add-atom &events let-subject) (tag b)) (kept $x))
!(pat:case% (let $_ (add-atom &events case-subject) (tag c))
    (((tag $x) (kept $x)) ($other (add-atom &events unexpected))))
!(collapse (get-atoms &events))
