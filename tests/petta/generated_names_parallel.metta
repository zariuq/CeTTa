; Names a parallel branch draws come from that branch's own supply, its
; pusher's next push when the branch is queued (a part of the pusher's pool
; that the push's count gives), so they depend on which branch drew them and
; in what order, never on which worker ran it or when.  The names are
; printed: a program gives the same names on every run and on any number
; of workers.  The root's names, a branch's and its siblings' are all
; distinct, and the text of a name a branch made reaches nothing at the
; root.
(= (named $label) (let $s (new-space) (Named $label (repr $s))))
(= (holding $label $n) (let* (($s (new-space)) ($_ (add-atom $s (held $n)))) (Held $label $s (repr $s))))
(= (root-name) (let $s (new-space) (repr $s)))
(= (text-of $pair) (let (Named $label $t) $pair $t))
(= (made $label) (let $s (new-space) (Made $label $s (repr $s))))
(= (joined (Made $label $s $t)) (Joined $label $s (== (repr $s) $t) (get-metatype $s)))

!(Root (root-name))
!(sort-atom (collapse (hyperpose ((named b0) (named b1) (named b2)))))
!(sort-atom (collapse (hyperpose ((named c0) (Pair (named c1) (named c2)) (named c3)))))
!(Root (root-name))

; Every name drawn by the root, before and after its branches, or by a
; branch, is fresh.
!(let* (($before (root-name))
        ($pairs (collapse (hyperpose ((named d0) (named d1) (named d2) (named d3)))))
        ($after (root-name))
        ($all (cons-atom $before (cons-atom $after (collapse (text-of (superpose $pairs)))))))
   (Fresh (size-atom $all) (size-atom (unique-atom $all))))

; A space a branch made keeps its rows at the root, and its name's text,
; built again, is a symbol of its own.
!(let (Held $label $s $t) (hyperpose ((holding f0 7))) (Forged (collapse (match $s (held $x) $x)) (collapse (match (atom_concat $t "") (held $x) $x)) (== (atom_concat $t "") $s)))

; The space a branch made is that space at the root: it prints as its name,
; the name the branch read, and its metatype is Symbol.
!(sort-atom (collapse (joined (hyperpose ((made g0) (made g1))))))
