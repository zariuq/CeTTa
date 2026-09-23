; `wrap` resumes after each `letter`. The proof is ground. `$z` is open
; until the later edge match binds it. Duplicate `a` stays duplicated.
; `z` matches no edge. The following `wrap` equation still answers.

(edge a b)
(edge a d)
(edge b c)

(= (letter) a)
(= (letter) a)
(= (letter) b)
(= (letter) z)

(= (wrap) (let $x (letter) (proof $x)))
(= (wrap) (proof a))

!(let (proof $x) (wrap)
   (match &self (edge $x $z) (wit $x $z)))
