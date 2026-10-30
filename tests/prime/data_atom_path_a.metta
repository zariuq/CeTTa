(= (foo $x) (done $x))
(= (my-car $e) (car-atom $e))
(= (my-cdr $e) (cdr-atom $e))
(= (my-decons $e) (decons-atom $e))
(: id-atom (-> Proc Proc))
(= (id-atom $x) $x)
(= (Box $a $b) (Hostile $a))

; P1: Data is not run inside arithmetic (+ $x 1 -> (+ (+ 1 2) 1))
!(let $x (cons-atom + (1 2)) (+ $x 1))
!(let (quote $x) (quote (+ 1 2)) (+ $x 1))

; P1e: Explicit eval runs Data (+ (eval $x) 1 -> 4)
!(let $x (cons-atom + (1 2)) (+ (eval $x) 1))
!(let (quote $x) (quote (+ 1 2)) (+ (eval $x) 1))

; P2: cons-atom builds Data; let preserves Data ((+ 1 2))
!(let $d (cons-atom + (1 2)) (let $y $d $y))

; P3: Atom-typed parameter preserves Data ((+ 1 2))
!(let $d (cons-atom + (1 2)) (id-atom $d))

; P4a: car-atom over non-Data does not stamp Data; observation normalizes (((Field 3)))
!(let $p (car-atom (((Field (+ 1 2))))) $p)

; P4b: car-atom over non-Data does not stamp Data; sort-atom normalizes (((Field 3)))
!(let $p (car-atom (((Field (+ 1 2))))) (sort-atom $p))

; P4c: let-bound non-Data destructured by car-atom agrees with P4b (((Field 3)))
!(let $xs (((Field (+ 1 2)))) (let $p (car-atom $xs) (sort-atom $p)))

; P5: car-atom and cdr-atom preserve Data marks; rebuilt Data equals original Data (both inline and let-bound -> True)
!(let $d (cons-atom + (1 2)) (== $d (cons-atom (car-atom $d) (cdr-atom $d))))
!(let $d (cons-atom + (1 2)) (let $h (car-atom $d) (let $t (cdr-atom $d) (== $d (cons-atom $h $t)))))
!(let $d (cons-atom (cons-atom + (1 2)) ((cons-atom + (2 3)))) (== $d (cons-atom (car-atom $d) (cdr-atom $d))))
!(let $d (cons-atom (cons-atom + (1 2)) ((cons-atom + (2 3)))) (let $h (car-atom $d) (let $t (cdr-atom $d) (== $d (cons-atom $h $t)))))

; P5h: constructor-shaped equation (= (Box $a $b) (Hostile $a)); Data (Box 30 1) from cons-atom never fires equation
!(let $d (cons-atom Box (30 1)) $d)
!(let $d (cons-atom Box (30 1)) (== $d (cons-atom Box (30 1))))
!(let $d (cons-atom Box (30 1)) (eval $d))

; P6: car-atom evaluates its argument to a value (done)
!(car-atom (foo bar))

; P7: wrapper congruence with P6 (done)
!(my-car (foo bar))

; P8: let congruence with P6/P7 (done)
!(let $e (foo bar) (car-atom $e))

; P9: quote is a constructor that blocks evaluation while allowing unification over variables inside it
!(car-atom (quote (foo bar)))
!(cdr-atom (quote (foo bar)))
!(decons-atom (quote (foo bar)))

; cdr-atom analogues of P4a, P4b, P4c, P5, P5h, P6, P7, P8
!(let $p (cdr-atom (head (Field (+ 1 2)))) $p)
!(let $p (cdr-atom (head (Field (+ 1 2)))) (sort-atom $p))
!(let $xs (head (Field (+ 1 2))) (let $p (cdr-atom $xs) (sort-atom $p)))
!(let $d (cons-atom head (cons-atom Box (30 1))) (cdr-atom $d))
!(let $d (cons-atom head (cons-atom Box (30 1))) (== (cdr-atom $d) (cons-atom Box (30 1))))
!(cdr-atom (foo bar))
!(my-cdr (foo bar))
!(let $e (foo bar) (cdr-atom $e))

; decons-atom analogues of P4a, P4b, P4c, P5, P5h, P6, P7, P8
!(let ($h $t) (decons-atom (((Field (+ 1 2))) (Field (+ 2 3)))) ($h $t))
!(let ($h $t) (decons-atom (((Field (+ 1 2))) (Field (+ 2 3)))) ((sort-atom $h) (sort-atom $t)))
!(let $xs (((Field (+ 1 2))) (Field (+ 2 3))) (let ($h $t) (decons-atom $xs) ((sort-atom $h) (sort-atom $t))))
!(let $d (cons-atom + (1 2)) (let ($h $t) (decons-atom $d) (== $d (cons-atom $h $t))))
!(let $b (cons-atom Box (30 1)) (let $d (cons-atom $b $b) (let ($h $t) (decons-atom $d) ($h $t))))
!(decons-atom (foo bar))
!(my-decons (foo bar))
!(let $e (foo bar) (decons-atom $e))

