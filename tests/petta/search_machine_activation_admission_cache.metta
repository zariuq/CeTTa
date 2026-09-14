; The same compiled clause bodies are observed before, during, and after a
; mutable translator-registry change.  Cached activation admission must be
; reused while authority is stable and recomputed at both mutation edges.

(= (lane one) ok)
(= (lane two) (empty))
(= (lane three) (empty))
(= (lane four) (empty))

(= (walk $n (one (-> (-> $a (-> $b $c)) (-> (-> $d $e) (-> $f $g)))))
   (let $_ (lane one)
     (if (== $n 0) done (walk (- $n 1) $next))))
(= (walk $n (two (-> (-> $a (-> $b $c)) (-> (-> $d $e) (-> $f $g)))))
   (let $_ (lane two)
     (if (== $n 0) never (walk (- $n 1) $next))))
(= (walk $n (three (-> (-> $a (-> $b $c)) (-> (-> $d $e) (-> $f $g)))))
   (let $_ (lane three)
     (if (== $n 0) never (walk (- $n 1) $next))))
(= (walk $n (four (-> (-> $a (-> $b $c)) (-> (-> $d $e) (-> $f $g)))))
   (let $_ (lane four)
     (if (== $n 0) never (walk (- $n 1) $next))))

!(let $before (walk 2 $first-proof)
   (let $_ (add-translator-rule! lane)
     (let $during (walk 2 $second-proof)
       (let $_ (remove-translator-rule! lane)
         (walk 2 $third-proof)))))
