; A space new-space makes has a generated name.  It prints as &space(N), its
; metatype is Symbol, typed calls take it as a Symbol, and its text crosses to
; Prolog as an atom.  The name is the space's identity, not its text: the
; text built again (here from repr, with atom_concat) is an ordinary symbol,
; which names a space of its own once written to and never reaches the
; generated one, while that space lives and after it has been collected and
; its memory used again.  A name held in a row, a closure, a collected bag,
; a pending alternative or another space's row keeps its space alive.  The
; numbers count every new-space and skip a name in use, and no collection
; changes them.
(: as-symbol (-> Symbol Bool))
(= (as-symbol $x) True)
(: as-grounded (-> Grounded Bool))
(= (as-grounded $x) True)
(= (burn $n $acc) (if (== $n -1) (println! unreachable) (if (== $n 0) $acc (burn (- $n 1) (payload $n r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 r10 r11 r12 r13 r14 r15)))))
(= (kept-space $n) (let $s (new-space) (let $_ (add-atom $s (kept $n)) $s)))

; The presentation, and identity.
!(let $s (new-space) (Shown (get-metatype $s) (as-symbol $s) (collapse (as-grounded $s)) (is-space $s)))
!(let* (($a (new-space)) ($b (new-space))) (Distinct (== $a $b) (== $a $a)))

; The text built again refuses access to the live space: a read finds none
; of its rows, and a write lands in the symbol's own space.
!(let* (($s (new-space)) ($_ (add-atom $s (secret 1))) ($t (repr $s)) ($forged (atom_concat $t ""))) (Forged (get-metatype $forged) (== $forged $s) (collapse (match $forged (secret $x) $x))))
!(let* (($s (new-space)) ($t (repr $s)) ($forged (atom_concat $t "")) ($_ (add-atom $forged (planted 2)))) (Planted (collapse (match $s (planted $x) $x)) (collapse (match $forged (planted $x) $x))))

; Only a space's text is kept; the space is collected and new spaces use
; its memory again; reads and writes through the text still refuse access.
!(let* (($s (new-space)) ($_ (add-atom $s (secret 3))) ($t (repr $s))) (add-atom &texts (text $t)))
!(let $_ (burn 4096 seed) burned)
!(let $n (new-space) (add-atom $n (secret 4)))
!(match &texts (text $t) (Collected (collapse (let $f (atom_concat $t "") (match $f (secret $x) $x)))))
!(match &texts (text $t) (let* (($f (atom_concat $t "")) ($_ (add-atom $f (planted 5)))) (Written (collapse (match $f (planted $x) $x)))))

; A held name keeps its space alive across a collection.
!(let $s (kept-space 6) (add-atom &holders (holder $s)))
!(let* (($s (kept-space 7)) ($f (|-> () (collapse (match $s (kept $x) $x))))) (add-atom &closures (closure $f)))
!(let* (($inner (kept-space 8)) ($outer (new-space)) ($_ (add-atom $outer (inner $inner)))) (add-atom &holders (outer $outer)))
!(let $_ (burn 4096 seed) burned)
!(match &holders (holder $s) (Held (collapse (match $s (kept $x) $x))))
!(match &closures (closure $f) (Closure ($f)))
!(match &holders (outer $o) (Composed (collapse (match $o (inner $i) (collapse (match $i (kept $x) $x))))))
!(let $bag (collapse (kept-space 9)) (let $_ (burn 4096 seed) (Bag (collapse (let $s (superpose $bag) (match $s (kept $x) $x))))))
!(let* (($a (kept-space 10)) ($b (kept-space 11))) (Pending (collapse (let $s (superpose ($a $b)) (let $_ (burn 4096 seed) (match $s (kept $x) $x))))))

; An imported module fills a generated space as any other.
!(let $s (new-space) (let $_ (import! $s fixtures/generated_space_module) (Imported (collapse (match $s (module-fact $x) $x)))))

; The numbers count on, past a name a program wrote to.
!(let $t (atom_concat "&space(" "16)") (add-atom $t (occupied)))
!(let $s (new-space) $s)
!(let $s (new-space) $s)