; union-atom and unique-atom over Data
!(let $u (union-atom (cons-atom + ()) (1 2)) (+ $u 1))
!(let $u (union-atom (cons-atom + ()) (1 2)) (eval $u))
!(let $b (cons-atom Box (30 1)) (let $c (cons-atom $b ($b)) (unique-atom $c)))

; Law 1 (let vs inline congruence): 42-row census across 7 positions x 6 argument forms
; 1. car-atom across 6 argument forms
!(== (car-atom (foo bar)) (let $e (foo bar) (car-atom $e)))
!(== (car-atom (cons-atom a (b))) (let $e (cons-atom a (b)) (car-atom $e)))
!(== (car-atom (let $x (foo a) $x)) (let $e (let $x (foo a) $x) (car-atom $e)))
!(== (car-atom (case (foo a) (((done $x) ($x b))))) (let $e (case (foo a) (((done $x) ($x b)))) (car-atom $e)))
!(== (car-atom (chain (foo a) $x $x)) (let $e (chain (foo a) $x $x) (car-atom $e)))
!(== (car-atom (((Field (+ 1 2))) ((Field (+ 2 3))))) (let $e (((Field (+ 1 2))) ((Field (+ 2 3)))) (car-atom $e)))

; 2. cdr-atom across 6 argument forms
!(== (cdr-atom (foo bar)) (let $e (foo bar) (cdr-atom $e)))
!(== (cdr-atom (cons-atom a (b))) (let $e (cons-atom a (b)) (cdr-atom $e)))
!(== (cdr-atom (let $x (foo a) $x)) (let $e (let $x (foo a) $x) (cdr-atom $e)))
!(== (cdr-atom (case (foo a) (((done $x) ($x b))))) (let $e (case (foo a) (((done $x) ($x b)))) (cdr-atom $e)))
!(== (cdr-atom (chain (foo a) $x $x)) (let $e (chain (foo a) $x $x) (cdr-atom $e)))
!(== (cdr-atom (((Field (+ 1 2))) ((Field (+ 2 3))))) (let $e (((Field (+ 1 2))) ((Field (+ 2 3)))) (cdr-atom $e)))

; 3. decons-atom across 6 argument forms
!(== (decons-atom (foo bar)) (let $e (foo bar) (decons-atom $e)))
!(== (decons-atom (cons-atom a (b))) (let $e (cons-atom a (b)) (decons-atom $e)))
!(== (decons-atom (let $x (foo a) $x)) (let $e (let $x (foo a) $x) (decons-atom $e)))
!(== (decons-atom (case (foo a) (((done $x) ($x b))))) (let $e (case (foo a) (((done $x) ($x b)))) (decons-atom $e)))
!(== (decons-atom (chain (foo a) $x $x)) (let $e (chain (foo a) $x $x) (decons-atom $e)))
!(== (decons-atom (((Field (+ 1 2))) ((Field (+ 2 3))))) (let $e (((Field (+ 1 2))) ((Field (+ 2 3)))) (decons-atom $e)))

; 4. size-atom across 6 argument forms
!(== (size-atom (foo bar)) (let $e (foo bar) (size-atom $e)))
!(== (size-atom (cons-atom a (b))) (let $e (cons-atom a (b)) (size-atom $e)))
!(== (size-atom (let $x (foo a) $x)) (let $e (let $x (foo a) $x) (size-atom $e)))
!(== (size-atom (case (foo a) (((done $x) ($x b))))) (let $e (case (foo a) (((done $x) ($x b)))) (size-atom $e)))
!(== (size-atom (chain (foo a) $x $x)) (let $e (chain (foo a) $x $x) (size-atom $e)))
!(== (size-atom (((Field (+ 1 2))) ((Field (+ 2 3))))) (let $e (((Field (+ 1 2))) ((Field (+ 2 3)))) (size-atom $e)))

; 5. cons-atom head across 6 argument forms
!(== (cons-atom (foo bar) (tail)) (let $e (foo bar) (cons-atom $e (tail))))
!(== (cons-atom (cons-atom a (b)) (tail)) (let $e (cons-atom a (b)) (cons-atom $e (tail))))
!(== (cons-atom (let $x (foo a) $x) (tail)) (let $e (let $x (foo a) $x) (cons-atom $e (tail))))
!(== (cons-atom (case (foo a) (((done $x) ($x b)))) (tail)) (let $e (case (foo a) (((done $x) ($x b)))) (cons-atom $e (tail))))
!(== (cons-atom (chain (foo a) $x $x) (tail)) (let $e (chain (foo a) $x $x) (cons-atom $e (tail))))
!(== (cons-atom (((Field (+ 1 2))) ((Field (+ 2 3)))) (tail)) (let $e (((Field (+ 1 2))) ((Field (+ 2 3)))) (cons-atom $e (tail))))

