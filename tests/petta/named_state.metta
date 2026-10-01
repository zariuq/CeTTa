; PeTTa state names address process-local values rather than exposing the
; shared evaluator's StateMonad carrier.
!(bind! state (new-state rest))
!(test (get-state state) rest)
!(change-state! state active)
!(test (get-state state) active)

; change-state! also creates a name, and its value argument is evaluated.
!(change-state! counter (+ 20 22))
!(test (get-state counter) 42)

; A resource-shaped state name denotes the location here, not the value
; already stored at that location.  Nested use must preserve that boundary.
!(change-state! &counter-resource 41)
!(test (let $value (get-state &counter-resource) (+ $value 1)) 42)

; Malformed bind! forms have no relational answer.
!(test (collapse (bind! invalid not-a-new-state)) ())

; Outside bind!'s recognized state pattern, new-state remains ordinary data
; while its known nested arguments still evaluate.
!(test (new-state (+ 1 2)) (new-state 3))

; A name that was never set has no value: reading it raises SWI's
; existence_error, which catch makes a value.
!(catch (get-state never-set))
!(test (let $v (catch (get-state never-set)) (car-atom $v)) Error)

; A computed name is the name it evaluates to.
!(change-state! (atom_concat agenda_ kb) 7)
!(test (get-state (atom_concat agenda_ kb)) 7)

; State names and space names are separate namespaces, as the reference's
; global variables and spaces are: a state never shadows a space of the
; same name, nor a space a state.
!(change-state! &shared-name 5)
!(add-atom &shared-name (item 1))
!(test (collapse (match &shared-name (item $x) $x)) (1))
!(test (get-state &shared-name) 5)
!(change-state! &shared-name 6)
!(test (collapse (match &shared-name (item $x) $x)) (1))

; A stored value is a copy whose variables are fresh: binding the written
; variable leaves the stored one unbound, and two states written in one
; query keep distinct variables.
!(test (let $u (change-state! written (f $y)) (let $v (get-state written) (let $y 5 (let (f $z) $v (is-var $z))))) true)
!(test (let $u (change-state! first-state (k $p)) (let $u2 (change-state! second-state (m $p)) (let $v (get-state first-state) (let $w (get-state second-state) (let (k 1) $v (let (m $r) $w (is-var $r))))))) true)

; Reads share the stored value's variables, as nb_getval/2 answers the one
; stored term.
!(test (let $u (change-state! shared-vars (g $z)) (let $a (get-state shared-vars) (let $b (get-state shared-vars) (let (g 1) $a $b)))) (g 1))

; A value read before a change keeps its value.
!(change-state! kept (a b (c 1)))
!(test (let $old (get-state kept) (let $u (change-state! kept 3) (let $v (change-state! kept 4) $old))) (a b (c 1)))

; A state changed inside a failed transaction stays changed: global
; variables are outside the database's transactions.
!(change-state! outside 1)
!(test (collapse (transaction (let $u (change-state! outside 9) (empty)))) ())
!(test (get-state outside) 9)

; A counter over many changes, each reading the state it changes.
(= (bump-named $n)
   (if (== $n 0)
       (get-state bump-counter)
       (let $t (change-state! bump-counter (+ (get-state bump-counter) 1))
         (bump-named (- $n 1)))))
!(change-state! bump-counter 0)
!(test (bump-named 1000) 1000)
