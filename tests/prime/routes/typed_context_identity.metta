; A function type (-> A B) is data, like any other constructor
; application: nothing computes it.  A context that carries one is therefore
; passed as a value, and the variables it holds keep their identity.

(= (has nil $v) False)
(= (has (ctx (ov $x $t) $rest) $v)
   (if (noreduce-eq $x $v) True (has $rest $v)))

; Each binder of a code term enters the context; at the bottom both
; variables of the body are found there.
(= (walk $ctx $term)
   (unify $term (quote (lam $x $body))
     (walk (ctx (ov $x T) $ctx) @$body)
     (unify $term (quote (f $y $z))
       (Found (has $ctx $y) (has $ctx $z))
       none)))
(= (top $dom $term)
   (unify $term (quote (lam $a $rest))
     (walk (ctx (ov $a $dom) nil) @$rest)
     none))
!(top (-> P Bool) @(lam $a (lam $x (lam $z (f $x $z)))))
!(top (Rel P Bool) @(lam $a (lam $x (lam $z (f $x $z)))))

; A value that holds a type is taken apart by unify.
(= (check $d $v) (unify $d (w (pair $x $t)) (noreduce-eq $x $v) no))
(= (keep $c $v) (check (w $c) $v))
(= (start $v) (keep (pair $v (-> P Bool)) $v))
!(start $q)

; A variable that is not in the context is not found.
(= (absent $v) (has (ctx (ov $other (-> P Bool)) nil) $v))
!(absent $q)

; A function type evaluates to itself.
!(-> P Bool)
