; Recorded output for inspection; the learner does not read this file.
(= (ic-learned:b $p0 $p1 $p2 $p3 $p4 $p5)
   (if (< $p0 $p1)
       $p2
       (+ (* $p3 $p4)
          (/safe (* (- $p5 $p3) (- $p2 (* $p1 $p4)))
                 (- $p5 $p1)))))
(= (ic-learned:a $p0 $p1 $p2)
   (/ (* (+ 1.0 (/ $p0 $p1)) $p2) (+ 1.0 $p2)))