; 6. cons-atom tail across 6 argument forms
!(== (cons-atom head (foo bar)) (let $e (foo bar) (cons-atom head $e)))
!(== (cons-atom head (cons-atom a (b))) (let $e (cons-atom a (b)) (cons-atom head $e)))
!(== (cons-atom head (let $x (foo a) $x)) (let $e (let $x (foo a) $x) (cons-atom head $e)))
!(== (cons-atom head (case (foo a) (((done $x) ($x b))))) (let $e (case (foo a) (((done $x) ($x b)))) (cons-atom head $e)))
!(== (cons-atom head (chain (foo a) $x $x)) (let $e (chain (foo a) $x $x) (cons-atom head $e)))
!(== (cons-atom head (((Field (+ 1 2))) ((Field (+ 2 3))))) (let $e (((Field (+ 1 2))) ((Field (+ 2 3)))) (cons-atom head $e)))

; 7. union-atom across 6 argument forms
!(== (union-atom (foo bar) (r)) (let $e (foo bar) (union-atom $e (r))))
!(== (union-atom (cons-atom a (b)) (r)) (let $e (cons-atom a (b)) (union-atom $e (r))))
!(== (union-atom (let $x (foo a) $x) (r)) (let $e (let $x (foo a) $x) (union-atom $e (r))))
!(== (union-atom (case (foo a) (((done $x) ($x b)))) (r)) (let $e (case (foo a) (((done $x) ($x b)))) (union-atom $e (r))))
!(== (union-atom (chain (foo a) $x $x) (r)) (let $e (chain (foo a) $x $x) (union-atom $e (r))))
!(== (union-atom (((Field (+ 1 2))) ((Field (+ 2 3)))) (r)) (let $e (((Field (+ 1 2))) ((Field (+ 2 3)))) (union-atom $e (r))))

; Effectful and nondeterministic destructor rows
(= (loud-pair $x) (let $_ (println! (effect $x)) (done $x)))
!(car-atom (loud-pair 1))
!(cdr-atom (loud-pair 2))
!(decons-atom (loud-pair 3))
!(== (car-atom (let $u (println! (effect 4)) (a b))) (let $e (let $u (println! (effect 5)) (a b)) (car-atom $e)))
!(== (collapse (car-atom (superpose ((a b) (c d))))) (collapse (let $e (superpose ((a b) (c d))) (car-atom $e))))

; Route congruence across inline strict demand, let, wrapper equations, and explicit eval:
; data built by syntax constructors or read from storage/state/parse is never re-executed
; as code when demanded inline, while authored templates and computation-choosing forms run.
(= (wrap-cons) (cons-atom + (1 2)))
(= (wrap-union) (union-atom (cons-atom + ()) (1 2)))
!(+ (cons-atom + (1 2)) 1)
!(+ (wrap-cons) 1)
!(+ (eval (cons-atom + (1 2))) 1)
!(+ (union-atom (cons-atom + ()) (1 2)) 1)
!(+ (wrap-union) 1)
!(+ (eval (union-atom (cons-atom + ()) (1 2))) 1)

!(bind! &route-space (new-space))
!(add-atom &route-space (+ 1 2))
(= (wrap-atoms) (get-atoms &route-space))
(= (wrap-match) (match &route-space $x $x))
(= (wrap-match-pair) (match &route-space (+ $a $b) ($a $b)))
!(+ (get-atoms &route-space) 1)
!(let $x (get-atoms &route-space) (+ $x 1))
!(+ (wrap-atoms) 1)
!(let $x (get-atoms &route-space) (+ (eval $x) 1))
!(+ (eval (get-atoms &route-space)) 1)
!(+ (match &route-space $x $x) 1)
!(let $x (match &route-space $x $x) (+ $x 1))
!(+ (wrap-match) 1)
!(+ (eval (match &route-space $x $x)) 1)
!(let ($a $b) (wrap-match-pair) ($a $b))
; Negative control: an authored match template is code and runs
!(+ (match &route-space (+ $a $b) (+ $a $b)) 1)
!(let $x (match &route-space (+ $a $b) (+ $a $b)) (+ $x 1))

(= (wrap-parse) (parse "(+ 1 2)"))
!(+ (parse "(+ 1 2)") 1)
!(let $x (parse "(+ 1 2)") (+ $x 1))
!(+ (wrap-parse) 1)
!(let $x (parse "(+ 1 2)") (+ (eval $x) 1))
!(eval (parse "(+ 1 2)"))
!(+ (eval (parse "(+ 1 2)")) 1)

!(bind! &route-state (new-state (cons-atom + (1 2))))
!(bind! &route-state-num (new-state (+ 1 2)))
(= (wrap-state) (get-state &route-state))
!(+ (get-state &route-state) 1)
!(let $x (get-state &route-state) (+ $x 1))
!(+ (wrap-state) 1)
!(let $x (get-state &route-state) (+ (eval $x) 1))
!(+ (eval (get-state &route-state)) 1)
!(get-state &route-state-num)
!(let $_ (change-state! &route-state (cons-atom + (2 3))) (+ (get-state &route-state) 1))
!(let $_ (change-state! &route-state (cons-atom + (2 3))) (let $x (get-state &route-state) (+ $x 1)))

; superpose chooses among its alternatives as computations, while preserving completed data
!(+ (superpose ((+ 1 2))) 1)
!(let $x (superpose ((+ 1 2))) (+ $x 1))
!(+ (superpose ((cons-atom + (1 2)))) 1)
!(let $x (superpose ((cons-atom + (1 2)))) (+ $x 1))
