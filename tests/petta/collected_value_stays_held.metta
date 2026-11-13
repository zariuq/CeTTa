; Code a value holds stays held when the value is collected: a parameter's
; value an activation spliced into a body is read as that value, never run
; again, also where the body is compiled on the open tier.
(= (taken $request)
   (let ($_t $value $type) $request
     (let $found (collapse (let $type (get-metatype $value) (answered true $request)))
       (let ($first $rest) (decons-atom $found) (h $first $rest)))))
(: held (-> Atom %Undefined%))
(= (held $x) (taken $x))
!(held (guard (got 6 (+ 4 2)) Expression))

; The body's own written code still runs beside the held value.
(= (computed $request)
   (collapse (let $type (get-metatype $request) (answered (+ 4 2) $type $request))))
(: held-computed (-> Atom %Undefined%))
(= (held-computed $x) (computed $x))
!(held-computed (got (+ 4 2)))

; Quoted code in the body is its written operand.
(= (quoted $request)
   (collapse (let $t (get-metatype $request) (pair (quote (+ 1 2)) $request))))
(: held-quoted (-> Atom %Undefined%))
(= (held-quoted $x) (quoted $x))
!(held-quoted (+ 4 2))

; A value the body binds itself is held as well.
(= (bound-later $request)
   (collapse (let $copy $request (let $t (get-metatype $copy) (kept $copy)))))
(: held-bound (-> Atom %Undefined%))
(= (held-bound $x) (bound-later $x))
!(held-bound (+ 4 2))

; Controls of the row: no collapse, and the item listed with cons-atom.
(= (l2 $a $b) (cons-atom $a (cons-atom $b ())))
(= (plain $list) (let ($first $rest) (decons-atom $list) (h $first $rest)))
(: held-plain (-> Atom %Undefined%))
(= (held-plain $x) (plain $x))
!(held-plain ((got 6 (+ 4 2)) more))
(= (listed $list) (let ($first $rest) (decons-atom $list) (l2 $first $rest)))
(: held-listed (-> Atom %Undefined%))
(= (held-listed $x) (listed $x))
!(held-listed ((got 6 (+ 4 2)) more))

; An unheld argument is evaluated before the call, as written.
(= (evaluated $x) (collapse (let $t (get-metatype $x) (seen $x))))
!(evaluated (+ 4 2))
