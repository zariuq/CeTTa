!(import! &self pat)
(parent alice bob)
(parent bob carol)
(parent diane $unknown)
(parent erin frank)
!(collapse (pat:match% &self
    (, (parent $x $y) (parent $y $z)) (grandparent $x $z)))
!(pat:apply
   (pat:solve match% (((p $x) (p a))
                      ((q $x $y) (q a b))
                      ((r $y) (r b))))
   (proof $x $y))
!(collapse (pat:solve match% (((p $x) (p $subject)) ((q $x) (q a)))))
; Separate attempts do not inherit protection: this intentionally differs.
!(pat:let% (p $x) (p $subject)
   (pat:let% (q $x) (q a) (separate $subject)))
; An interactive solver can inspect an input batch whose variable names it
; did not know beforehand; a fixed application template cannot list that map.
!(pat:view (pat:solve match% (((p $x) (p a)) ((q $x $y) (q a b)))))
