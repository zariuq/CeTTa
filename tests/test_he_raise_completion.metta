; Errors in closures with effects that run on the compiled equation machine.
; HE completes every evaluation: an error replaces the evaluations that read
; it, a collection drops it beside a success, and the call's completion does
; too.  A run with effects cannot be evaluated again, so the machine raises
; each error to the innermost collection, or answers it for the call.

(= (run $s $ts)
   (match &self (, (state $s $ts (IC $i)) (program $s $i $ins))
      (perform $s $ins $ts $i)))
(= (perform $s H $ts $i) $ts)
(= (perform $s (BAD) $ts $i) (+ $ts "S"))
(= (perform $s (INC $r) $ts $i) (advance $s $ts (+ $i 1) $r 1))
(= (perform $s (JZ $r $j) $ts $i)
   (match &self (state $s $ts (REG $r $v))
      (if (== $v 0) (advance $s $ts $j $r 0) (advance $s $ts (+ $i 1) $r 0))))
(= (advance $s $ts $ni $r $delta)
   (let $n (+ $ts 1)
        (let $copied (collapse (copy-register $s $ts $n $r $delta))
             (let $set (add-atom &self (state $s $n (IC $ni)))
                  (run $s $n)))))
(= (copy-register $s $ts $n $r $delta)
   (match &self (state $s $ts (REG $k $v))
      (let $nv (if (== $k $r) (+ $v $delta) $v)
           (add-atom &self (state $s $n (REG $k $nv))))))

; A register that cannot be incremented: its copy's error stays in the
; collection beside the other copies, and the run goes on without it.
(program held 0 (INC 0))
(program held 1 (INC 1))
(program held 2 H)
(state held 0 (REG 0 "S"))
(state held 0 (REG 1 0))
(state held 0 (IC 0))
!(run held 0)
!(collapse (match &self (state held $t (REG $k $v)) ($t $k $v)))

; Only errors in the collection: the let whose value it is goes on.
(program alone 0 (INC 0))
(program alone 1 H)
(state alone 0 (REG 0 "S"))
(state alone 0 (IC 0))
!(run alone 0)
!(collapse (match &self (state alone $t (IC $i)) ($t $i)))

; An instruction that faults beside one that halts: the run's completion
; drops the error.
(program beside 0 (INC 0))
(program beside 1 (BAD))
(program beside 1 (INC 0))
(program beside 2 H)
(state beside 0 (REG 0 0))
(state beside 0 (IC 0))
!(run beside 0)

; A first leg with two rows: the conjunction's rows complete before any
; template changes the space.
(program legs 0 (INC 0))
(program legs 1 H)
(program legs 2 (INC 1))
(program legs 3 H)
(state legs 0 (REG 0 0))
(state legs 0 (REG 1 0))
(state legs 0 (IC 0))
(state legs 0 (IC 2))
!(run legs 0)
!(match &self (state legs $t (IC $i)) ($t $i))

; A condition's error is the if's value, dropped beside the other answers.
(kk 2)
(kk "S")
(kk 0)
(= (cond-eff $x) (if (> $x 1) (let $u (add-atom &self (big $x)) big) small))
(= (conditions) (collapse (match &self (kk $x) (cond-eff $x))))
!(conditions)
!(collapse (match &self (big $y) $y))

; A call whose equations give an error and a success after an effect.
(= (alt $x) (+ $x "S"))
(= (alt $x) (let $u (add-atom &self (alt-ran $x)) 5))
!(alt 1)

; Only errors in a collection whose value goes unread.
(k 1)
(k 2)
(= (bad $x) (+ $x "S"))
(= (unread) (let $c (collapse (match &self (k $x) (bad $x))) (add-atom &self (after unread))))
!(unread)
!(collapse (match &self (after $y) $y))

; The run's only answer is an error: it is the result.
(program fails 0 (INC 0))
(program fails 1 (BAD))
(state fails 0 (REG 0 0))
(state fails 0 (IC 0))
!(run fails 0)
