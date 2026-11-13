!(import! &self (library lib_transform))
!(import! &self (library lib_step_rules))

; (check NAME GOT WANT): GOT is evaluated and compared with WANT up to the names
; of variables.
(: check (-> Atom Atom Atom %Undefined%))
(= (check $name $got $want)
   (let $value (collapse (eval $got))
     (if (=alpha $value $want) (holds $name) (FAILS $name $value $want))))

; A state with two facts, a fact holding an unknown, and a stored schema.
(= (start)
   (let (step:admitted $state $_firing)
        (step:fire (step:empty)
          (step:firing () () ((p 1) (p 2) (q $u) (step:schema (r (step:local 0) (step:local 0)))) () () () ()))
     $state))
(= (verdict (step:admitted $state $firing)) admitted)
(= (verdict (step:refused $reason)) (refused $reason))
(= (store (step:admitted (step:state $next $names $apart $present) $firing)) (step:labels $present))
(= (store (step:refused $reason)) (refused $reason))
(= (apart (step:admitted (step:state $next $names $apart $present) $firing)) $apart)

; takes, reads, makes
!(check take-read-make
   (store (step:fire (start) (step:firing ((step:use (p $a) 1)) ((step:use (p $b) 2)) ((done $a $b)) () () () ())))
   (((p 2) (q $u) (step:schema (r (step:local 0) (step:local 0))) (done 1 2))))
!(check take-does-not-unify (verdict (step:fire (start) (step:firing ((step:use (q $a) 1)) () () () () () ())))
   ((refused unsolvable)))
!(check occurrence-missing (verdict (step:fire (start) (step:firing ((step:use $a 9)) () () () () () ())))
   ((refused fits)))
!(check occurrence-taken-twice
   (verdict (step:fire (start) (step:firing ((step:use $a 1) (step:use $b 1)) () () () () () ())))
   ((refused shapes)))
!(check occurrence-taken-and-read
   (verdict (step:fire (start) (step:firing ((step:use $a 1)) ((step:use $b 1)) () () () () ())))
   ((refused fits)))
!(check read-twice-is-one-occurrence
   (verdict (step:fire (start) (step:firing () ((step:use (p $a) 1) (step:use (p $b) 1)) ((same $a $b)) () () () ())))
   (admitted))
!(check a-refused-firing-changes-nothing
   (step:admits (start) (step:firing ((step:use (q $a) 1)) () ((never)) () () () ()))
   ((step:refused unsolvable)))

; knowledge: a tell binds the unknown the store holds
!(check tell-binds-the-store
   (store (step:fire (start) (step:firing () ((step:use (q $x) 3)) () ((step:equal $x 7)) () () ())))
   (((p 1) (p 2) (q 7) (step:schema (r (step:local 0) (step:local 0))))))
!(check tell-without-a-solution
   (verdict (step:fire (start) (step:firing () ((step:use (p $x) 1)) () ((step:equal $x 2)) () () ())))
   ((refused unsolvable)))

; guards hold at every solution
!(check equal-not-entailed
   (verdict (step:fire (start) (step:firing () ((step:use (q $x) 3)) () () () ((step:equal $x 7)) ())))
   ((refused (step:guard (step:equal $x 7)))))
!(check equal-entailed-once-told
   (verdict (step:fire (start) (step:firing () ((step:use (q $x) 3)) () ((step:equal $x 7)) () ((step:equal $x 7)) ())))
   (admitted))
!(check absent (verdict (step:fire (start) (step:firing () () ((ok)) () () ((step:absent (p 3))) ()))) (admitted))
!(check absent-fails-when-present
   (verdict (step:fire (start) (step:firing () () ((ok)) () () ((step:absent (p 1))) ())))
   ((refused (step:guard (step:absent (p 1))))))
!(check absent-not-entailed-beside-an-unknown
   (verdict (step:fire (start) (step:firing () () ((ok)) () () ((step:absent (q 5))) ())))
   ((refused (step:guard (step:absent (q 5))))))
!(check count-copies-of-the-instance
   (verdict (step:fire (start) (step:firing () ((step:use (p $n) 1)) () () () ((step:count (p $n) 1)) ())))
   (admitted))
!(check region-query (verdict (step:fire (start) (step:firing () () () () () ((step:region (p $any) 2)) ())))
   (admitted))
!(check region-query-wrong-number
   (verdict (step:fire (start) (step:firing () () () () () ((step:region (p $any) 3)) ())))
   ((refused (step:guard (step:region (p $any) 3)))))
!(check region-not-entailed-beside-an-unknown
   (verdict (step:fire (start) (step:firing () () () () () ((step:region (q 5) 0)) ())))
   ((refused (step:guard (step:region (q 5) 0)))))
!(check permission-by-expression
   (verdict (step:fire (start) (quote (step:firing () ((step:use (p $n) 2)) () () () ((step:when (> $n 1))) ()))))
   (admitted))
