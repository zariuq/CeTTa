; A match call reads its space once, when it runs; its rows' bodies run in
; the order of that read, each to completion; a match nested in a body reads
; when its branch reaches it; a write takes effect at once.  How the write is
; spelled, and how the matches are nested, change nothing.  Each case reads
; a space of its own.

; Three nested matches whose body writes an edge (d w) into the space they
; read.  With these facts the later branches read the earlier writes:
; 16 answers and 16 writes.
!(bind! &s1 (new-space))
!(add-atom &s1 (edge a b))
!(add-atom &s1 (edge b c))
!(add-atom &s1 (edge c d))
!(add-atom &s1 (edge c e))
!(let $r (collapse (match &s1 (edge $x $y) (match &s1 (edge $y $z)
     (match &s1 (edge $z $w) (add-atom &s1 (edge d $w)))))) (size-atom $r))
!(let $r (collapse (match &s1 (edge d $v) $v)) (size-atom $r))

; The write wrapped in a function.
!(bind! &s2 (new-space))
!(add-atom &s2 (edge a b))
!(add-atom &s2 (edge b c))
!(add-atom &s2 (edge c d))
!(add-atom &s2 (edge c e))
(= (note2 $x) (add-atom &s2 (edge d $x)))
!(let $r (collapse (match &s2 (edge $x $y) (match &s2 (edge $y $z)
     (match &s2 (edge $z $w) (note2 $w))))) (size-atom $r))
!(let $r (collapse (match &s2 (edge d $v) $v)) (size-atom $r))

; The space named through a function.
!(bind! &s3 (new-space))
!(add-atom &s3 (edge a b))
!(add-atom &s3 (edge b c))
!(add-atom &s3 (edge c d))
!(add-atom &s3 (edge c e))
(= (space3) &s3)
!(let $r (collapse (match &s3 (edge $x $y) (match &s3 (edge $y $z)
     (match &s3 (edge $z $w) (add-atom (space3) (edge d $w)))))) (size-atom $r))
!(let $r (collapse (match &s3 (edge d $v) $v)) (size-atom $r))

; A let between the matches, and a body that answers after writing.
!(bind! &s4 (new-space))
!(add-atom &s4 (edge a b))
!(add-atom &s4 (edge b c))
!(add-atom &s4 (edge c d))
!(add-atom &s4 (edge c e))
(= (note4 $x) (add-atom &s4 (edge d $x)))
!(let $r (collapse (match &s4 (edge $x $y) (let $u () (match &s4 (edge $y $z)
     (let $v () (match &s4 (edge $z $w)
       (let $t (note4 $w) (seen $x $y $z $w)))))))) (size-atom $r))
!(let $r (collapse (match &s4 (edge d $v) $v)) (size-atom $r))

; The same facts in the other order: 2 answers and 2 writes, written
; directly or through a function.
!(bind! &s5 (new-space))
!(add-atom &s5 (edge c e))
!(add-atom &s5 (edge c d))
!(add-atom &s5 (edge b c))
!(add-atom &s5 (edge a b))
!(let $r (collapse (match &s5 (edge $x $y) (match &s5 (edge $y $z)
     (match &s5 (edge $z $w) (add-atom &s5 (edge d $w)))))) (size-atom $r))
!(let $r (collapse (match &s5 (edge d $v) $v)) (size-atom $r))
!(bind! &s6 (new-space))
!(add-atom &s6 (edge c e))
!(add-atom &s6 (edge c d))
!(add-atom &s6 (edge b c))
!(add-atom &s6 (edge a b))
(= (note6 $x) (add-atom &s6 (edge d $x)))
!(let $r (collapse (match &s6 (edge $x $y) (match &s6 (edge $y $z)
     (match &s6 (edge $z $w) (note6 $w))))) (size-atom $r))
!(let $r (collapse (match &s6 (edge d $v) $v)) (size-atom $r))

; A write to another space does not change what the reads see: 2 answers.
!(bind! &s7 (new-space))
!(bind! &out7 (new-space))
!(add-atom &s7 (edge a b))
!(add-atom &s7 (edge b c))
!(add-atom &s7 (edge c d))
!(add-atom &s7 (edge c e))
!(let $r (collapse (match &s7 (edge $x $y) (match &s7 (edge $y $z)
     (match &s7 (edge $z $w) (add-atom &out7 (path $x $w)))))) (size-atom $r))
!(let $r (collapse (match &out7 (path $x $w) ($x $w))) (size-atom $r))

; Removing the rows a read returned: the read is complete, so each row's
; body runs, and the rows are gone afterwards.
!(bind! &s8 (new-space))
!(add-atom &s8 (t 1))
!(add-atom &s8 (t 2))
!(add-atom &s8 (t 3))
(= (drop8 $x) (remove-atom &s8 (t $x)))
!(let $r (collapse (match &s8 (t $x) (let $u (drop8 $x) $x))) (size-atom $r))
!(collapse (match &s8 (t $x) $x))

; A body that writes nothing answers the same either way: 2 answers.
!(bind! &s9 (new-space))
!(add-atom &s9 (edge a b))
!(add-atom &s9 (edge b c))
!(add-atom &s9 (edge c d))
!(add-atom &s9 (edge c e))
!(let $r (collapse (match &s9 (edge $x $y) (match &s9 (edge $y $z)
     (match &s9 (edge $z $w) (path $x $w))))) (size-atom $r))

; Rows hold code.  The body (path $x $w) is a constructor, but the third leg
; binds $w to a stored (add-atom ...), which evaluating the body runs.  The
; later legs are then read after that write: 2 answers, one write.
!(bind! &s10 (new-space))
!(add-atom &s10 (edge a b))
!(add-atom &s10 (edge b c))
!(add-atom &s10 (edge c (add-atom &s10 (edge c d))))
!(add-atom &s10 (edge d e))
!(let $r (collapse (match &s10 (edge $x $y) (match &s10 (edge $y $z)
     (match &s10 (edge $z $w) (path $x $w))))) (size-atom $r))
!(let $r (collapse (match &s10 (edge c d) found)) (size-atom $r))
