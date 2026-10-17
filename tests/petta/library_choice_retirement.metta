; Library recursions written with exclusive clauses have one answer each and
; leave no alternative behind, so a deterministic loop over them keeps a
; constant choice depth instead of retaining one record per iteration.
!(import! &self (library path))
!(import! &self (library list))
!(println! (collapse (path:pop-stack () True)))
!(println! (collapse (path:pop-stack () False)))
!(println! (collapse (path:dirname "/a/b/c")))
!(println! (collapse (path:join "/a" "b/../c")))
!(println! (collapse (list:reverse-acc (a b c) ())))
(= (loop $n)
   (if (== $n 0) done
     (let $_ (and True (case (path:dirname "/a/b/c") (("/a/b" True) ($other False))))
       (loop (- $n 1)))))
!(println! (loop 200))
