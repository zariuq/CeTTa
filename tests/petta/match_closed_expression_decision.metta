; Closed expression matching is structural equality and has no binding effect.
; These independent term geometries keep both equal and unequal decisions live.
(= (closed-equal $x $x) equal)

!(closed-equal
   (unary (unary (unary (unary (unary (unary leaf))))))
   (unary (unary (unary (unary (unary (unary leaf)))))))
!(collapse (closed-equal
   (unary (unary (unary (unary (unary (unary leaf))))))
   (unary (unary (unary (unary (unary (unary other))))))))

!(closed-equal
   (wide a b c d e f g h i j k l)
   (wide a b c d e f g h i j k l))
!(collapse (closed-equal
   (wide a b c d e f g h i j k l)
   (wide a b c d e f g h i j k other)))

!(closed-equal
   (node (node (leaf a) (leaf b)) (node (leaf c) (leaf d)))
   (node (node (leaf a) (leaf b)) (node (leaf c) (leaf d))))
!(collapse (closed-equal
   (node (node (leaf a) (leaf b)) (node (leaf c) (leaf d)))
   (node (node (leaf a) (leaf b)) (node (leaf c) (leaf other)))))

!(closed-equal
   (evidence (claim rain) (support (source sensor-a) (weight 7 10)))
   (evidence (claim rain) (support (source sensor-a) (weight 7 10))))
!(collapse (closed-equal
   (evidence (claim rain) (support (source sensor-a) (weight 7 10)))
   (evidence (claim rain) (support (source sensor-b) (weight 7 10)))))

!(closed-equal
   (Apply (Lambda (Binder x) (Pair (Var x) (Quote (Var y))))
          (Quote (Constructor payload)))
   (Apply (Lambda (Binder x) (Pair (Var x) (Quote (Var y))))
          (Quote (Constructor payload))))
!(collapse (closed-equal
   (Apply (Lambda (Binder x) (Pair (Var x) (Quote (Var y))))
          (Quote (Constructor payload)))
   (Apply (Lambda (Binder x) (Pair (Var x) (Quote (Var z))))
          (Quote (Constructor payload)))))

; Exercise the core space matcher, where each closed pattern is compared with
; five independently stored rows.  Each family has one hit and one near miss.
!(bind! &closed-expression-decision (new-space))
!(add-atom &closed-expression-decision
   (unary (unary (unary (unary (unary (unary leaf)))))))
!(add-atom &closed-expression-decision
   (wide a b c d e f g h i j k l))
!(add-atom &closed-expression-decision
   (node (node (leaf a) (leaf b)) (node (leaf c) (leaf d))))
!(add-atom &closed-expression-decision
   (evidence (claim rain) (support (source sensor-a) (weight 7 10))))
!(add-atom &closed-expression-decision
   (Apply (Lambda (Binder x) (Pair (Var x) (Quote (Var y))))
          (Quote (Constructor payload))))

!(match &closed-expression-decision
   (unary (unary (unary (unary (unary (unary leaf))))))
   (hit unary))
!(collapse (match &closed-expression-decision
   (unary (unary (unary (unary (unary (unary other))))))
   (hit unary-mismatch)))
!(match &closed-expression-decision
   (wide a b c d e f g h i j k l)
   (hit wide))
!(collapse (match &closed-expression-decision
   (wide a b c d e f g h i j k other)
   (hit wide-mismatch)))
!(match &closed-expression-decision
   (node (node (leaf a) (leaf b)) (node (leaf c) (leaf d)))
   (hit balanced))
!(collapse (match &closed-expression-decision
   (node (node (leaf a) (leaf b)) (node (leaf c) (leaf other)))
   (hit balanced-mismatch)))
!(match &closed-expression-decision
   (evidence (claim rain) (support (source sensor-a) (weight 7 10)))
   (hit evidence))
!(collapse (match &closed-expression-decision
   (evidence (claim rain) (support (source sensor-b) (weight 7 10)))
   (hit evidence-mismatch)))
!(match &closed-expression-decision
   (Apply (Lambda (Binder x) (Pair (Var x) (Quote (Var y))))
          (Quote (Constructor payload)))
   (hit syntax))
!(collapse (match &closed-expression-decision
   (Apply (Lambda (Binder x) (Pair (Var x) (Quote (Var z))))
          (Quote (Constructor payload)))
   (hit syntax-mismatch)))
