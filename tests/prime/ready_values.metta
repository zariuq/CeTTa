!(import! &self str)

; Ready values.  `return` hands back its datum unevaluated, a native
; operation reports a completed value, and an observation (collapse,
; collect, select, once, fold, fold-by-key) publishes the answers it
; observed: all are ready data.  Passing a ready value on never turns it
; back into a computation, even when an equation is headed by its
; constructor; the parts a pattern takes from it are ready too.  Only an
; explicit evaluation (eval, or the operand of chain) runs a ready value as
; code.  Ordinary results stay as they were: source still evaluates, and an
; argument nobody demands stays delayed.

(= (mk $x) (function (return (Box $x 1))))
(= (mk-pair $x) (function (return (Pair (Box $x 1) done))))
(= (Box $a $b) (Hostile $a))
(= (unbox-tuple ((Box $a $b))) $a)
(= (consume $s) (function (chain (eval (unbox-tuple ($s))) $r (return $r))))
(= (forward $s) (consume $s))

; Positive: a returned datum survives an equation named by its constructor,
; through let, chain, case, pattern parts and a direct argument.
!(let $s (mk 1) (consume $s))
!(let $s (mk 2) (forward $s))
!(chain (mk 3) $s (consume $s))
!(case (mk 4) (($s (consume $s))))
!(let (Pair $p $q) (mk-pair 5) (consume $p))
!(case (mk-pair 6) (((Pair $p $q) (consume $p))))
!(consume (mk 10))
(= (mk-nested $x) (function (return (Outer ((Box $x 1)) tail))))
!(let $o (mk-nested 21) (let (Outer ($b) $t) $o (consume $b)))
!(let $o (mk-nested 22) (unify $o (Outer ($b) $t) (consume $b) no))
!(let $s (mk 7) (Kept $s))
!(let $s (mk 8) (quote $s){$s})

; Positive: a builtin that builds a value reports it completed.
!(let $s (cons-atom Box (29 1)) (consume $s))

; Observations publish completed answers: the parts of a collapse, what
; collect, select and once observed, and a fold's items and answers are
; ready too, as are the items and answers of fold-by-key and the answers
; of a fold over a list.
!(let ($s) (collapse (mk 23)) (consume $s))
!(let $l (collapse (mk 24)) (let ($s) $l (consume $s)))
!(let $t (collapse (mk 41)) (let $s (superpose $t) (consume $s)))
!(let $c (collect (mk 43)) (let ($s) $c (consume $s)))
!(let $c (collect (mk 44)) $c)
!(let $s (select (mk 45)) (consume $s))
!(let $s (once (mk 46)) (consume $s))
!(fold (mk 25) 0 $acc $x (consume $x))
!(let $r (fold (superpose (26)) none $acc $x (mk $x)) (consume $r))
!(fold (superpose (1 2)) (mk 27) $acc $x (let $v (consume $acc) (mk $v)))
!(fold-by-key (mk 47) none $acc $x k (consume $x))
!(let $g (fold-by-key (superpose (48)) none $acc $x k (mk $x))
   (let ((k $s)) ($g) (consume $s)))
!(let $r (foldl-atom (1 2) 0 $acc $x (mk $x)) (consume $r))
!(foldl-atom (1 2) (mk 5) $acc $x (let $v (consume $acc) (mk $v)))
; Positive: native results are ready data, through the public wrappers.
(= (Utf8DecoderV1 $policy $tail $seen) (Hostile $policy))
!(let $decoder (str:utf8-new strict)
   (let $decoded (str:utf8-feed $decoder "ok" True)
     (Decoded $decoded)))
!(case (str:utf8-feed (str:utf8-new strict) "ab" False)
   (((Utf8DecodedV1 $text $state)
     (let $rest (str:utf8-feed $state "c" True) (Rest $text $rest)))))
(= (feed-rest (Utf8DecodedV1 $text $state)) (str:utf8-feed $state "c" True))
!(feed-rest (str:utf8-feed (str:utf8-new strict) "ab" False))
!(let $decoded (str:utf8-feed (str:utf8-new strict) "ab" False)
   (unify $decoded (Utf8DecodedV1 $text $state)
     (str:utf8-feed $state "c" True)
     no-match))


; Explicit evaluation runs a ready value as code; a lazy parameter's own
; computation runs once.
!(let $s (mk 12) (eval $s))
!(let $s (mk 16) (chain $s $r (Ran $r)))
(= (evaluate-argument $x) (eval $x))
!(evaluate-argument (mk 13))

