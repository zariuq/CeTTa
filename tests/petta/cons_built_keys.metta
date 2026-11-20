; Lookups keyed by lists that cons-atom builds inside a match over rows with
; variables: the key is complete once its last tail is, and is then the flat
; list it spells, whatever the depth it sits at in the pattern; a key whose
; tail stays unbound stands for every list it may become.  The answers keep
; the rows' order and their copies.
!(bind! &kb (new-space))
!(add-atom &kb (ctv (P a) ra (P a $t) 0.5))
!(add-atom &kb (ctv (P b) rb (P b $t) 0.25))
!(add-atom &kb (ctv (Q c) rc (Q c $t) 0.125))
!(add-atom &kb (obs (P a t1) 1))
!(add-atom &kb (obs (P a t2) 0))
!(add-atom &kb (obs (P b t1) 0))
!(add-atom &kb (obs (Q c t1) 1))
!(add-atom &kb (obs (Q c t1) 1))
!(add-atom &kb (obs (Q c t3) 1))
!(add-atom &kb (obs ((P a t1)) nested))
!(add-atom &kb (obs (P $any t1) open-row))
(= (with-period $s $t)
   (if (== (cdr-atom $s) ()) ($t) (cons-atom (car-atom $s) (with-period (cdr-atom $s) $t))))
(= (head-key $s) ((car-atom $s) (car-atom (cdr-atom $s))))
(= (rules-of $x) (let $k (head-key $x) (collapse (match &kb (ctv $k $n $x $p) $n))))
(= (obs-of $x) (collapse (match &kb (obs $x $v) $v)))
; A key built from each rule's head, used where it is built.
!(collapse (match &kb (ctv $k $n $c $p)
     (let $x (with-period $c t1) ($n (rules-of $x) (obs-of $x)))))
; The same keys one level deeper in the pattern.
!(collapse (match &kb (ctv $k $n $c $p)
     (let $x (with-period $c t1) ($n (obs-of ($x))))))
; A key whose last element is still a variable: its rows by unification.
!(collapse (match &kb (ctv $k $n $c $p)
     (let $x (with-period $c $w) ($n (obs-of $x)))))
; A key whose tail is unbound: every list it may become.
!(collapse (match &kb (ctv $k $n $c $p)
     (let $x (cons-atom (car-atom $c) $rest) ($n (obs-of $x)))))
; A period no row holds.
!(collapse (match &kb (ctv $k $n $c $p)
     (let $x (with-period $c t9) ($n (obs-of $x)))))
