;; PeTTa-dialect port of Nil's typed-composition synthesis experiment,
;; written in the idiom of his bfc-xp.metta obc/bc chainers:
;; spaces as arguments, direct match, one clause per rule arity.

;; Nat depth (bfc-xp.metta style)
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

;; Knowledge base: typed atoms
!(bind! &kb (new-space))
!(add-atom &kb (: f (-> Number String)))
!(add-atom &kb (: g (-> String Bool)))
!(add-atom &kb (: h (-> Bool Number)))
!(add-atom &kb (: i (-> String Number Bool)))

;; Rule base: flat n-ary arrows, premises then conclusion
!(bind! &rb (new-space))
!(add-atom &rb (: compose (-> (-> $b $c) (-> $a $b) (-> $a $c))))
!(add-atom &rb (: blackbird (-> (-> $c $d) (-> $a $b $c) (-> $a $b $d))))

;; Backward chainer, Nil bc style
;; Base case: query provable directly from the kb
(= (bcn $kb $rb $_ (: $x $a)) (match $kb (: $x $a) (: $x $a)))
;; Binary rule application
(= (bcn $kb $rb (S $k) (: ($f $x $y) $c))
   (let* (((: $f (-> $a $b $c)) (match $rb (: $f (-> $a $b $c)) (: $f (-> $a $b $c))))
          ((: $x $a) (bcn $kb $rb $k (: $x $a)))
          ((: $y $b) (bcn $kb $rb $k (: $y $b))))
     (: ($f $x $y) $c)))

!(test (collapse (bcn &kb &rb (fromNumber 1) (: $t (-> String Number Number))))
       ((: (blackbird h i) (-> String Number Number))))