; Across a space boundary a ready value is stored as itself, and a match
; hands its parts back as ready values.
!(let $s (mk 17) (add-atom &self (Kept-value $s)))
!(match &self (Kept-value $k) (quote $k){$k})
!(match &self (Kept-value $k) (consume $k))

; Occurrences of one ready value are one value; distinct results keep
; their multiplicity.
!(let $s (mk 9) (Twice (consume $s) (consume $s)))
!(let $s (superpose ((mk 11) (mk 11))) (consume $s))

; A completed value with free variables keeps them shared with the code
; around it: a template mapped over a list a function received binds its
; variable once for each element, and nothing else reaches the space.
(= (map-into $space $items) (map-atom $items $d (add-atom $space $d)))
!(bind! &ready-values:store (new-space))
!(map-into &ready-values:store ((Item 1) (Item 2)))
!(match &ready-values:store (Item 1) one)
!(match &ready-values:store (Item 2) two)
!(size-atom (collapse (match &ready-values:store $x $x)))

; Saved answers restore the bindings they were saved with, whether the
; tuple arrives through let or through chain.
!(let $saved (collapse-bind (superpose (31 32))) (superpose-bind $saved))
!(chain (collapse-bind (superpose (33 34))) $saved (superpose-bind $saved))

; Negative: source still evaluates, including source that mentions a
; ready value, and an ordinary result's fields are computations.
!(consume (Box 18 1))
!(let $s (mk 19) (Box $s 0))
!(collapse (Box 28 1))
!(let $r (fold (superpose ()) (+ 14 15) $acc $x $acc) (* $r 2))
(= (double $x) (* 2 $x))
!(let $n (+ 6 6) (double $n))
(= (weight $x) (+ $x 1))
(= (feature $x) (Feature $x (weight $x)))
!(let (Feature $a $w) (feature 20) (* $w 2))

; Laziness: an argument the callee never demands stays delayed beside a
; ready value.
!(bind! &ready-values:count (new-state 0))
(= (ready-values:bump)
   (let $old (get-state &ready-values:count)
     (let $_ (change-state! &ready-values:count (+ $old 1)) $old)))
(= (ignore-second $v $x) (consume $v))
(= (keep-syntax $x) (quote $x){$x})
!(let $s (mk 14) (ignore-second $s (ready-values:bump)))
!(let $s (mk 15) (keep-syntax (ready-values:bump)))
!(get-state &ready-values:count)

; Equality of fields observes ready data without running its constructor.
(= (ready-pair-same) (function (return (ReadyDuo (Box 51 1) (Box 51 1)))))
(= (ready-pair-other) (function (return (ReadyDuo (Box 51 1) (Box 52 1)))))
(= (ready-pair-check (ReadyDuo $x $x)) same)
(= (ready-pair-check (ReadyDuo $x $y)) different)
!(ready-pair-check (ready-pair-same))
!(ready-pair-check (ready-pair-other))

; Logical refinement can finish an open source field. The same syntax
; explicitly returned as data keeps its completed role after refinement.
(= (ready-field (ReadyField $x)) $x)
(= (ready-refine $x) (unify $x 5 ok no))
!(let $p (ReadyField (+ $y 1))
   (let $r (ready-refine $y) (ready-field $p)))
!(let $p (function (return (ReadyField (+ $y 1))))
   (let $r (ready-refine $y) (ready-field $p)))

; A completed syntax field keeps its binder and body visible to patterns;
; observing the field does not execute its body.
(= (ready-lambda-fields (: (lam $v (ReadyAnd $f $g)) (-> $a Bool)))
   (LambdaFields $a))
!(ready-lambda-fields (: (lam $x (ReadyAnd (F $x) (G $x))) (-> UInt Bool)))
!(ready-lambda-fields
   (function (return (: (lam $x (ReadyAnd (F $x) (G $x))) (-> UInt Bool)))))

; Extracting an open field before refining its variables preserves the
; distinction between a returned datum and an ordinary computation.
!(let $p (function (return (ReadyField (+ $y 1))))
   (let $v (ready-field $p) (let $r (ready-refine $y) $v)))
!(let $p (ReadyField (+ $y 1))
   (let $v (ready-field $p) (let $r (ready-refine $y) $v)))
