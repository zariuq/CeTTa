; A conjunction, in an equation's body or at the top, whose legs as written
; leave one unjoined to the legs before it is read in a joined order: next
; comes a leg whose variables the legs before it bind (it only tests), else
; the next leg as written if it shares a variable with them, else the one
; sharing one that leaves the fewest unbound.  The answers are the written
; order's bag, and every leg reads the space as it stood at the call
; ([DIFF-20]).  Each bag is sorted or counted, as search order is not
; specified.

(parent p0 p1) (parent p0 p2) (parent p1 p3) (parent p1 p4) (parent p2 p5)
(parent p2 p6) (parent p3 p7) (parent p3 p8) (parent p4 p9) (parent p4 p10)
(parent p5 p11) (parent p5 p12) (parent p6 p13) (parent p6 p14)
(Src a) (Src b) (Tag t1) (Tag t2) (Tag t3)
(Link a b) (Link b c)

(= (cousin $a $b)
   (match &self (, (parent $x $a) (parent $y $b) (parent $g $x) (parent $g $y)) ($a $b)))
(= (cousin-joined $a $b)
   (match &self (, (parent $x $a) (parent $g $x) (parent $g $y) (parent $y $b)) ($a $b)))
(= (with-root $a $c)
   (match &self (, (parent $a $b) (parent $b $c) (parent p0 p1)) ($a $c)))
(= (tagged)
   (match &self (, (Src $a) (Tag $t) (Link $a $b))
     (let $u (add-atom &self (Link $b $b)) ($a $t $b))))

!(size-atom (collapse (cousin $a $b)))
!(== (msort (collapse (cousin $a $b))) (msort (collapse (cousin-joined $a $b))))
!(msort (collapse (cousin p7 $b)))
!(msort (collapse (with-root $a $c)))
!(msort (collapse (tagged)))
!(msort (collapse (match &self (Link $x $y) ($x $y))))
!(size-atom (collapse (match &self (, (parent $x $a) (parent $y $b) (parent $g $x) (parent $g $y)) ($a $b))))
!(msort (collapse (match &self (, (Src $a) (Tag $t) (Link $a $b)) (let $u (add-atom &self (Link $b $a)) ($a $t $b)))))
!(msort (collapse (match &self (Link $x $y) ($x $y))))
