; An error raised inside a call dispatched at run time fails that path alone;
; every other alternative of the call remains, in any exploration order.
; Argument errors are outside the dispatch and propagate, and an Error value
; is an answer like any other.

(= (boom) (/ 1 0))

(= (alt-a) (/ 1 0))
(= (alt-a) 1)
(= (alt-a) 2)

(= (alt-b) 1)
(= (alt-b) (/ 1 0))
(= (alt-b) 2)

(= (alt-c) 1)
(= (alt-c) 2)
(= (alt-c) (/ 1 0))

(= (id2 $x) $x)
(= (err-data) (Error a b))
(= (inner-catch) (catch (/ 1 0)))
(= (invoke $f) ($f))

; The raising path fails; the other alternatives remain, whatever the order.
!(let $f alt-a (msort (collapse ($f))))
!(let $f alt-b (msort (collapse ($f))))
!(let $f alt-c (msort (collapse ($f))))

; A body that only raises has no answer.
!(let $f boom (collapse ($f)))

; An error in an argument is outside the dispatch.
!(catch (let $f id2 ($f (/ 1 0))))

; An Error value is data, and a catch inside the body keeps its own scope.
!(let $f err-data (collapse ($f)))
!(let $f inner-catch (collapse ($f)))

; reduce dispatches at run time, written in place or held by a variable.
!(collapse (reduce (boom)))
!(let $e (quote (boom)) (collapse (reduce $e)))

; A list native's element call is a dispatch: a raising condition fails its
; element's test.
!(filter-atom (1 0 2) (|-> ($x) (> (/ 6 $x) 2)))

; A call specialized once its callee is known keeps the handler it had.
!(catch (collapse (let $f (superpose (boom)) (invoke $f))))
!(catch (collapse (invoke boom)))

; A direct call is not a dispatch: its error reaches the catch.
!(catch (collapse (boom)))

; A callable in an argument pattern runs inside the dispatch, before the
; RHS. Specialization must keep that handler, including its dependencies.
(= (pattern-boom (boom)) accepted)
(= (invoke-pattern $f $x) ($f $x))
!(catch (collapse (let $f (superpose (pattern-boom)) ($f $x))))
!(catch (collapse (invoke-pattern pattern-boom $x)))
!(catch (collapse (pattern-boom $x)))

(= (pattern-step $x) $x)
(= (pattern-call (pattern-step $x)) $x)
!(collapse (invoke-pattern pattern-call 7))
!(add-atom &self (= (pattern-step $x) (/ 1 0)))
!(msort (collapse (invoke-pattern pattern-call 7)))

; Static calls over computed values retain their handler boundary when the
; tier hands off; the same callable dispatched dynamically still recovers.
(= (divide-computed $x) (/ 10 (- $x 2)))
(= (sort-computed $x) (msort (+ $x 1)))
(= (dynamic-forward $f $x) (+ 1 ($f $x)))
(= (divide-or-value $x) (divide-computed $x))
(= (divide-or-value $x) 7)
!(catch (collapse (divide-computed 2)))
!(collapse (let $f (superpose (divide-computed)) ($f 2)))
!(catch (collapse (divide-computed 4)))
!(catch (sort-computed 2))
!(collapse (let $f (superpose (sort-computed)) ($f 2)))
!(catch (collapse (dynamic-forward (superpose (divide-or-value)) 2)))
!(catch (collapse (dynamic-forward (superpose (divide-or-value)) 1)))

; An Atom-typed position keeps syntax even during specialization. Mixed
; overloads retain both interpretations through the ordinary call route.
(: syntax-keep (-> Atom %Undefined%))
(= (syntax-keep $x) $x)
(: evaluated-keep (-> %Undefined% %Undefined%))
(= (evaluated-keep $x) $x)
(: mixed-keep (-> Atom %Undefined%))
(: mixed-keep (-> %Undefined% %Undefined%))
(= (mixed-keep $x) $x)
(= (increment $x) (+ $x 1))
(= (fail-in-body $x) (/ $x 0))
(= (hold-syntax $f $x) (syntax-keep ($f $x)))
(= (run-value $f $x) (evaluated-keep ($f $x)))
(= (mixed-use $f $x) (mixed-keep ($f $x)))
!(hold-syntax increment 2)
!(hold-syntax fail-in-body 2)
!(run-value increment 2)
!(catch (collapse (run-value fail-in-body 2)))
!(catch (collapse (mixed-use increment 2)))
!(catch (collapse (mixed-use fail-in-body 2)))

(: map-flat3 (-> Atom %Undefined%))
(= (map-flat3 ($f ())) ())
(= (map-flat3 ($f (cons $x $xs))) (cons ($f $x) (map-flat3 ($f $xs))))
(= (p1 $x) (+ 1 $x))
!(map-flat3 (p1 (1 2)))
