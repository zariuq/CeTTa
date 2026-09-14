; Exact-key match-decision reuse across synchronous nested evaluation.
; The relations exercise different selection laws while the outer loop makes
; repeated requests from freshly created collapse machines.

; Repeated-variable refutation after structural indexing.
(= (repo-nonlinear 0 $x $x) zero)
(= (repo-nonlinear 1 $x $x) one)
(= (repo-nonlinear 2 $x $x) two)
(= (repo-nonlinear 3 $x $x) three)

; Literal-head selection.
(= (repo-literal red) red-result)
(= (repo-literal blue) blue-result)
(= (repo-literal green) green-result)

; Equal authored occurrences must remain two occurrences.
(= (repo-duplicate token) same)
(= (repo-duplicate token) same)

; Deep structural selection and binding.
(= (repo-deep (node red $x)) (red-value $x))
(= (repo-deep (node blue $x)) (blue-value $x))
(= (repo-deep (leaf $x)) (leaf-value $x))

; Simultaneously applicable alternatives retain authored order.
(= (repo-ordered token) first)
(= (repo-ordered token) second)

; Overlapping structural patterns preserve both lawful survivors.
(= (repo-overlap (pair left $x)) (left-value $x))
(= (repo-overlap (pair $x right)) (right-value $x))

(= (repo-exercise $n)
   (if (== $n 0)
       (let $nonlinear (collapse (repo-nonlinear $tag left right))
         (let $literal (collapse (repo-literal blue))
           (let $duplicate (collapse (repo-duplicate token))
             (let $deep (collapse (repo-deep (node blue payload)))
               (let $ordered (collapse (repo-ordered token))
                 (let $overlap (collapse (repo-overlap (pair left right)))
                   (result $nonlinear $literal $duplicate $deep $ordered
                     $overlap)))))))
       (let $_ (collapse (repo-nonlinear $tag left right))
         (let $_ (collapse (repo-literal blue))
           (let $_ (collapse (repo-duplicate token))
             (let $_ (collapse (repo-deep (node blue payload)))
               (let $_ (collapse (repo-ordered token))
                 (let $_ (collapse (repo-overlap (pair left right)))
                   (repo-exercise (- $n 1))))))))))

!(repo-exercise 20)
