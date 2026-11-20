; An activation's body reads every binding its head made.  Most queries first
; bind one of their own variables to a fresh copy, so the environment already
; holds a binding when the equation opens, and each box carries an open field.
;
; one-box, two-box and late-var take their list through (cons $w ()), which
; structural matching does not discharge, so each equation is opened by
; solving its head against the call.  Every call answers, as it does without
; the copy.
(= (pick (box $s $v)) $s)
(= (one-box (cons $w ())) (pick $w))
(= (two-box (cons $w ()) $k) (+ $k (pick $w)))
(= (late-var (cons $w ())) (let $u (pick $w) ($u $w)))
!(let* (($c (copy_term (pair $a))) ((pair $f) $c)) (One (one-box ((box 5 $q)))))
!(let* (($c (copy_term (pair $a))) ((pair $f) $c)) (Two (two-box ((box 5 $q)) 1)))
!(let* (($c (copy_term (pair $a))) ((pair $f) $c)) (Late (late-var ((box 5 $q)))))
!(One (one-box ((box 5 $q))))

; The compiled open-equation tier leaves to the search machine a body whose
; let binds a collapse, so on the default route as well these equations are
; opened by solving their heads.
(= (inner-empty $p) (let $c (empty) $p))
(= (pick-after (box $s $v)) (let $u (collapse (inner-empty $s)) $s))
(= (one-box-after (cons $w ())) (let $u (collapse (inner-empty $w)) (pick-after $w)))
!(let* (($c (copy_term (pair $a))) ((pair $f) $c)) (After (one-box-after ((box 5 $q)))))
!(let $u (collapse (inner-empty 1)) (let* (($c (copy_term (pair $a))) ((pair $f) $c)) (AfterInBody (one-box-after ((box 5 $q))))))

; A head that also binds a variable of its caller.
(= (bind-out $w 1) (pick $w))
!(let* (($c (copy_term (pair $a))) ((pair $f) $c)) (Out (let $r (bind-out (box 5 $q) $out) ($r $out))))
(= (bind-out-after $w 1) (let $u (collapse (inner-empty $w)) (pick-after $w)))
!(let* (($c (copy_term (pair $a))) ((pair $f) $c)) (OutAfter (let $r (bind-out-after (box 5 $q) $out) ($r $out))))

; A head that fails after its equation opened leaves the next equation to
; answer, and a call that no head matches has no answer.
(= (unwrap (cons (box $s $v) ())) $s)
(= (unwrap (cons (tag $s) ())) (tagged $s))
!(let* (($c (copy_term (pair $a))) ((pair $f) $c)) (Second (unwrap ((tag 7)))))
!(let* (($c (copy_term (pair $a))) ((pair $f) $c)) (NoHead (collapse (unwrap ((other 7))))))
!(let* (($c (copy_term (pair $a))) ((pair $f) $c)) (TwoCells (collapse (one-box ((box 5 $q) (box 6 $r))))))

; An equation without variables opens no frame and answers like any other,
; on the default route as well.
(= (h (cons a ())) c)
!(let* (($c (copy_term (pair $a))) ((pair $f) $c)) (NoVariables (h (a))))
!(NoVariables (h (a)))
!(NoVariablesMiss (collapse (h (b))))
(= (h-after (cons a ())) (collapse (inner-empty a)))
!(let* (($c (copy_term (pair $a))) ((pair $f) $c)) (NoVariablesAfter (h-after (a))))
