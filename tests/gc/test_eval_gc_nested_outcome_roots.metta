; A strict binary call retains its first child OutcomeSet while evaluating the
; second child in a nested C evaluator frame.  Until those lexical frames have
; precise stack maps, nested moving collection must be excluded.
(= (gc-nested-fib $n)
   (if (< $n 2)
       $n
       (+ (gc-nested-fib (- $n 1))
          (gc-nested-fib (- $n 2)))))

!(assertEqual (gc-nested-fib 20) 6765)
