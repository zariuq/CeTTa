; One indexed PathMap activation must freshen each distinct stored VarId once.
; The derived theorem below contains two variables with the same base lineage
; but different incoming epochs; they must remain distinct after re-opening,
; while repeated occurrences of the middle variable must remain co-referent.

(= (opening-id-fs2 $rb (: $x1 $a1) (: $x2 $a2))
   (match $rb (: $f (-> $a1 $a2 $b)) (: ($f $x1 $x2) $b)))

(= (opening-id-fs $rb $ss)
   (opening-id-fs2 $rb
     (match $ss (: $x1 $a1) (: $x1 $a1))
     (match $ss (: $x2 $a2) (: $x2 $a2))))

(= (opening-id-pair $rb $rows)
   (let ($r1 $tail) (decons-atom $rows)
     (let ($r2 $_rest) (decons-atom $tail)
       (opening-id-fs2 $rb $r1 $r2))))

!(bind! &opening-id-rb (new-space pathmap))
!(add-atom &opening-id-rb (: ax-mp (-> (→ $p $q) $p $q)))

!(bind! &opening-id-ss (new-space pathmap))
!(add-atom &opening-id-ss (: ax-1 (→ $p (→ $q $p))))
!(let $rows (collapse (opening-id-fs &opening-id-rb &opening-id-ss))
   (let $row (superpose $rows)
     (add-atom &opening-id-ss $row)))

!(assertAlphaEqualToResult
  (match &opening-id-ss (: (ax-mp ax-1 ax-1) $type) $type)
  ((→ $outer (→ $middle (→ $inner $middle)))))

; Counted duplicates are separate logical activations even when their stored
; term and source VarIds are identical.  Collapsing one indexed match must not
; make the two answer occurrences share their freshly opened variables.
!(bind! &opening-id-duplicates (new-space pathmap))
!(let $row (: ax-1 (→ $p (→ $q $p)))
   (let $_unit (add-atom &opening-id-duplicates $row)
     (add-atom &opening-id-duplicates $row)))
!(assertAlphaEqualToResult
  (let $rows
    (collapse
      (match &opening-id-duplicates (: $x $a) (: $x $a)))
    (opening-id-pair &opening-id-rb $rows))
  ((: (ax-mp ax-1 ax-1)
      (→ $outer (→ $middle (→ $inner $middle))))))
