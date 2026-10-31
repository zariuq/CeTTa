; A match inside a match reads its space afresh for every row before it,
; after the effects of the rows already answered.  Where no effect meets
; such a read, the answers are those of reading the rows the query began
; with; where one does, later reads see it.
!(bind! &t (new-space set))
!(bind! &b (new-space))
; A circuit evaluated level by level: each level reads the values of the
; levels below it and writes its own, which no read of the level meets.
(gate g1 x0 x1) (gate g2 x1 x2) (gate g3 g1 g2) (gate g4 g3 x0)
(lvl 1 g1) (lvl 1 g2) (lvl 2 g3) (lvl 3 g4)
(both 0 0 0) (both 0 1 0) (both 1 0 0) (both 1 1 1)
(input x0 0 1) (input x0 1 1) (input x1 0 1) (input x1 1 0) (input x2 0 1) (input x2 1 1)
!(size-atom (collapse (match &self (input $p $k $v) (add-atom &t (val $p $k $v)))))
(= (level $l)
   (match &self (lvl $l $n) (match &self (gate $n $a $b)
     (match &t (val $a $k $x) (match &t (val $b $k $y)
       (match &self (both $x $y $z) (add-atom &t (val $n $k $z))))))))
!(size-atom (collapse (level 1)))
!(size-atom (collapse (level 2)))
!(size-atom (collapse (level 3)))
!(collapse (match &t (val g4 0 $v) $v))
!(collapse (match &t (val g4 1 $v) $v))
; Each row's addition is read by the rows after it, whatever their order:
; the first row's pattern on &t meets one fact, the second's two.
(k a 1) (k a 2) (pick a)
!(add-atom &t (e a 0))
!(add-atom &b (e a 0))
!(size-atom (collapse (match &self (pick $x) (match &self (k $x $y)
   (match &t (e $x $z) (add-atom &t (e $x $y)))))))
!(size-atom (collapse (match &t (e a $z) $z)))
!(size-atom (collapse (match &self (pick $x) (match &self (k $x $y)
   (match &b (e $x $z) (add-atom &b (e $x $y)))))))
!(size-atom (collapse (match &b (e a $z) $z)))
; The same variable in two columns: an addition holding two different
; atoms there meets no read of them, and one holding the same atom twice
; does.
!(add-atom &t (loop c c))
(pick2 c)
!(size-atom (collapse (match &self (pick2 $x) (match &t (loop $x $x)
   (match &t (loop $q $q) (add-atom &t (loop $x e)))))))
!(size-atom (collapse (match &self (pick2 $x) (match &t (loop $x $x)
   (match &t (loop $q $q) (add-atom &t (loop e e)))))))
!(sort-atom (collapse (match &t (loop $u $w) ($u $w))))
; The first pattern is read once, before any effect: copying every fact
; into itself under another head, and mirroring a relation into itself.
!(add-atom &t (m 1 2))
!(add-atom &t (m 2 3))
!(size-atom (collapse (match &t (m $x $y) (add-atom &t (m $y $x)))))
!(size-atom (collapse (match &t (m $x $y) t)))
; Removals of the facts each row reads, which a later row's read could
; meet.
!(add-atom &t (r 1 p)) !(add-atom &t (r 2 p)) !(add-atom &t (r 2 q))
(pick3 1) (pick3 2)
!(size-atom (collapse (match &self (pick3 $x) (match &t (r $x $y)
   (remove-atom &t (r $x $y))))))
!(size-atom (collapse (match &t (r $x $y) t)))
