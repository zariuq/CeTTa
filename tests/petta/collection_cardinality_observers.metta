; Cardinality consumes every occurrence, effects and faults included, without
; treating a collection held as data as a new computation.
!(size-atom (collapse (superpose (1 1 2))))
!(length (collapse (superpose (1 1 2))))
!(size-atom (collapse (match &self (absent $x) $x)))
!(let $v (quote (collapse (superpose (1 1 2)))) (size-atom $v))
!(let $v (quote (collapse (superpose (1 1 2)))) (length $v))
!(let $v (collapse (superpose (1 1 2))) (size-atom $v))
!(size-atom (collapse (let $x (superpose (a b)) (let $_ (println! $x) $x))))
!(let (Error $formal $context) (catch (size-atom (collapse (sqrt-math -1.0)))) $formal)
!(size-atom (collapse (catch (sqrt-math -1.0))))
!(catch (length (quote (cons a b))))
