; A space new-space makes has a generated name.  It prints as &space(...)
; around a numeral: dot-separated parts of decimal or hexadecimal digits,
; none empty.  Its metatype is Symbol, typed calls take it as a Symbol,
; and its text crosses to Prolog as an atom.  Two spaces have two names, and
; a name equals only itself.  The name is the space's identity, not its
; text: the text built again (here from repr, with atom_concat) or read back
; (with atom_codes) is an ordinary symbol, which names a space of its own
; once written to and never reaches the generated one, while that space
; lives and after it has been collected and its memory used again.  A name
; held in a row, a closure, a collected bag, a pending alternative or
; another space's row keeps its space alive, and a live name never changes.
; A new name never collides with a name in use, a symbol a program wrote to
; as a space included.  Which numeral a name holds is not specified: every
; check below holds whatever names the spaces are given.
(: as-symbol (-> Symbol Bool))
(= (as-symbol $x) True)
(: as-grounded (-> Grounded Bool))
(= (as-grounded $x) True)
(= (burn $n $acc) (if (== $n -1) (println! unreachable) (if (== $n 0) $acc (burn (- $n 1) (payload $n r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 r10 r11 r12 r13 r14 r15)))))
(= (kept-space $n) (let $s (new-space) (let $_ (add-atom $s (kept $n)) $s)))
; Whether text spells &space( around a numeral and ): character codes that
; are dot-separated parts, each of one or more decimal or hexadecimal
; digits.  $open: no digit since the start or the last dot.
(= (numeral-digit $c) (or (and (>= $c 48) (<= $c 57)) (and (>= $c 97) (<= $c 102))))
(= (numeral-parts $cs $open)
   (if (== $cs ()) (if $open False True)
     (let $c (car-atom $cs)
       (if (== $c 46)
           (if $open False (numeral-parts (cdr-atom $cs) True))
           (if (numeral-digit $c) (numeral-parts (cdr-atom $cs) False) False)))))
(= (numeral-name $t)
   (let* (($_ (translatePredicate (atom_concat "&space(" $rest $t)))
          ($_ (translatePredicate (atom_concat $digits ")" $rest)))
          ($_ (translatePredicate (atom_codes $digits $cs))))
     (numeral-parts $cs True)))
(= (spelled-name? $t) (if (== (collapse (numeral-name $t)) (True)) True False))
; The text &space(K) for K from 1 to N, each written to as a space.
(= (spelled $k) (atom_concat "&space(" (atom_concat $k ")")))
(= (occupy $k $n) (if (> $k $n) done (let $_ (add-atom (spelled $k) (occupied $k)) (occupy (+ $k 1) $n))))
(= (collisions $new $k $n) (if (> $k $n) 0 (+ (if (== $new (spelled $k)) 1 0) (collisions $new (+ $k 1) $n))))

; The presentation, and identity.
!(let $s (new-space) (Shown (get-metatype $s) (as-symbol $s) (collapse (as-grounded $s)) (is-space $s)))
!(let $s (new-space) (Spelled (spelled-name? (repr $s)) (spelled-name? "&space(x)") (spelled-name? "&spaces(3)") (spelled-name? "&space()")))
; The law's controls: a root's, a child's and a keyed block's numerals; an
; empty part at either end or between dots, a letter past f, a blank.
!(Law (spelled-name? "&space(12)") (spelled-name? "&space(0.3.1)") (spelled-name? "&space(529d30b948a8330fdb6a1996a5480869.0.1)") (spelled-name? "&space(.1)") (spelled-name? "&space(1.)") (spelled-name? "&space(1..2)") (spelled-name? "&space(0.g)") (spelled-name? "&space(1 2)"))
!(let* (($a (new-space)) ($b (new-space))) (Distinct (== $a $b) (== $a $a) (== (repr $a) (repr $b))))

; The text built again refuses access to the live space: a read finds none
; of its rows, and a write lands in the symbol's own space.
!(let* (($s (new-space)) ($_ (add-atom $s (secret 1))) ($t (repr $s)) ($forged (atom_concat $t ""))) (Forged (get-metatype $forged) (== $forged $s) (collapse (match $forged (secret $x) $x))))
!(let* (($s (new-space)) ($t (repr $s)) ($forged (atom_concat $t "")) ($_ (add-atom $forged (planted 2)))) (Planted (collapse (match $s (planted $x) $x)) (collapse (match $forged (planted $x) $x))))
!(let* (($s (new-space)) ($_ (add-atom $s (secret 1))) ($t (repr $s)) ($_ (translatePredicate (atom_codes $t $cs))) ($_ (translatePredicate (atom_codes $read $cs)))) (Read (get-metatype $read) (== $read $s) (collapse (match $read (secret $x) $x))))

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
; A live name never changes, through a collection and the names made after.
!(let* (($s (kept-space 12)) ($before (repr $s)) ($_ (burn 4096 seed)) ($o (new-space)) ($after (repr $s))) (Stable (== $before $after) (== (repr $o) $before) (collapse (match $s (kept $x) $x))))

; An imported module fills a generated space as any other.
!(let $s (new-space) (let $_ (import! $s fixtures/generated_space_module) (Imported (collapse (match $s (module-fact $x) $x)))))

; A new name never collides with a name in use: with the texts &space(1)
; to &space(100) each written to as a space, each new name equals none of
; them and its space holds none of their rows.
!(occupy 1 100)
!(let $s (new-space) (Fresh (collisions $s 1 100) (spelled-name? (repr $s)) (collapse (match $s (occupied $x) $x))))
!(let $s (new-space) (Fresh (collisions $s 1 100) (spelled-name? (repr $s)) (collapse (match $s (occupied $x) $x))))
!(let* (($a (new-space)) ($b (new-space))) (Fresh (== $a $b) (collisions $a 1 100) (collisions $b 1 100)))