!(check permission-refused
   (verdict (step:fire (start) (quote (step:firing () ((step:use (p $n) 1)) () () () ((step:when (> $n 1))) ()))))
   ((refused (step:guard (step:when (> 1 1))))))

; a stored schema opens afresh at every port
!(check schema-opens-afresh
   (store (step:fire (start) (step:firing () ((step:use (r a $x) 4) (step:use (r b $y) 4)) ((got $x $y)) () () () ())))
   (((p 1) (p 2) (q $u) (step:schema (r (step:local 0) (step:local 0))) (got a b))))
!(check schema-keeps-its-own-equation
   (verdict (step:fire (start) (step:firing () ((step:use (r a b) 4)) () () () () ())))
   ((refused unsolvable)))

; names: minted by the firing, never written before
!(check fresh-names
   (store (step:fire (start) (step:firing () () ((chan $n) (chan $m) (pair $n $m)) () () () ($n $m))))
   (((p 1) (p 2) (q $u) (step:schema (r (step:local 0) (step:local 0)))
     (chan (step:name 0)) (chan (step:name 1)) (pair (step:name 0) (step:name 1)))))
!(check a-name-is-not-a-code
   (verdict (let (step:admitted $s $f) (step:fire (start) (step:firing () () ((chan $n)) () () () ($n)))
     (step:fire $s (step:firing () ((step:use (chan 0) 5)) () () () () ()))))
   ((refused unsolvable)))

; disequations: told, kept while open, and a later equation that violates one is refused
!(check apart-is-kept
   (apart (step:fire (start) (step:firing () ((step:use (q $x) 3)) () () ((step:apart $x 7)) () ())))
   (((step:apart $x 7))))
!(check apart-decided-is-dropped
   (apart (step:fire (start) (step:firing () ((step:use (p $x) 1)) () () ((step:apart $x 7)) () ())))
   (()))
!(check apart-violated-at-once
   (verdict (step:fire (start) (step:firing () ((step:use (p $x) 1)) () () ((step:apart $x 1)) () ())))
   ((refused unsolvable)))
!(check apart-violated-later
   (verdict (let (step:admitted $s $f) (step:fire (start) (step:firing () ((step:use (q $x) 3)) () () ((step:apart $x 7)) () ()))
     (step:fire $s (step:firing () ((step:use (q $y) 3)) () ((step:equal $y 7)) () () ()))))
   ((refused unsolvable)))
!(check apart-respected-later
   (verdict (let (step:admitted $s $f) (step:fire (start) (step:firing () ((step:use (q $x) 3)) () () ((step:apart $x 7)) () ()))
     (step:fire $s (step:firing () ((step:use (q $y) 3)) () ((step:equal $y 8)) () () ()))))
   (admitted))

; runs, rules that choose their occurrences, and halting
!(check run-stops-at-the-first-refusal
   (let (step:ran $count $state)
        (step:run (start) ((step:firing ((step:use (p $a) 1)) () () () () () ())
                           (step:firing ((step:use (p $b) 1)) () () () () () ())
                           (step:firing ((step:use (p $c) 2)) () () () () () ())))
     $count)
   (1))
!(check every-candidate
   (let (step:admitted (step:state $next $names $apart $present) $firing)
        (step:any (start) (step:rule ((p $n)) () ((seen $n)) () () () ()))
     (step:labels $present))
   (((p 2) (q $u) (step:schema (r (step:local 0) (step:local 0))) (seen 1))
    ((p 1) (q $v) (step:schema (r (step:local 0) (step:local 0))) (seen 2))))
!(check saturate-to-a-halted-state
   (let (step:halted $count (step:state $next $names $apart $present))
        (step:saturate (start) ((step:rule ((p $n)) () ((seen $n)) () () () ())))
     (halted $count (step:labels $present)))
   ((halted 2 ((q $u) (step:schema (r (step:local 0) (step:local 0))) (seen 1) (seen 2)))))

; a rule with no unknown and no name is one transform over a space
!(bind! &facts (new-space))
!(add-atom &facts (edge a b))
!(add-atom &facts (edge b c))
(= (closure-rule)
   (step:rule () ((edge $x $y) (edge $y $z)) ((edge $x $z)) () () ((step:absent (edge $x $z))) ()))
!(check rule-as-sources (step:sources (closure-rule))
   ((, (read (edge $x $y)) (read (edge $y $z)) (absent (edge $x $z)))))
!(check rule-fires-in-a-space (step:fire-in &facts (closure-rule)) (1))
!(check rule-fired-store (sort-atom (collapse (match &facts $fact $fact))) (((edge a b) (edge a c) (edge b c))))
!(check rule-as-a-step
   (let (step:admitted (step:state $next $names $apart $present) $firing)
        (step:any (let (step:admitted $s $f) (step:fire (step:empty) (step:firing () () ((edge a b) (edge b c)) () () () ())) $s)
                  (closure-rule))
     (step:labels $present))
   (((edge a b) (edge b c) (edge a c))))
