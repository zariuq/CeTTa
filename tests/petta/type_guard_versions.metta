; A typed relation's calls to itself skip their protocol once a checked
; call's arguments meet the entry facts; every answer below is the one the
; complete protocol gives.

(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))

; A chainer-shaped relation: a knowledge space, a ground depth, and a
; polymorphic query whose instance is the result.
(: walk (-> $kb Nat $q $q))
(= (walk $kb $_ (: $p $t)) (match $kb (: $p $t) (: $p $t)))
(= (walk $kb (S $n) (: ($r $a) $t))
   (let* (((: $r (-> (: $a $u) $t)) (walk $kb $n (: $r (-> (: $a $u) $t))))
          ((: $a $u) (walk $kb $n (: $a $u))))
     (: ($r $a) $t)))

; Uninterpreted knowledge: the version runs.
!(bind! &kb1 (new-space))
!(add-atom &kb1 (: ax A))
!(add-atom &kb1 (: rule (-> (: $x A) B)))
!(collapse (walk &kb1 (S (S Z)) (: $prf B)))

; Knowledge naming a declared symbol: its rows are typed through Z's
; declaration, so the entry is refused.
!(bind! &kb2 (new-space))
!(add-atom &kb2 (: ax Z))
!(add-atom &kb2 (: rule (-> (: $x Z) B)))
!(collapse (walk &kb2 (S (S Z)) (: $prf B)))

; A query naming a declared symbol.
!(collapse (walk &kb1 (S (S Z)) (: $prf S)))

; A depth that is not ground, sharing a variable with the query: the query
; binds it to a proof term, which the depth guard then refuses.
!(collapse (walk &kb1 (S $d) (: $d B)))

; A rule named by a function: the result's application would run it.
(= (twice $x) ($x $x))
!(bind! &kb3 (new-space))
!(add-atom &kb3 (: ax A))
!(add-atom &kb3 (: twice (-> (: $x A) B)))
!(collapse (walk &kb3 (S (S Z)) (: $prf B)))

; Knowledge changed between runs is judged again.
!(add-atom &kb1 (: lift (-> (: $x B) Z)))
!(collapse (walk &kb1 (S (S (S Z))) (: $prf Z)))
!(collapse (walk &kb1 (S (S Z)) (: $prf B)))

; Duplicate answers stay distinct occurrences.
!(bind! &kb4 (new-space))
!(add-atom &kb4 (: ax A))
!(add-atom &kb4 (: ax A))
!(add-atom &kb4 (: rule (-> (: $x A) B)))
!(collapse (walk &kb4 (S (S Z)) (: $prf B)))

; A premise proved by an application of a declared function: the inner
; result guard rejects (S Z) for the application-shaped premise ($g $y),
; so the complete protocol has no answer here.
!(bind! &kb5 (new-space))
!(add-atom &kb5 (: rule (-> (: ($g $y) A) B)))
!(add-atom &kb5 (: (S Z) A))
!(collapse (walk &kb5 (S (S Z)) (: $prf B)))
