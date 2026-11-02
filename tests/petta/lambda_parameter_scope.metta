; A lambda's parameters are its own: the reference compiles a lambda to a
; clause of its own, so a parameter named like a variable of the enclosing
; form is another variable, while the lambda's other variables are the
; form's.  A lambda that is data (quoted, stored by add-atom) keeps its
; variables.
(= (shadow1 $a $l) (maplist (|-> ($a) $a) $l))
(= (shadow2 $a $l) (maplist (|-> (($a $b)) ($a $b)) $l))
(= (shadow4 $a) ((|-> ($a) $a) 5))
(= (outer-after $x) (let $f (|-> ($x) (+ $x 1)) ($f 10)))
(= (free-shared $l) (let $k 3 (maplist (|-> ($x) (+ $x $k)) $l)))
!(shadow1 outer (1 2))
!(shadow2 outer ((1 2) (3 4)))
!(shadow4 outer)
!(outer-after 1)
!(let $a outer (maplist (|-> ($a) $a) (1 2)))
!(let $a outer ((|-> (($a $b)) $a) (1 2)))
!(let $a outer (eval (quote ((|-> ($a) $a) 5))))
!(let $x 1 (let $f (|-> ($x) $x) (($f 2) $x)))
!(let $a outer (let $_ (add-atom &self (holder (|-> ($a) $a))) (collapse (match &self (holder $h) $h))))

; Simultaneous opening preserves parameter order, held values and each
; declared nested scope. Wide captures exercise the ordinary worker.
(= (span-free-branch $x) (let $f (|-> ($a) (if (> $a 0) $a (Keep $span0 $span1 $span2 $span3 $span4 $span5 $span6 $span7 $span8 $span9 $span10 $span11 $span12 $span13 $span14 $span15 $span16 $span17 $span18 $span19 $span20 $span21 $span22 $span23 $span24 $span25 $span26 $span27 $span28 $span29 $span30 $span31 $span32 $span33 $span34 $span35 $span36 $span37 $span38 $span39 $span40 $span41 $span42 $span43 $span44 $span45 $span46 $span47 $span48 $span49 $span50 $span51 $span52 $span53 $span54 $span55 $span56 $span57 $span58 $span59 $span60 $span61 $span62 $span63 $span64))) ($f $x)))
!((|-> ($a $b $c) (Triple $a $b $c)) 11 22 33)
!(let $f (|-> ($a $b) (Pair $a $b)) (let $p ($f 11) ($p 22)))
!((|-> ($a) (|-> ($b) (Pair $a $b))) 11 22)
!((|-> ($a $b) $a) (quote (readln!)) 22)
!((|-> ($a $b) (quote (Pair $a $b))) 11 22)
!(let $s (new-space) (let $f (|-> ($a $b) (progn (add-atom $s (tick $a $b)) (+ $a $b))) (let $r ($f 11 22) (Pair $r (size-atom (collapse (get-atoms $s)))))))
!(collapse ((|-> ($a $b) (Pair $a $b)) (superpose (11 22)) 33))
!((|-> () (|-> ($a $b) (Pair $a $b))) 11 22)
!(span-free-branch 7)
!(size-atom (span-free-branch 0))
