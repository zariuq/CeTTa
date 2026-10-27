; Firing now, once later, or always.  A parked firing waits on the regions
; its last attempt read and is woken by any change there.
!(import! &self interaction)
!(bind! &q (new-space))
!(bind! &out (new-space))

; await: parked until it can fire, then once
!(await (transform &q (take (job $x)) (add-atom &out (got $x))))
!(collapse (get-atoms &out))
!(transform &q () (, (add-atom &q (job 1)) (add-atom &q (job 2))))
!(sort-atom (collapse (get-atoms &out)))
!(collapse (match &q (job $x) $x))

; whenever: a rule; each enabling fires it and the rule stays
!(whenever (transform &q (take (job $x)) (add-atom &out (served $x))))
!(transform &q () (, (add-atom &q (job 3)) (add-atom &q (job 4))))
!(sort-atom (collapse (get-atoms &out)))
!(collapse (match &q (job $x) $x))
; a write by any route wakes it at the next commit
!(add-atom &q (job 5))
!(transform &q () (add-atom &q tick))
!(sort-atom (collapse (match &out (served $x) $x)))

; else: now, otherwise the fallback
!(else (transform &q (take (absent-thing $x)) (found $x)) fallback)
!(else (transform &q (take tick) took-tick) fallback)

; choose now: one alternative commits
!(add-atom &q (b 7))
!(choose (transform &q (take (a $x)) (from-a $x))
         (transform &q (take (b $x)) (from-b $x)))
; await a choice: a reply or a cancellation, whichever comes first; the
; other arm never fires
!(bind! &reply (new-space))
!(bind! &cancel (new-space))
!(await (choose (transform &reply (take (answer $v)) (add-atom &out (replied $v)))
                (transform &cancel (take stop) (add-atom &out cancelled))))
!(transform &cancel () (add-atom &cancel stop))
!(transform &reply () (add-atom &reply (answer 42)))
!(collapse (match &out cancelled cancelled))
!(collapse (match &out (replied $v) $v))
!(collapse (match &reply (answer $v) $v))

; par: processes whose steps the agenda interleaves; a server on a fresh
; channel with private reply channels
!(bind! &bank (new-space))
!(add-atom &bank (balance 100))
(= (teller $req)
   (whenever (transform $req (, (take $req (withdraw $n $reply))
                                 (take &bank (balance $b))
                                 (let $c (- $b $n)) (let True (>= $c 0)))
       (, (add-atom &bank (balance $c)) (add-atom $reply (ok $n))))))
!(let* (($req (new-region channel)) ($r1 (new-region channel)) ($r2 (new-region channel)))
   (par (teller $req)
        (transform $req () (add-atom $req (withdraw 30 $r1)))
        (transform $req () (add-atom $req (withdraw 50 $r2)))
        (await (transform $r1 (take (ok $v)) (add-atom &out (r1 $v))))
        (await (transform $r2 (take (ok $v)) (add-atom &out (r2 $v))))))
!(sort-atom (collapse (match &out ($who $v) (if (== $who served) (empty) ($who $v)))))
!(collapse (match &bank (balance $b) $b))

; seq: one step of today's operations, then its continuation as a process
!(bind! &log (new-space))
!(par (seq (match &q (b $x) $x) $v (add-atom &log (saw-b $v)))
      (seq (add-atom &log start) $u (add-atom &log next)))
!(sort-atom (collapse (get-atoms &log)))
