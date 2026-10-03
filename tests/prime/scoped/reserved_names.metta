; A call whose head Prime interprets itself is answered without the
; program's equations, so an equation or a type declaration the program
; writes for that name, in the space it runs in, is refused in its place,
; with the name and the reason: a grounded operation, a builtin of Prime's
; profile, a judgment, or a form of the evaluator.  The rest of the program
; is loaded and runs.
(= (size $x) (my-size $x))
(: size (-> Expression Number))
(= (+ $a $b) (my-plus $a $b))
(= (try $v $c) (go $c $v))
(= (if $c $a $b) (my-if $c $a $b))
(= (collect $x) (my-collect $x))
; The builtins answer as before, once each.
!(size (a b c))
!(+ 1 2)
!(if True yes no)
; The refusal applies to equations added while the program runs, too.
!(add-atom &self (= (once $x) (my-once $x)))
!(add-atom &self (: repr (-> Number Number)))
!(once (superpose (1 2)))
; An ordinary name is the program's own.
(= (my-size $x) 42)
(= (carrier-size $x) 7)
!(my-size (a b c))
!(carrier-size (a b c))

; A definition by `set:define` or `set:inductive` of such a name that the
; space already declares through Prime's library cannot be admitted beside
; that declaration, and is refused the same way.
!(set:inductive &self nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(try (set:define &self repr (-> nat nat) (= (repr $n) (succ $n))))
!(try (set:inductive &self box (u 0) (: size box)))
!(set:define &self plus1 (-> nat nat) (= (plus1 $n) (succ $n)))
!(plus1 zero)
; A name the library does not declare may name a constant: the definition
; takes effect, when run and in judgments.
!(set:define &self and (-> nat nat nat) (= (and $m $n) $m))
!(and (succ zero) zero)
!(type:eq (and (succ zero) zero) (succ zero))
; A theory in a space of its own may name its constants after them, and
; declarations added there are its own.
!(bind! &thy (new-space))
!(set:inductive &thy bool (u 0) (: true bool) (: false bool))
!(set:define &thy and (-> bool bool bool) (= (and true $b) $b) (= (and false $b) false))
!(type:eq &thy (and true false) false)
!(set:define &thy repr (-> bool bool) (= (repr $b) $b))
!(type:eq &thy (repr true) true)
!(add-atom &thy (: size bool))
!(match &thy (: size $type) $type)

; The words of the term syntax are not reserved: a program may define them,
; and its definition takes effect when run and in judgments.
(= (pair $a $b) (my-pair $a $b))
!(pair 1 2)
!(bind! &tok (new-space))
!(set:inductive &tok token (u 0) (: a token) (: b token) (: pair (-> token token token)))
!(set:define &tok first (-> token token) (= (first a) a) (= (first b) b) (= (first (pair $x $y)) $x))
!(type:eq &tok (first (pair b a)) b)
