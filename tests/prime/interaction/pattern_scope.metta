; A transform's source patterns bind the names they hold, as a pattern of
; match binds its names: each is the transform's own, in force in its
; sources and its template.  A name of the scope around reaches a pattern
; only through a crossing set, T{$x}.  Locations and the expression of a
; (let P E) source are read where they stand.  The binding structure comes
; from declarations the library makes, so a document can declare its own
; forms the same way.
!(import! &self interaction)
!(bind! &t (new-space))
!(transform &t () (, (add-atom &t (holds a 1)) (add-atom &t (holds b 2))
                     (add-atom &t (likes a b))))

; an outer name does not refine a pattern: match and transform agree
!(let $f 1 (sort-atom (collapse (match &t (holds $j $f) ($j $f)))))
!(let $f 1 (sort-atom (collapse (transform &t (every (read (holds $j $f))) ($j $f)))))
; a crossing set shares it, for both
!(let $f 1 (sort-atom (collapse (match &t (holds $j $f) $j){$f})))
!(let $f 1 (sort-atom (collapse (transform &t (every (read (holds $j $f))) $j){$f})))
; await, whenever and choose read a transform through its crossing set
!(let $j b (await (transform &t (read (holds $j $n)) ($j $n)){$j}))
; an equation's parameter is a name of the scope around too
(= (holders $f) (sort-atom (collapse (transform &t (every (read (holds $j $f))) $j))))
(= (holder $f) (collapse (transform &t (every (read (holds $j $f))) $j){$f}))
!(holders 2)
!(holder 2)
; the template's name is the pattern's, not the one around
!(let $j zzz (sort-atom (collapse (transform &t (every (read (holds $j $n))) $j))))

; a name in two sources is one name: the sources join on it
!(collapse (transform &t (every (, (read (holds $x $n)) (read (likes $x $y)))) ($x $y $n)))
; a location is read where it stands: the scope around's name
!(let $s &t (sort-atom (collapse (transform &t (every (read $s (holds $j $n))) ($j $n)))))
; the expression of a let source reads the sources' names and the scope's
!(let $k 10 (sort-atom (collapse (transform &t (every (, (read (holds $j $n)) (let $m (+ $n $k)))) ($j $m)))))

; a receipt and seq bind as a transform does
!(let $y zz (for ((likes $x $y) <<- &t) ($x $y)))
!(let $v 0 (par (seq (superpose (1 2)) $v (add-atom &t (got $v)))))
!(sort-atom (collapse (match &t (got $g) $g)))

; code formed while the program runs reads the declarations too
!(sort-atom (collapse (eval (parse "(let $f 1 (transform &t (every (read (holds $j $f))) ($j $f)))"))))

; a form nobody declared is an ordinary call: its argument is a value, and
; the name around refines it
(= (rows-of $p) (match &t $p $p){$p})
!(let $f 1 (collapse (rows-of (holds $j $f))))
; a document declares its own binding form the same way
(scope:binder (pick value pattern body))
(= (pick $s $p $t) (collapse (match $s $p $t){$p $t}))
!(let $f 1 (sort-atom (pick &t (holds $j $f) ($j $f))))
!(let $f 1 (sort-atom (pick &t (holds $j $f) $j){$f}))
