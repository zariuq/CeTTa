; Runs the forest learner on the task loaded before it: each example's
; supports, one per line, after (example I).
!(import! &self provenance)
(= (task:print-supports $ss)
   (if (== $ss ()) ()
       (let ($h $t) (decons-atom $ss) (let $_ (println! $h) (task:print-supports $t)))))
(= (task:print $rs $i)
   (if (== $rs ()) ()
       (let* ((($r $t) (decons-atom $rs)) (($e $ss) $r)
              ($_ (println! (example $i))) ($_2 (task:print-supports $ss)))
         (task:print $t (+ $i 1)))))
!(let $rs (forest:generalise-all (task-examples) (task-bound)) (task:print $rs 1))
