; Counts over a pattern or a conjunction count a stored variable, which
; matches every pattern, as ordinary search does, whether the facts it joins
; are rows read through the trie or a relation holds them.  This is HE's: a
; PeTTa program stores no bare variable, since its add-atom of one raises as
; assertz does.
!(bind! &counted (new-space native))
!(add-atom &counted (edge 1 2))
!(add-atom &counted (edge 2 3))
!(add-atom &counted (edge 3 4))
!(add-atom &counted (edge 2 5))
; A head past the threshold of rows, made by the program's own adds.
(= (fill $n)
   (if (> $n 0)
       (let* (($m (- $n 1)) ($_ (add-atom &counted (link $n $m)))) (fill $m))
       done))
!(fill 4200)
; Rows of two heads, each below the count at which a head takes its
; relation, so that the counts below read the space's rows through its trie.
(= (pad $n)
   (if (> $n 0)
       (let* (($m (- $n 1)) ($_ (add-atom &counted (pad-a $n)))
              ($_ (add-atom &counted (pad-b $n))))
             (pad $m))
       done))
!(pad 2100)
!(size (collapse (match &counted (, (link $x $y) (edge $y $z)) 1)))
; A stored variable meets each pattern once.
!(add-atom &counted $v)
!(size (collapse (match &counted (edge $x $y) 2)))
!(size (collapse (match &counted (, (edge $x $y) (edge $y $z)) 1)))
; A stored variable in a conjunction one of whose patterns meets a relation:
; the cursors count it.
!(size (collapse (match &counted (, (link $x $y) (edge $y $z)) 1)))
!(size (collapse (match &counted (, (link $x $y) (link $y $z)) 1)))
