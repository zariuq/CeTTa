; Python's built-in containers on CeTTa's PeTTa lane, for host_runtime.metta:
; lists, tuples, dicts, sets, ranges and iterators; subscripts and slices;
; repr; the built-in functions and the methods of the built-in types.
;
; A list is (py:list ID): (h:len ID N) and one (h:item ID I V) per element.
; A dict is (py:dict ID): (h:dent ID KEY ORIGINAL VALUE SEQ) per entry, KEY
; the canonical form of the key the dict found first (ORIGINAL), SEQ its
; place in insertion order; (h:dnext ID N) the next SEQ.  A set is
; (py:set ID), CPython's set table (see sets below).  A tuple is
; the value (py:tuple V...), a range (py:range START STOP STEP), an iterator
; (py:iter ID) with its state (h:it ID STATE).

; ---------------------------------------------------------------- heap cells

; add-atom and remove-atom take their atom as written: a value computed
; inside one is bound first.  An expression written in an evaluated place is
; a call: a tuple of values is always built under a constructor (py:pair,
; e, bcall ...) or with cons-atom, never written as (X Y)

(= (pyrt:heap-one $pattern $template)
   (let $xs (collapse (match &py-heap $pattern $template))
     (if (== $xs ()) (m:missing) (car-atom $xs))))
(= (pyrt:heap-replace $old $new)
   (let $r (pyrt:heap-remove $old) (add-atom &py-heap $new)))
; a heap record removed by its key, its value fields left open: a record
; holding a NaN, which equals nothing (itself included, as CeTTa compares
; floats), is removed as any other.  A record's key is its id and the fields
; that name it among its id's: a list item's index, a field's name, a dict
; entry's place in insertion order, a set entry's and slot's index.
(= (pyrt:heap-remove $r) (let $k (pyrt:heap-key $r) (remove-atom &py-heap $k)))
(= (pyrt:heap-key $r)
   (case $r
     (((h:item $id $i $v) (h:item $id $i $v1))
      ((h:f $id $name $v) (h:f $id $name $v1))
      ((h:it $id $st) (h:it $id $st1))
      ((h:len $id $n) (h:len $id $n1))
      ((h:cell $id $v) (h:cell $id $v1))
      ((h:dnext $id $n) (h:dnext $id $n1))
      ((h:dent $id $k $o $v $s) (h:dent $id $k1 $o1 $v1 $s))
      ((h:sent $id $k $o $i $h) (h:sent $id $k1 $o1 $i $h1))
      ((h:sslot $id $i $k) (h:sslot $id $i $k1))
      ($other $other))))

; ---------------------------------------------------------------- lists

(= (pyrt:list-new $xs)
   (let $id (pyrt:new-id)
     (let $w (pyrt:list-fill $id 0 $xs)
       (let $n (size-atom $xs)
         (let $a (add-atom &py-heap (h:len $id $n))
           (py:list $id))))))
(= (pyrt:list-fill $id $i $xs)
   (if (== $xs ()) ()
     (let ($x $rest) (decons-atom $xs)
       (let $a (add-atom &py-heap (h:item $id $i $x))
         (pyrt:list-fill $id (+ $i 1) $rest)))))
(= (pyrt:list-len (py:list $id)) (pyrt:heap-one (h:len $id $n) $n))
(= (pyrt:list-get (py:list $id) $i) (pyrt:heap-one (h:item $id $i $v) $v))
(= (pyrt:list-set (py:list $id) $i $v)
   (let $old (pyrt:list-get (py:list $id) $i) (pyrt:heap-replace (h:item $id $i $old) (h:item $id $i $v))))
(= (pyrt:list-append (py:list $id) $v)
   (let $n (pyrt:list-len (py:list $id))
     (let $a (add-atom &py-heap (h:item $id $n $v))
       (pyrt:heap-replace (h:len $id $n) (h:len $id (+ $n 1))))))
(= (pyrt:list-items $l) (pyrt:list-items-from $l 0 (pyrt:list-len $l)))
(= (pyrt:list-items-from $l $i $n)
   (if (>= $i $n) () (cons-atom (pyrt:list-get $l $i) (pyrt:list-items-from $l (+ $i 1) $n))))
; the last element removed and given
(= (pyrt:list-pop-last (py:list $id))
   (let $n (pyrt:list-len (py:list $id))
     (let $last (- $n 1)
       (let $v (pyrt:list-get (py:list $id) $last)
         (let $r (pyrt:heap-remove (h:item $id $last $v))
           (let $s (pyrt:heap-replace (h:len $id $n) (h:len $id $last)) $v))))))
; the list made to hold exactly XS
(= (pyrt:list-reset (py:list $id) $xs)
   (let $n (pyrt:list-len (py:list $id))
     (let $c (pyrt:list-clear-from $id 0 $n)
       (let $f (pyrt:list-fill $id 0 $xs)
         (pyrt:heap-replace (h:len $id $n) (h:len $id (size-atom $xs)))))))
(= (pyrt:list-clear-from $id $i $n)
   (if (>= $i $n) ()
     (let $v (pyrt:list-get (py:list $id) $i)
       (let $r (pyrt:heap-remove (h:item $id $i $v)) (pyrt:list-clear-from $id (+ $i 1) $n)))))

; ---------------------------------------------------------------- keys

; the canonical form of a hashable key: equal keys have one form (1, 1.0 and
; True; -0.0 and 0); (m:err E) for an unhashable one
(= (pyrt:key $k)
   (if (pyrt:is-bool $k) (m:val (if $k 1 0))
     (if (pyrt:is-int $k) (m:val $k)
       (if (pyrt:is-str $k) (m:val $k)
         (if (== $k (py:None)) (m:val $k)
           (if (== (get-metatype $k) Grounded) (m:val (pyrt:float-key $k))
             (case $k
               (((py:tuple) (m:val $k))
                ((py:obj $id) (pyrt:object-key $k))
                ((py:list $id) (pyrt:unhashable $k))
                ((py:dict $id) (pyrt:unhashable $k))
                ((py:set $id) (pyrt:unhashable $k))
                ((py:frozenset $id) (m:val (pyrt:frozenset-key $k)))
                ((py:Complex $r $i) (m:val (if (pyrt:float-zero $i) (pyrt:float-key $r) $k)))
                ((py:slice $sa $sb $sc) (case (pyrt:tuple-key ($sa $sb $sc) ()) (((m:val $kt) (m:val (cons-atom py:slice (cdr-atom $kt)))) ($ke $ke))))
                ($other (if (== (car-atom $k) py:tuple) (pyrt:tuple-key (cdr-atom $k) ()) (m:val $k)))))))))))
; a float equal to an int is the same key as that int (2.0 and 2)
(= (pyrt:float-key $x)
   (if (float:is-finite $x)
       (let $t (float:to-int $x) (if (== (float:compare-int $x $t) 0) $t $x))
       $x))
(= (pyrt:tuple-key $xs $acc)
   (if (== $xs ())
       (m:val (cons-atom py:tuple $acc))
       (let ($x $rest) (decons-atom $xs)
         (case (pyrt:key $x)
           (((m:val $kx) (pyrt:tuple-key $rest (pyrt:append $acc (cons-atom $kx ()))))
            ($e $e))))))
; a key looked up in a set (set_contains): a set as the frozenset of its
; members, once its own hash has failed
(= (pyrt:set-lookup-key $x) (case $x (((py:set $i) (m:val (pyrt:frozenset-key $x))) ($o (pyrt:key $x)))))
; a key for a dict or a set operation (dict_unhashable_type,
; set_unhashable_type): an unhashable key's TypeError, exactly TypeError, is
; restated naming the use
(= (pyrt:dict-key $k) (case (pyrt:key $k) (((m:err $e) (pyrt:key-refused $e $k "dict key")) ($r $r))))
(= (pyrt:set-key $k) (case (pyrt:key $k) (((m:err $e) (pyrt:key-refused $e $k "set element")) ($r $r))))
(= (pyrt:set-member-key $k) (case (pyrt:set-lookup-key $k) (((m:err $e) (pyrt:key-refused $e $k "set element")) ($r $r))))
(= (pyrt:key-refused $e $k $use)
   (let $x (pyrt:exc-instance $e)
     (if (== (pyrt:type-of $x) (py:type TypeError))
         (m:err (pyrt:exc "TypeError" (str:concat (str:concat (str:concat (str:concat (str:concat "cannot use '" (pyrt:type-fqn (pyrt:type-of $k))) "' as a ") $use) " (")
                                      (str:concat (pyrt:text-of (pyrt:str-of $x)) ")"))))
         (m:err $e))))
; a frozenset's key: its members' keys in order, so equal frozensets are one key
(= (pyrt:frozenset-key $f)
   (let $id (pyrt:set-id $f) (cons-atom py:fskey (sort-atom (collapse (match &py-heap (h:sent $id $k $o $i $h) $k))))))
(= (pyrt:unhashable $k) (m:err (pyrt:exc "TypeError" (str:concat "unhashable type: " (pyrt:quoted (pyrt:type-name $k))))))

; ---------------------------------------------------------------- dicts

(= (pyrt:dict-new)
   (let $id (pyrt:new-id) (let $a (add-atom &py-heap (h:dnext $id 0)) (py:dict $id))))
(= (pyrt:dict-entry (py:dict $id) $key)
   (let $k (pyrt:resolve-dict-key $id $key) (pyrt:heap-one (h:dent $id $k $o $v $s) (h:dent $id $k $o $v $s))))
; a value by canonical key, or (m:missing)
(= (pyrt:dict-get (py:dict $id) $key)
   (let $k (pyrt:resolve-dict-key $id $key) (pyrt:heap-one (h:dent $id $k $o $v $s) $v)))
; a key hashed by a program's __hash__, (py:hkey HASH OBJECT): the stored key
; with the same hash whose object is it or equal to it (CPython's probe and
; compare), else the key as it is
(= (pyrt:resolve-dict-key $id $key)
   (case $key
     (((py:hkey $h $x) (pyrt:first-equal-hkey (collapse (match &py-heap (h:dent $id (py:hkey $h $y) $o $v $s) (py:hkey $h $y))) $x $key))
      ($other $key))))
(= (pyrt:resolve-set-key $id $key)
   (case $key
     (((py:hkey $h $x) (pyrt:first-equal-hkey (collapse (match &py-heap (h:sent $id (py:hkey $h $y) $o $s $hh) (py:hkey $h $y))) $x $key))
      ($other $key))))
(= (pyrt:first-equal-hkey $cands $x $default)
   (if (== (size-atom $cands) 0) $default
     (let ((py:hkey $h $y) $rest) (decons-atom $cands)
       (if (pyrt:py-eq $y $x) (py:hkey $h $y) (pyrt:first-equal-hkey $rest $x $default)))))
(= (pyrt:dict-set (py:dict $id) $key $value) (pyrt:dict-put (py:dict $id) $key $key $value))
; put by canonical KEY; a new key keeps ORIGINAL and the next place in order
(= (pyrt:dict-put (py:dict $id) $key $original $value)
   (let $e (pyrt:dict-entry (py:dict $id) $key)
     (if (== $e (m:missing))
         (let $s (pyrt:heap-one (h:dnext $id $s) $s)
           (let $n (pyrt:heap-replace (h:dnext $id $s) (h:dnext $id (+ $s 1)))
             (add-atom &py-heap (h:dent $id $key $original $value $s))))
         (let (h:dent $i $k $o $v $s) $e
           (pyrt:heap-replace $e (h:dent $i $k $o $value $s))))))
(= (pyrt:dict-delete (py:dict $id) $key)
   (let $e (pyrt:dict-entry (py:dict $id) $key)
     (if (== $e (m:missing)) false (let $r (pyrt:heap-remove $e) true))))
; the entries in insertion order: (ORIGINAL VALUE) pairs
(= (pyrt:dict-entries (py:dict $id))
   (let $es (collapse (match &py-heap (h:dent $id $k $o $v $s) (e $s $o $v)))
     (pyrt:entries-of (sort-atom $es))))
(= (pyrt:entries-of $es)
   (if (== $es ()) ()
     (let ((e $s $o $v) $rest) (decons-atom $es) (cons-atom (py:pair $o $v) (pyrt:entries-of $rest)))))
(= (pyrt:dict-len (py:dict $id)) (size-atom (collapse (match &py-heap (h:dent $id $k $o $v $s) $s))))
(= (pyrt:dict-keys $d) (pyrt:firsts (pyrt:dict-entries $d)))
(= (pyrt:dict-values $d) (pyrt:seconds (pyrt:dict-entries $d)))
(= (pyrt:firsts $ps) (if (== $ps ()) () (let ((py:pair $a $b) $rest) (decons-atom $ps) (cons-atom $a (pyrt:firsts $rest)))))
(= (pyrt:seconds $ps) (if (== $ps ()) () (let ((py:pair $a $b) $rest) (decons-atom $ps) (cons-atom $b (pyrt:seconds $rest)))))
(= (pyrt:pairs-as-tuples $ps)
   (if (== $ps ()) () (let ((py:pair $a $b) $rest) (decons-atom $ps) (cons-atom (py:tuple $a $b) (pyrt:pairs-as-tuples $rest)))))

; ---------------------------------------------------------------- sets

; A set is CPython's set table (Objects/setobject.c), so that it iterates as
; CPython's does: (h:stab ID MASK FILL USED FINGER); a member's entry
; (h:sent ID KEY ORIGINAL INDEX HASH), KEY canonical (pyrt:key), HASH
; CPython's; and each used place (h:sslot ID INDEX KEY), a removed member's
; place holding (m:dummy).  Members iterate by place.  A set is (py:set ID),
; a frozenset (py:frozenset ID), the same table: a frozenset is built as a
; set and changes no more once a program holds it.
(= (pyrt:set-new)
   (let $id (pyrt:new-id) (let $a (add-atom &py-heap (h:stab $id 7 0 0 0)) (py:set $id))))
(= (pyrt:set-id (py:set $id)) $id)
(= (pyrt:set-id (py:frozenset $id)) $id)
; a set subclass's instance: its set part's table (the layout it inherits)
(= (pyrt:set-id (py:obj $i)) (pyrt:set-id (pyrt:field (py:obj $i) value)))
(= (pyrt:freeze (py:set $id)) (py:frozenset $id))
(= (pyrt:is-anyset $x) (case $x (((py:set $i) true) ((py:frozenset $i) true) ($o false))))
(= (pyrt:set-table $id) (pyrt:heap-one (h:stab $id $m $f $u $g) (h:stab $id $m $f $u $g)))
(= (pyrt:set-entry $s $key)
   (let $id (pyrt:set-id $s)
     (let $k (pyrt:resolve-set-key $id $key) (pyrt:heap-one (h:sent $id $k $o $i $h) (h:sent $id $k $o $i $h)))))
(= (pyrt:set-slot $id $i) (pyrt:heap-one (h:sslot $id $i $k) $k))
(= (pyrt:set-items $s)
   (let $id (pyrt:set-id $s)
     (pyrt:seconds (sort-atom (collapse (match &py-heap (h:sent $id $k $o $i $h) (py:pair $i $o)))))))
(= (pyrt:set-len $s) (let (h:stab $id $m $f $u $g) (pyrt:set-table (pyrt:set-id $s)) $u))
; a member in the order of the places: (INDEX KEY ORIGINAL HASH)
(= (pyrt:set-members $id)
   (pyrt:seconds (sort-atom (collapse (match &py-heap (h:sent $id $k $o $i $h) (py:pair $i (c:member $i $k $o $h)))))))
; add (set_add_entry): a key not there takes the last removed place on its
; probe sequence before the first unused one, else that one; a new place may
; grow the table (set_table_resize)
(= (pyrt:set-add $s $key $original)
   (if (== (pyrt:set-entry $s $key) (m:missing))
       (pyrt:set-insert (pyrt:set-id $s) $key $original (pyrt:key-hash $key $original))
       ()))
(= (pyrt:set-insert $id $key $orig $h)
   (let (h:stab $id $mask $fill $used $finger) (pyrt:set-table $id)
     (let $u (pyrt:u64 $h)
       (let (c:place $i $fresh) (pyrt:set-probe $id $mask $u (% $u (+ $mask 1)) (m:none))
         (let $p (pyrt:set-place $id $i $key $orig $h)
           (let $fill2 (if $fresh (+ $fill 1) $fill)
             (let $used2 (+ $used 1)
               (let $t (pyrt:heap-replace (h:stab $id $mask $fill $used $finger) (h:stab $id $mask $fill2 $used2 $finger))
                 (if (if $fresh (>= (* $fill2 5) (* $mask 3)) false)
                     (pyrt:set-resize $id (if (> $used2 50000) (* $used2 2) (* $used2 4)))
                     ())))))))))
(= (pyrt:set-probe $id $mask $perturb $i $free)
   (case (pyrt:set-scan $id $i (if (<= (+ $i 9) $mask) 10 1) $free)
     (((c:place $j $fresh) (c:place $j $fresh))
      ((c:go-on $free2)
       (let $p2 (integer:shift-right $perturb 5)
         (pyrt:set-probe $id $mask $p2 (% (+ (+ (* $i 5) 1) $p2) (+ $mask 1)) $free2))))))
(= (pyrt:set-scan $id $i $n $free)
   (if (== $n 0) (c:go-on $free)
     (let $k (pyrt:set-slot $id $i)
       (if (== $k (m:missing))
           (if (== $free (m:none)) (c:place $i true) (c:place $free false))
           (pyrt:set-scan $id (+ $i 1) (- $n 1) (if (== $k (m:dummy)) $i $free))))))
(= (pyrt:set-place $id $i $key $orig $h)
   (let $old (pyrt:set-slot $id $i)
     (let $r (if (== $old (m:missing)) () (pyrt:heap-remove (h:sslot $id $i $old)))
       (let $a (add-atom &py-heap (h:sslot $id $i $key))
         (add-atom &py-heap (h:sent $id $key $orig $i $h))))))
; a new table of the least power of two above MINUSED (at least 8), the
; members put back in their old places' order on fresh probe sequences; a
; minimal table without removed places is left as it is
(= (pyrt:set-resize $id $minused)
   (let (h:stab $id $mask $fill $used $finger) (pyrt:set-table $id)
     (let $size (pyrt:pow2-above 8 $minused)
       (if (if (== $size 8) (if (== $mask 7) (== $fill $used) false) false) ()
         (let $ms (pyrt:set-members $id)
           (let $c (pyrt:set-forget-places $id)
             (let $t (pyrt:heap-replace (h:stab $id $mask $fill $used $finger) (h:stab $id (- $size 1) $used $used $finger))
               (pyrt:set-put-back $id (- $size 1) $ms))))))))
(= (pyrt:pow2-above $n $min) (if (> $n $min) $n (pyrt:pow2-above (* $n 2) $min)))
(= (pyrt:set-forget-places $id)
   (let $a (pyrt:remove-all (collapse (match &py-heap (h:sslot $id $i $k) (h:sslot $id $i $k))))
     (pyrt:remove-all (collapse (match &py-heap (h:sent $id $k $o $i $h) (h:sent $id $k $o $i $h))))))
(= (pyrt:set-put-back $id $mask $ms)
   (if (== $ms ()) ()
     (let ((c:member $i0 $k $o $h) $rest) (decons-atom $ms)
       (let $u (pyrt:u64 $h)
         (let (c:place $i $fresh) (pyrt:set-probe $id $mask $u (% $u (+ $mask 1)) (m:none))
           (let $p (pyrt:set-place $id $i $k $o $h) (pyrt:set-put-back $id $mask $rest)))))))
; discard (set_discard_entry): the member's place keeps (m:dummy)
(= (pyrt:set-discard $s $key)
   (let $e (pyrt:set-entry $s $key)
     (if (== $e (m:missing)) false
       (let (h:sent $id $k $o $i $h) $e
         (let $r1 (pyrt:heap-remove $e)
           (let $r2 (pyrt:heap-remove (h:sslot $id $i $k))
             (let $a (add-atom &py-heap (h:sslot $id $i (m:dummy)))
               (let (h:stab $id $mask $fill $used $finger) (pyrt:set-table $id)
                 (let $t (pyrt:heap-replace (h:stab $id $mask $fill $used $finger) (h:stab $id $mask $fill (- $used 1) $finger))
                   true)))))))))
; pop (set_pop): the first member from the finger on, the finger after it
(= (pyrt:set-pop $s)
   (let (h:stab $id $mask $fill $used $finger) (pyrt:set-table (pyrt:set-id $s))
     (if (== $used 0) (m:err (pyrt:exc "KeyError" "pop from an empty set"))
       (let $i (pyrt:set-next-member $id (% $finger (+ $mask 1)) $mask)
         (let $k (pyrt:set-slot $id $i)
           (let (h:sent $id $k $o $j $h) (pyrt:heap-one (h:sent $id $k $o1 $j1 $h1) (h:sent $id $k $o1 $j1 $h1))
             (let $d (pyrt:set-discard $s $k)
               (let (h:stab $id $mask2 $fill2 $used2 $finger2) (pyrt:set-table $id)
                 (let $t (pyrt:heap-replace (h:stab $id $mask2 $fill2 $used2 $finger2) (h:stab $id $mask2 $fill2 $used2 (+ $i 1)))
                   (m:val $o))))))))))
(= (pyrt:set-next-member $id $i $mask)
   (let $k (pyrt:set-slot $id $i)
     (if (if (== $k (m:missing)) true (== $k (m:dummy)))
         (pyrt:set-next-member $id (if (>= $i $mask) 0 (+ $i 1)) $mask)
         $i)))
; clear (set_clear_internal): an empty minimal table
(= (pyrt:set-clear $s)
   (let (h:stab $id $mask $fill $used $finger) (pyrt:set-table (pyrt:set-id $s))
     (let $c (pyrt:set-forget-places $id)
       (pyrt:heap-replace (h:stab $id $mask $fill $used $finger) (h:stab $id 7 0 0 $finger)))))
; update from another set (set_merge): the table grown first when needed; an
; empty set of the same size without removed places is a copy place by place;
; an empty one takes the members in the other's order on fresh probes; else
; each is added
(= (pyrt:set-merge $s $o)
   (let (h:stab $oid $omask $ofill $oused $ofinger) (pyrt:set-table (pyrt:set-id $o))
     (if (if (== (pyrt:set-id $s) $oid) true (== $oused 0)) ()
       (let (h:stab $id $mask $fill $used $finger) (pyrt:set-table (pyrt:set-id $s))
         (let $g (if (>= (* (+ $fill $oused) 5) (* $mask 3)) (pyrt:set-resize $id (* (+ $used $oused) 2)) ())
           (let (h:stab $id $mask2 $fill2 $used2 $finger2) (pyrt:set-table $id)
             (let $ms (pyrt:set-members $oid)
               (if (if (== $fill2 0) (if (== $mask2 $omask) (== $ofill $oused) false) false)
                   (let $p (pyrt:set-copy-places $id $ms)
                     (pyrt:heap-replace (h:stab $id $mask2 $fill2 $used2 $finger2) (h:stab $id $mask2 $ofill $oused $finger2)))
                   (if (== $fill2 0)
                       (let $p (pyrt:set-put-back $id $mask2 $ms)
                         (pyrt:heap-replace (h:stab $id $mask2 $fill2 $used2 $finger2) (h:stab $id $mask2 $oused $oused $finger2)))
                       (pyrt:set-add-members $id $ms))))))))))
(= (pyrt:set-copy-places $id $ms)
   (if (== $ms ()) ()
     (let ((c:member $i $k $o $h) $rest) (decons-atom $ms)
       (let $p (pyrt:set-place $id $i $k $o $h) (pyrt:set-copy-places $id $rest)))))
(= (pyrt:set-add-members $id $ms)
   (if (== $ms ()) ()
     (let ((c:member $i $k $o $h) $rest) (decons-atom $ms)
       (let $a (pyrt:set-add (py:set $id) $k $o) (pyrt:set-add-members $id $rest)))))
(= (pyrt:set-copy $s) (let $n (pyrt:set-new) (let $m (pyrt:set-merge $n $s) $n)))
; set_swap_bodies: two tables (by id) trade their members and places; each
; keeps its pop finger
(= (pyrt:set-swap-bodies $a $b)
   (let (h:stab $a $ma $fa $ua $ga) (pyrt:set-table $a)
     (let (h:stab $b $mb $fb $ub $gb) (pyrt:set-table $b)
       (let $ea (collapse (match &py-heap (h:sent $a $k $o $i $h) (h:sent $a $k $o $i $h)))
         (let $eb (collapse (match &py-heap (h:sent $b $k $o $i $h) (h:sent $b $k $o $i $h)))
           (let $sa (collapse (match &py-heap (h:sslot $a $i $k) (h:sslot $a $i $k)))
             (let $sb (collapse (match &py-heap (h:sslot $b $i $k) (h:sslot $b $i $k)))
               (let $r (pyrt:remove-all (pyrt:append (pyrt:append $ea $eb) (pyrt:append $sa $sb)))
                 (let $t1 (pyrt:heap-replace (h:stab $a $ma $fa $ua $ga) (h:stab $a $mb $fb $ub $ga))
                   (let $t2 (pyrt:heap-replace (h:stab $b $mb $fb $ub $gb) (h:stab $b $ma $fa $ua $gb))
                     (let $x1 (pyrt:add-all (pyrt:retag (pyrt:append $eb $sb) $a))
                       (pyrt:add-all (pyrt:retag (pyrt:append $ea $sa) $b)))))))))))))
; heap atoms (TAG ID FIELD ...) given another ID
(= (pyrt:retag $atoms $id)
   (if (== $atoms ()) ()
     (let ($x $rest) (decons-atom $atoms)
       (cons-atom (cons-atom (car-atom $x) (cons-atom $id (cdr-atom (cdr-atom $x)))) (pyrt:retag $rest $id)))))
(= (pyrt:add-all $as)
   (if (== $as ()) () (let ($x $rest) (decons-atom $as) (let $r (add-atom &py-heap $x) (pyrt:add-all $rest)))))
; s.update(x) and SET_UPDATE (set_update_internal): another set is merged
; (set_merge); a dict's keys are added in its order after one growth for them
; all; any other iterable's items are added as they come
(= (pyrt:set-update $s $x)
   (case $x
     (((py:set $oid) (let $m (pyrt:set-merge $s $x) (m:val $s)))
      ((py:frozenset $oid) (let $m (pyrt:set-merge $s $x) (m:val $s)))
      ((py:dict $did) (pyrt:set-update-dict $s $x))
      ($other (case (pyrt:now (pyrt:iter $x)) (((m:val $it) (pyrt:set-add-from $s $it)) ($e $e)))))))
(= (pyrt:set-update-dict $s $d)
   (let (h:stab $id $mask $fill $used $finger) (pyrt:set-table (pyrt:set-id $s))
     (let $n (pyrt:dict-len $d)
       (let $g (if (>= (* (+ $fill $n) 5) (* $mask 3)) (pyrt:set-resize $id (* (+ $used $n) 2)) ())
         (pyrt:fill-set $s (pyrt:dict-keys $d))))))
(= (pyrt:set-add-from $s $it)
   (case (pyrt:next-of $it)
     (((m:val (m:exhausted)) (m:val $s))
      ((m:val $v) (case (pyrt:set-key $v) (((m:val $k) (let $a (pyrt:set-add $s $k $v) (pyrt:set-add-from $s $it))) ($r $r))))
      ($r $r))))
; the set a display of constants is built from, CPython's frozenset constant,
; made once as the optimizer makes it (flowgraph.c optimize_lists_and_sets):
; the constants added in order.  The constant cache keeps that set as it is
; (_PyCompile_ConstCacheMergeOne merges without rebuilding, unlike 3.13's
; merge_consts_recursive).  A set the compiler found built again, (c:const-set-
; rebuilt VALUES), had a member interned as another object when its code
; object was made, and intern_constants then made a new set of its members
; in their places' order (pyhost:merge-const-sets decides it).  Known by its
; constants' written form, which keeps 0.0 and -0.0 apart as CPython's
; constants are.
(= (pyrt:const-set $vs) (pyrt:const-set-as $vs false))
(= (pyrt:const-set-as $vs $rebuilt)
   (let $key (repr (h:const-set-key $vs $rebuilt))
     (let $known (pyrt:heap-one (h:const-set $key $s) $s)
       (if (== $known (m:missing))
           (let $s1 (pyrt:set-new)
             (let $f (pyrt:const-set-fill $s1 $vs)
               (let $s2 (if $rebuilt (pyrt:set-rebuilt $s1) $s1)
                 (let $fz (pyrt:freeze $s2)
                   (let $a (add-atom &py-heap (h:const-set $key $fz)) $fz)))))
           $known))))
; a new set of a set's members, added in their places' order
(= (pyrt:set-rebuilt (py:set $id1))
   (let $s2 (pyrt:set-new)
     (let (py:set $id2) $s2
       (let $m (pyrt:set-add-members $id2 (pyrt:set-members $id1)) $s2))))
(= (pyrt:const-set-fill $s $vs)
   (case $vs
     (((LNil) $s)
      ((LCons $v $rest)
       (case (pyrt:set-key $v) (((m:val $k) (let $a (pyrt:set-add $s $k $v) (pyrt:const-set-fill $s $rest)))))))))

; ---------------------------------------------------------------- hashes

; CPython's hash of a key (PYTHONHASHSEED=0), a signed 64-bit integer: an
; int's modulo 2**61 - 1 with its sign (-1 is -2), a float's as the rational
; it is (_Py_HashDouble), a str's by SipHash-1-3 over its compact code units,
; a tuple's by the xxHash rounds (tuplehash), None's constant, a program
; object's __hash__ (in its key) or its identity
(= (pyrt:key-hash $key $orig)
   (case $key
     (((py:hkey $h $o) $h)
      ($k (pyrt:hash-value $orig)))))
(= (pyrt:hash-value $x)
   (if (pyrt:is-bool $x) (if $x 1 0)
     (if (pyrt:is-int $x) (pyrt:int-hash $x)
       (if (pyrt:is-str $x) (pyrt:str-hash $x)
         (if (== $x (py:None)) 4238894112
           (if (pyrt:is-float $x) (pyrt:float-hash $x)
             (if (pyrt:is-tuple $x) (pyrt:tuple-hash (cdr-atom $x))
               (pyrt:other-hash $x))))))))
; complex_hash; bytes hash as a text of the same bytes (both SipHash-1-3 over
; one byte a character); an object by its identity, as CPython hashes by
; address (so no order of such keys is CPython's)
(= (pyrt:other-hash $x)
   (case $x
     (((py:Complex $r $i) (pyrt:complex-hash $r $i))
      ((py:Bytes $b) (pyrt:str-hash $b))
      ((py:slice $sa $sb $sc) (pyrt:slice-hash $sa $sb $sc))
      ((py:frozenset $id) (pyrt:frozenset-hash $x))
      ((py:obj (lazy $n)) $n)
      ((py:obj $id) $id) ((py:func $id) $id) ((py:gen $id) $id) ((py:coro $id) $id) ((py:agen $id) $id)
      ((py:type $t) (if (pyrt:is-int $t) $t (pyrt:str-hash (repr $x))))
      ($other (pyrt:str-hash (repr $x))))))
; frozenset_hash: its members' hashes shuffled (_shuffle_bits) and combined by
; exclusive or, its size mixed in, then dispersed; modulo 2**64 (-1 is
; 590923713)
(= (pyrt:frozenset-hash $f)
   (let $id (pyrt:set-id $f)
     (let $hs (collapse (match &py-heap (h:sent $id $k $o $i $h) $h))
       (let $x0 (pyrt:xor-shuffled $hs 0)
         (let $x1 (integer:xor $x0 (% (* (+ (size-atom $hs) 1) 1927868237) 18446744073709551616))
           (let $x2 (integer:xor $x1 (integer:xor (integer:shift-right $x1 11) (integer:shift-right $x1 25)))
             (let $x3 (% (+ (* $x2 69069) 907133923) 18446744073709551616)
               (if (== $x3 18446744073709551615) 590923713 (pyrt:s64 $x3)))))))))
(= (pyrt:xor-shuffled $hs $acc)
   (if (== $hs ()) $acc
     (let ($h $rest) (decons-atom $hs) (pyrt:xor-shuffled $rest (integer:xor $acc (pyrt:shuffle-bits (pyrt:u64 $h)))))))
(= (pyrt:shuffle-bits $h)
   (% (* (integer:xor (integer:xor $h 89869747) (% (integer:shift-left $h 16) 18446744073709551616)) 3644798167)
      18446744073709551616))
; the real part's hash plus 1000003 times the imaginary part's, modulo 2**64
; (-1 is -2)
(= (pyrt:complex-hash $r $i)
   (let $c (% (+ (pyrt:u64 (pyrt:float-hash $r)) (* 1000003 (pyrt:u64 (pyrt:float-hash $i)))) 18446744073709551616)
     (let $h (pyrt:s64 $c) (if (== $h -1) -2 $h))))
(= (pyrt:u64 $h) (if (< $h 0) (+ $h 18446744073709551616) $h))
(= (pyrt:s64 $u) (if (>= $u 9223372036854775808) (- $u 18446744073709551616) $u))
(= (pyrt:str-hash $s) (let $h (pyrt:s64 (str:siphash13 0 0 $s)) (if (== $h -1) -2 $h)))
(= (pyrt:float-hash $v)
   (if (float:is-nan $v) 0
     (if (float:is-inf $v) (if (float:sign-bit $v) -314159 314159)
       (case (float:frexp $v)
         (((FloatFrexp $m $e)
           (let $x (pyrt:float-hash-digits (float:abs $m) $e 0)
             (let (c:fh $x1 $e1) $x
               (let $e2 (if (>= $e1 0) (% $e1 61) (- 60 (% (- -1 $e1) 61)))
                 (let $x2 (integer:or (integer:and (integer:shift-left $x1 $e2) 2305843009213693951)
                                      (integer:shift-right $x1 (- 61 $e2)))
                   (let $x3 (if (float:sign-bit $m) (- 0 $x2) $x2)
                     (if (== $x3 -1) -2 $x3))))))))))))
(= (pyrt:float-hash-digits $m $e $x)
   (if (== (float:compare $m 0.0) 0) (c:fh $x $e)
     (let $x1 (integer:or (integer:and (integer:shift-left $x 28) 2305843009213693951) (integer:shift-right $x 33))
       (let $m1 (float:mul $m 268435456.0)
         (let $y (float:to-int (float:trunc $m1))
           (let $m2 (float:sub $m1 (float:trunc $m1))
             (let $x2 (+ $x1 $y)
               (pyrt:float-hash-digits $m2 (- $e 28) (if (>= $x2 2305843009213693951) (- $x2 2305843009213693951) $x2)))))))))
(= (pyrt:tuple-hash $xs)
   (let $acc (pyrt:tuple-hash-lanes $xs 2870177450012600261)
     (let $acc2 (% (+ $acc (integer:xor (size-atom $xs) (integer:xor 2870177450012600261 3527539))) 18446744073709551616)
       (if (== $acc2 18446744073709551615) 1546275796 (pyrt:s64 $acc2)))))
(= (pyrt:tuple-hash-lanes $xs $acc)
   (if (== $xs ()) $acc
     (let ($x $rest) (decons-atom $xs)
       (let $lane (pyrt:u64 (pyrt:hash-value $x))
         (let $a1 (% (+ $acc (* $lane 14029467366897019727)) 18446744073709551616)
           (let $a2 (integer:or (integer:and (integer:shift-left $a1 31) 18446744073709551615) (integer:shift-right $a1 33))
             (pyrt:tuple-hash-lanes $rest (% (* $a2 11400714785074694791) 18446744073709551616))))))))

; ---------------------------------------------------------------- building

(= (pyrt:build-list $xs) (pyrt:list-new $xs))
(= (pyrt:build-set $xs)
   (let $s (pyrt:set-new) (pyrt:fill-set $s $xs)))
(= (pyrt:fill-set $s $xs)
   (if (== $xs ()) (m:val $s)
     (let ($x $rest) (decons-atom $xs)
       (case (pyrt:set-key $x)
         (((m:val $k) (let $a (pyrt:set-add $s $k $x) (pyrt:fill-set $s $rest)))
          ($e $e))))))
(= (pyrt:build-dict $pairs)
   (let $d (pyrt:dict-new) (pyrt:fill-dict $d $pairs)))
(= (pyrt:fill-dict $d $pairs)
   (if (== $pairs ()) (m:val $d)
     (let ((py:pair $k $v) $rest) (decons-atom $pairs)
       (case (pyrt:dict-key $k)
         (((m:val $ck) (let $a (pyrt:dict-put $d $ck $k $v) (pyrt:fill-dict $d $rest)))
          ($e $e))))))

; ---------------------------------------------------------------- lengths and items

(= (pyrt:len $x) (if (pyrt:needs-python $x) (pyrt:prelude-call "_len" (cons-atom $x ())) (pyrt:len-builtin $x)))
(= (pyrt:len-builtin $x)
   (if (pyrt:is-str $x) (m:val (str:codepoint-count $x))
     (case $x
       (((py:Bytes $b) (m:val (str:codepoint-count $b)))
        ((py:list $id) (m:val (pyrt:list-len $x)))
        ((py:dict $id) (m:val (pyrt:dict-len $x)))
        ((py:set $id) (m:val (pyrt:set-len $x)))
        ((py:frozenset $id) (m:val (pyrt:set-len $x)))
        ((py:range $a $b $s) (m:val (pyrt:range-len $a $b $s)))
        ((py:view $kind $d) (m:val (pyrt:dict-len $d)))
        ($other (if (pyrt:is-tuple $x) (m:val (- (size-atom $x) 1))
                  (m:err (pyrt:exc "TypeError" (str:concat (str:concat "object of type " (pyrt:quoted (pyrt:type-name $x))) " has no len()")))))))))
(= (pyrt:is-tuple $x) (if (== (get-metatype $x) Expression) (if (== $x ()) false (== (car-atom $x) py:tuple)) false))
; how many values a range gives: (b - a - 1) // s + 1 going up, likewise down
(= (pyrt:range-len $a $b $s)
   (if (> $s 0)
       (if (< $a $b) (+ (pyrt:floor-div (- (- $b $a) 1) $s) 1) 0)
       (if (> $a $b) (+ (pyrt:floor-div (- (- $a $b) 1) (- 0 $s)) 1) 0)))
(= (pyrt:floor-div $x $y) (/ (- $x (% $x $y)) $y))
; the elements of an iterable that holds them, as a MeTTa list
; a value's items: a program's object's through its iterator (Python, from
; the host), a builtin value's here
(= (pyrt:elements $x)
   (if (pyrt:needs-python $x)
       (case (pyrt:prelude-sync "_iter" (cons-atom $x ())) (((m:val $it) (pyrt:drain $it ())) ($e $e)))
       (pyrt:elements-builtin $x)))
(= (pyrt:elements-builtin $x)
   (if (pyrt:is-str $x) (m:val (pyrt:chars $x))
     (case $x
       (((py:Bytes $b) (m:val (pyrt:seq-copy (str:codepoint-values $b))))
        ((py:list $id) (m:val (pyrt:list-items $x)))
        ((py:dict $id) (m:val (pyrt:dict-keys $x)))
        ((py:set $id) (m:val (pyrt:set-items $x)))
        ((py:frozenset $id) (m:val (pyrt:set-items $x)))
        ((py:range $a $b $s) (m:val (pyrt:range-items $a $b $s)))
        ((py:view keys $d) (m:val (pyrt:dict-keys $d)))
        ((py:view values $d) (m:val (pyrt:dict-values $d)))
        ((py:view items $d) (m:val (pyrt:pairs-as-tuples (pyrt:dict-entries $d))))
        ((py:iter $id) (pyrt:drain $x ()))
        ((py:gen $id) (pyrt:drain $x ()))
        ($other (if (pyrt:is-tuple $x) (m:val (cdr-atom $x)) (pyrt:not-iterable $x)))))))
; whether a value can be iterated (CPython: tp_iter, or the sequence protocol)
(= (pyrt:is-iterable $x)
   (if (pyrt:is-str $x) true
     (if (pyrt:is-tuple $x) true
       (case $x
         (((py:list $i) true) ((py:dict $i) true) ((py:set $i) true) ((py:frozenset $i) true) ((py:range $a $b $s) true)
          ((py:iter $i) true) ((py:gen $i) true) ((py:view $k $d) true) ((py:Bytes $b) true) ($o false))))))
(= (pyrt:not-iterable $x) (m:err (pyrt:exc "TypeError" (str:concat (pyrt:quoted (pyrt:type-name $x)) " object is not iterable"))))
(= (pyrt:range-items $a $b $s)
   (if (> $s 0) (if (< $a $b) (cons-atom $a (pyrt:range-items (+ $a $s) $b $s)) ())
     (if (> $a $b) (cons-atom $a (pyrt:range-items (+ $a $s) $b $s)) ())))
; a str's characters, one-codepoint strings (str:codepoints), as an expression
(= (pyrt:chars $s) (pyrt:chars-of (str:codepoints $s)))
(= (pyrt:chars-of $cs)
   (if (== (size-atom $cs) 0) ()
     (let ($c $rest) (decons-atom $cs) (cons-atom $c (pyrt:chars-of $rest)))))
(= (pyrt:drain $it $acc)
   (let $v (pyrt:next-of $it)
     (case $v
       (((m:val (m:exhausted)) (m:val $acc))
        ((m:val $x) (pyrt:drain $it (pyrt:append $acc (cons-atom $x ()))))
        ($e $e)))))

; ---------------------------------------------------------------- iterators

(= (pyrt:iter $x) (if (pyrt:needs-python $x) (pyrt:prelude-call "_iter" (cons-atom $x ())) (pyrt:iter-builtin $x)))
(= (pyrt:iter-builtin $x)
   (if (pyrt:is-str $x) (pyrt:iter-new (it:seq (pyrt:chars $x) 0))
     (case $x
       (((py:Bytes $b) (pyrt:iter-new (it:bytes (pyrt:seq-copy (str:codepoint-values $b)) 0)))
        ((py:list $id) (pyrt:iter-new (it:list $x 0)))
        ((py:range $a $b $s) (pyrt:iter-new (it:range $a $b $s)))
        ((py:iter $id) (m:val $x))
        ((py:gen $id) (m:val $x))
        ((py:dict $id) (pyrt:iter-new (it:seq (pyrt:dict-keys $x) 0)))
        ((py:set $id) (pyrt:iter-new (it:seq (pyrt:set-items $x) 0)))
        ((py:frozenset $id) (pyrt:iter-new (it:seq (pyrt:set-items $x) 0)))
        ((py:view $kind $d) (case (pyrt:elements $x) (((m:val $xs) (pyrt:iter-new (it:seq $xs 0))) ($e $e))))
        ($other (if (pyrt:is-tuple $x) (pyrt:iter-new (it:seq (cdr-atom $x) 0)) (pyrt:not-iterable $x)))))))
(= (pyrt:iter-new $state)
   (let $id (pyrt:new-id) (let $a (add-atom &py-heap (h:it $id $state)) (m:val (py:iter $id)))))
; the next value, or (m:exhausted)
(= (pyrt:next (py:iter $id))
   (let $st (pyrt:heap-one (h:it $id $s) $s)
     (case $st
       (((it:list $l $i)
         (if (< $i (pyrt:list-len $l))
             (let $u (pyrt:heap-replace (h:it $id $st) (h:it $id (it:list $l (+ $i 1)))) (m:val (pyrt:list-get $l $i)))
             (m:val (m:exhausted))))
        ((it:seq $xs $i)
         (if (< $i (size-atom $xs))
             (let $u (pyrt:heap-replace (h:it $id $st) (h:it $id (it:seq $xs (+ $i 1)))) (m:val (index-atom $xs $i)))
             (m:val (m:exhausted))))
        ((it:bytes $bxs $bi)
         (if (< $bi (size-atom $bxs))
             (let $u (pyrt:heap-replace (h:it $id $st) (h:it $id (it:bytes $bxs (+ $bi 1)))) (m:val (index-atom $bxs $bi)))
             (m:val (m:exhausted))))
        ; a bytearray's iterator reads its items as they are at each step
        ((it:bytearray $bl $bj)
         (if (< $bj (pyrt:list-len $bl))
             (let $u (pyrt:heap-replace (h:it $id $st) (h:it $id (it:bytearray $bl (+ $bj 1)))) (m:val (pyrt:list-get $bl $bj)))
             (m:val (m:exhausted))))
        ((it:range $a $b $s)
         (if (if (> $s 0) (< $a $b) (> $a $b))
             (let $u (pyrt:heap-replace (h:it $id $st) (h:it $id (it:range (+ $a $s) $b $s))) (m:val $a))
             (m:val (m:exhausted))))
        ((it:map $f $is)
         (case (pyrt:nexts-of $is ())
           (((m:val (m:exhausted)) (m:val (m:exhausted)))
            ((m:val $xs) (pyrt:call-sync $f $xs ()))
            ($e $e))))
        ((it:filter $f $i) (pyrt:filter-next $f $i))
        ((it:enumerate $i $n)
         (case (pyrt:next-of $i)
           (((m:val (m:exhausted)) (m:val (m:exhausted)))
            ((m:val $x) (let $u (pyrt:heap-replace (h:it $id $st) (h:it $id (it:enumerate $i (+ $n 1)))) (m:val (py:tuple $n $x))))
            ($e $e))))
        ((it:zip $is)
         (if (== $is ()) (m:val (m:exhausted))
           (case (pyrt:nexts-of $is ())
             (((m:val (m:exhausted)) (m:val (m:exhausted)))
              ((m:val $xs) (m:val (cons-atom py:tuple $xs)))
              ($e $e)))))
        ; the old protocol: __getitem__ from 0 until IndexError or StopIteration
        ((it:seqproto $o $k)
         (case (pyrt:prelude-sync "_getitem" (cons-atom $o (cons-atom $k ())))
           (((m:val $v) (let $u (pyrt:heap-replace (h:it $id $st) (h:it $id (it:seqproto $o (+ $k 1)))) (m:val $v)))
            ((m:err $e) (let $x (pyrt:exc-instance $e)
                          (if (if (pyrt:exc-is $x IndexError) true (pyrt:exc-is $x StopIteration)) (m:val (m:exhausted)) (m:err $e)))))))))))

; ---------------------------------------------------------------- subscripts

; a sequence's index (list_subscript and its kin): PyNumber_AsSsize_t's int
; (an int past an index-sized one is an IndexError), counted from the end
; when negative; WHAT names the sequence in the messages
(= (pyrt:index-of $i $n $what) (pyrt:seq-index $i $n (str:concat $what " index out of range") $what))
; the same, with the out-of-range message given (list_ass_subscript's
; "list assignment index out of range")
(= (pyrt:seq-index $i $n $oor $what)
   (case (pyrt:index-int $i)
     (((m:val $j)
       (if (pyrt:index-sized $j)
           (let $k (if (< $j 0) (+ $j $n) $j)
             (if (if (>= $k 0) (< $k $n) false) (m:val $k) (m:err (pyrt:exc "IndexError" $oor))))
           (pyrt:index-too-large $i)))
      ((m:missing) (pyrt:index-type-error $what $i))
      ($e $e))))
(= (pyrt:index-sized $j) (if (>= $j -9223372036854775808) (<= $j 9223372036854775807) false))
(= (pyrt:index-too-large $i)
   (m:err (pyrt:exc "IndexError" (str:concat (str:concat "cannot fit " (pyrt:quoted (pyrt:type-name $i))) " into an index-sized integer"))))
; a subscript that is neither an index nor a slice (str's words are its own)
(= (pyrt:index-type-error $what $i)
   (m:err (pyrt:exc "TypeError"
            (if (== $what "string")
                (str:concat "string indices must be integers, not " (pyrt:quoted (pyrt:type-name $i)))
                (str:concat (str:concat $what " indices must be integers or slices, not ") (pyrt:type-name $i))))))
(= (pyrt:is-slice $x) (case $x (((py:slice $p $q $r) true) ($o false))))

; ---------------------------------------------------------------- slices

; A slice (py:slice START STOP STEP) holds any three values, None where
; absent.  The items it selects of a sequence of N: (pyrt:slice-indices SL N)
; is (m:val (sl START STOP STEP LENGTH)), CPython's PySlice_Unpack then
; PySlice_AdjustIndices: the step, the start, the stop in turn each None or
; an index (__index__, beyond an index-sized integer held at its end); a step
; of zero refused; each bound counted from the end when negative, then held
; within the sequence; LENGTH the number of items selected
(= (pyrt:slice-indices $sl $n)
   (case (pyrt:slice-unpack $sl) (((m:val $u) (m:val (pyrt:slice-adjust $u $n))) ($e $e))))
; _PyEval_SliceIndex's refusal; slice.indices (evaluate_slice_index) names None too
(= (pyrt:slice-unpack $sl) (pyrt:slice-unpack-as $sl "slice indices must be integers or have an __index__ method"))
(= (pyrt:slice-unpack-as (py:slice $sa $sb $ss) $refusal)
   (case (pyrt:slice-bound $ss 1 $refusal)
     (((m:val $st)
       (if (== $st 0) (m:err (pyrt:exc "ValueError" "slice step cannot be zero"))
         (case (pyrt:slice-bound $sa (if (< $st 0) (sb:top) 0) $refusal)
           (((m:val $start)
             (case (pyrt:slice-bound $sb (if (< $st 0) (sb:bottom) (sb:top)) $refusal)
               (((m:val $stop) (m:val (us $start $stop $st)))
                ($e1 $e1))))
            ($e2 $e2)))))
      ($e3 $e3))))
; _PyEval_SliceIndex: None the default; an index; anything else refused
(= (pyrt:slice-bound $v $default $refusal)
   (if (== $v (py:None)) (m:val $default)
     (case (pyrt:index-int $v)
       (((m:missing) (m:err (pyrt:exc "TypeError" $refusal)))
        ($r $r)))))
; (sb:top) and (sb:bottom) stand for the largest and the smallest index
(= (pyrt:slice-adjust (us $start $stop $st) $n)
   (let $a (pyrt:adjust-bound $start $n $st)
     (let $z (pyrt:adjust-bound $stop $n $st)
       (sl $a $z $st (pyrt:slice-length $a $z $st)))))
(= (pyrt:adjust-bound $v $n $st)
   (case $v
     (((sb:top) (if (< $st 0) (- $n 1) $n))
      ((sb:bottom) (if (< $st 0) -1 0))
      ($k (if (< $k 0)
              (let $k2 (+ $k $n) (if (< $k2 0) (if (< $st 0) -1 0) $k2))
              (if (>= $k $n) (if (< $st 0) (- $n 1) $n) $k))))))
(= (pyrt:slice-length $a $z $st)
   (if (< $st 0)
       (if (< $z $a) (+ (pyrt:floor-div (- (- $a $z) 1) (- 0 $st)) 1) 0)
       (if (< $a $z) (+ (pyrt:floor-div (- (- $z $a) 1) $st) 1) 0)))
; the items a slice selects of an expression's items (0 the first)
(= (pyrt:slice-take $xs (sl $a $z $st $k)) (pyrt:take-every $xs $a $st $k))
(= (pyrt:take-every $xs $i $st $k)
   (if (<= $k 0) () (cons-atom (index-atom $xs $i) (pyrt:take-every $xs (+ $i $st) $st (- $k 1)))))
; the items a slice selects of a list, read one by one
(= (pyrt:list-take $l (sl $a $z $st $k)) (pyrt:list-take-every $l $a $st $k))
(= (pyrt:list-take-every $l $i $st $k)
   (if (<= $k 0) () (cons-atom (pyrt:list-get $l $i) (pyrt:list-take-every $l (+ $i $st) $st (- $k 1)))))
; slice.indices(length) (slice_indices): the bounds as a sequence of LENGTH
; reads them, a tuple
(= (pyrt:slice-indices-of $sl $len)
   (case (pyrt:index-int $len)
     (((m:val $n)
       (if (< $n 0) (m:err (pyrt:exc "ValueError" "length should not be negative"))
         (case (pyrt:slice-unpack-as $sl "slice indices must be integers or None or have an __index__ method")
           (((m:val $u) (let (sl $a $z $st $k) (pyrt:slice-adjust $u $n) (m:val (py:tuple $a $z $st)))) ($e $e)))))
      ((m:missing) (pyrt:not-an-integer $len))
      ($e $e))))
; slice(stop), slice(start, stop[, step]) (slice_new)
(= (pyrt:make-slice-of $args)
   (case (size-atom $args)
     ((0 (m:err (pyrt:exc "TypeError" "slice expected at least 1 argument, got 0")))
      (1 (m:val (py:slice (py:None) (car-atom $args) (py:None))))
      (2 (m:val (py:slice (index-atom $args 0) (index-atom $args 1) (py:None))))
      (3 (m:val (py:slice (index-atom $args 0) (index-atom $args 1) (index-atom $args 2))))
      ($k (m:err (pyrt:exc "TypeError" (str:concat "slice expected at most 3 arguments, got " (str:number->string $k))))))))
; a slice's hash (slice_hash, 3.12): its three parts' hashes in tuplehash's
; xxHash rounds, without the length
(= (pyrt:slice-hash $sa $sb $ss)
   (let $acc (pyrt:tuple-hash-lanes ($sa $sb $ss) 2870177450012600261)
     (if (== $acc 18446744073709551615) 1546275796 (pyrt:s64 $acc))))
(= (pyrt:getitem $x $i)
   (if (pyrt:needs-python $x) (pyrt:prelude-call "_getitem" (cons-atom $x (cons-atom $i ()))) (pyrt:getitem-builtin $x $i)))
(= (pyrt:getitem-builtin $x $i)
   (if (pyrt:is-str $x) (pyrt:str-getitem $x $i)
       (case $x
         (((py:Bytes $b) (pyrt:bytes-getitem $b $i))
          ((py:list $id) (pyrt:list-getitem $x $i))
          ((py:dict $id)
           (case (pyrt:dict-key $i)
             (((m:val $k) (let $v (pyrt:dict-get $x $k) (if (== $v (m:missing)) (m:err (pyrt:exc-args "KeyError" (cons-atom $i ()))) (m:val $v))))
              ($e $e))))
          ((py:range $a $b $s) (pyrt:range-getitem $a $b $s $i))
          ($other
           (if (pyrt:is-tuple $x) (pyrt:tuple-getitem $x $i)
               (m:err (pyrt:exc "TypeError" (str:concat (pyrt:quoted (pyrt:type-name $x)) " object is not subscriptable")))))))))
; s[i] a one-character str, s[i:j:k] a str
(= (pyrt:str-getitem $x $i)
   (if (pyrt:is-slice $i)
       (case (pyrt:slice-indices $i (str:codepoint-count $x))
         (((m:val $sl) (m:val (str:join "" (pyrt:slice-take (pyrt:chars $x) $sl)))) ($e $e)))
       (case (pyrt:index-of $i (str:codepoint-count $x) "string")
         (((m:val $j) (m:val (index-atom (pyrt:chars $x) $j))) ($e $e)))))
(= (pyrt:list-getitem $x $i)
   (if (pyrt:is-slice $i)
       (case (pyrt:slice-indices $i (pyrt:list-len $x)) (((m:val $sl) (m:val (pyrt:list-new (pyrt:list-take $x $sl)))) ($e $e)))
       (case (pyrt:index-of $i (pyrt:list-len $x) "list") (((m:val $j) (m:val (pyrt:list-get $x $j))) ($e $e)))))
(= (pyrt:tuple-getitem $x $i)
   (let $n (- (size-atom $x) 1)
     (if (pyrt:is-slice $i)
         (case (pyrt:slice-indices $i $n) (((m:val $sl) (m:val (cons-atom py:tuple (pyrt:slice-take (cdr-atom $x) $sl)))) ($e $e)))
         (case (pyrt:index-of $i $n "tuple") (((m:val $j) (m:val (index-atom $x (+ $j 1)))) ($e $e))))))
; r[i] an int, by arithmetic over an unbounded index (range_item); r[i:j:k]
; a range (compute_slice)
(= (pyrt:range-getitem $a $b $s $i)
   (let $n (pyrt:range-len $a $b $s)
     (if (pyrt:is-slice $i)
         (case (pyrt:slice-indices $i $n)
           (((m:val (sl $p $q $st $k)) (m:val (py:range (+ $a (* $p $s)) (+ $a (* $q $s)) (* $s $st))))
            ($e $e)))
         (case (pyrt:index-int $i)
           (((m:val $j0)
             (let $j (if (< $j0 0) (+ $j0 $n) $j0)
               (if (if (>= $j 0) (< $j $n) false) (m:val (+ $a (* $j $s)))
                   (m:err (pyrt:exc "IndexError" "range object index out of range")))))
            ((m:missing) (pyrt:index-type-error "range" $i))
            ($e $e))))))
(= (pyrt:setitem $x $i $v)
   (if (pyrt:needs-python $x) (pyrt:prelude-call "_setitem" (cons-atom $x (cons-atom $i (cons-atom $v ())))) (pyrt:setitem-builtin $x $i $v)))
(= (pyrt:setitem-builtin $x $i $v)
   (case $x
     (((py:list $id)
       (if (pyrt:is-slice $i) (pyrt:list-assign-slice $x $i $v)
         (case (pyrt:seq-index $i (pyrt:list-len $x) "list assignment index out of range" "list")
           (((m:val $j) (let $s (pyrt:list-set $x $j $v) (m:val (py:None)))) ($e $e)))))
      ((py:dict $id)
       (case (pyrt:dict-key $i) (((m:val $k) (let $s (pyrt:dict-put $x $k $i $v) (m:val (py:None)))) ($e $e))))
      ($other (m:err (pyrt:exc "TypeError" (str:concat (pyrt:quoted (pyrt:type-name $x)) " object does not support item assignment")))))))
; l[i:j:k] = v (list_ass_subscript): the bounds read first, then v's items
; (the list's own, copied, when v is the list), then the slice placed in the
; list as it is then: a simple slice's items replaced by them, an extended
; slice's assigned one by one when the counts agree
(= (pyrt:list-assign-slice $l $sl $v)
   (case (pyrt:slice-unpack $sl)
     (((m:val $u)
       (case (pyrt:assigned-items $l $v)
         (((m:val $vs)
           (let (sl $a $z $st $k) (pyrt:slice-adjust $u (pyrt:list-len $l))
             (if (== $st 1)
                 (let $r (pyrt:list-splice $l $a (if (< $z $a) $a $z) $vs) (m:val (py:None)))
                 (if (== (size-atom $vs) $k)
                     (let $r (pyrt:list-set-every $l $a $st $vs) (m:val (py:None)))
                     (m:err (pyrt:exc "ValueError"
                              (str:concat (str:concat (str:concat "attempt to assign sequence of size " (str:number->string (size-atom $vs)))
                                                      " to extended slice of size ")
                                          (str:number->string $k))))))))
          ($e $e))))
      ($e $e))))
; PySequence_Fast(v, "must assign iterable to extended slice"): the list's
; items when v is the list; v's items, a refusal to iterate it replaced by
; that message (an error while iterating passes on)
(= (pyrt:assigned-items $l $v)
   (if (== $l $v) (m:val (pyrt:list-items $l))
     (if (pyrt:is-iterable $v) (pyrt:elements $v)
       (if (pyrt:needs-python $v)
           (case (pyrt:now (pyrt:iter $v))
             (((m:val $it) (pyrt:drain $it ()))
              ((m:err $err) (if (pyrt:exc-is (pyrt:exc-instance $err) TypeError) (pyrt:must-assign-iterable) (m:err $err)))))
           (pyrt:must-assign-iterable)))))
(= (pyrt:must-assign-iterable) (m:err (pyrt:exc "TypeError" "must assign iterable to extended slice")))
; the list's items from A up to Z replaced by VS (list_ass_slice): the items
; before A stay; as many as there were, assigned in place
(= (pyrt:list-splice $l $a $z $vs)
   (if (== (- $z $a) (size-atom $vs)) (pyrt:list-set-every $l $a 1 $vs)
     (let $n (pyrt:list-len $l)
       (pyrt:list-refill-from $l $a $n (pyrt:append $vs (pyrt:list-items-from $l $z $n))))))
; the list's items from A on made XS
(= (pyrt:list-refill-from (py:list $id) $a $n $xs)
   (let $c (pyrt:list-clear-from $id $a $n)
     (let $f (pyrt:list-fill $id $a $xs)
       (pyrt:heap-replace (h:len $id $n) (h:len $id (+ $a (size-atom $xs)))))))
(= (pyrt:list-set-every $l $i $st $vs)
   (if (== (size-atom $vs) 0) ()
     (let ($v $rest) (decons-atom $vs)
       (let $u (pyrt:list-set $l $i $v) (pyrt:list-set-every $l (+ $i $st) $st $rest)))))
; del l[i:j:k] (list_ass_subscript): a simple slice's items removed, an
; extended slice's (its K items, from the lowest, every |ST|-th)
(= (pyrt:list-delete-slice $l $k)
   (case (pyrt:slice-indices $k (pyrt:list-len $l))
     (((m:val (sl $a $z $st $n))
       (let $r (if (== $st 1) (pyrt:list-splice $l $a (if (< $z $a) $a $z) ())
                 (if (> $n 0)
                     (let $lo (if (< $st 0) (+ $a (* (- $n 1) $st)) $a)
                       (let $len (pyrt:list-len $l)
                         (pyrt:list-refill-from $l $lo $len
                           (pyrt:items-skipping (pyrt:list-items-from $l $lo $len) 0 (if (< $st 0) (- 0 $st) $st) $n))))
                     ()))
         (m:val (py:None))))
      ($e $e))))
; XS but its items at 0, ST, 2*ST, ... (K of them)
(= (pyrt:items-skipping $xs $i $st $k)
   (if (== (size-atom $xs) 0) ()
     (let ($x $rest) (decons-atom $xs)
       (if (if (== (% $i $st) 0) (< $i (* $st $k)) false)
           (pyrt:items-skipping $rest (+ $i 1) $st $k)
           (cons-atom $x (pyrt:items-skipping $rest (+ $i 1) $st $k))))))
; list.insert's index (list_insert_impl): counted from the end when negative,
; then held within 0 and the length
(= (pyrt:insert-index $j $n)
   (if (< $j 0) (let $k (+ $j $n) (if (< $k 0) 0 $k)) (if (> $j $n) $n $j)))

; ---------------------------------------------------------------- repr

(= (pyrt:repr $x)
   (if (pyrt:is-str $x) (pyrt:str-repr $x)
     (case $x
       (((py:list $id) (str:concat (str:concat "[" (pyrt:join-repr (pyrt:list-items $x))) "]"))
        ((py:dict $id) (str:concat (str:concat "{" (pyrt:join-pairs (pyrt:dict-entries $x))) "}"))
        ((py:set $id) (if (== (pyrt:set-len $x) 0) "set()" (str:concat (str:concat "{" (pyrt:join-repr (pyrt:set-items $x))) "}")))
        ((py:frozenset $id) (if (== (pyrt:set-len $x) 0) "frozenset()" (str:concat (str:concat "frozenset({" (pyrt:join-repr (pyrt:set-items $x))) "})")))
        ((py:range $a $b $s) (str:concat (str:concat (str:concat (str:concat "range(" (str:number->string $a)) ", ") (str:number->string $b))
                                          (if (== $s 1) ")" (str:concat (str:concat ", " (str:number->string $s)) ")"))))
        ((py:view keys $d) (str:concat (str:concat "dict_keys([" (pyrt:join-repr (pyrt:dict-keys $d))) "])"))
        ((py:view values $d) (str:concat (str:concat "dict_values([" (pyrt:join-repr (pyrt:dict-values $d))) "])"))
        ((py:view items $d) (str:concat (str:concat "dict_items([" (pyrt:join-repr (pyrt:pairs-as-tuples (pyrt:dict-entries $d)))) "])"))
        ((py:obj $id) (if (pyrt:is-exc-instance $x) (pyrt:exc-repr $x) (pyrt:scalar-str $x)))
        ((py:gen $id) (str:concat (str:concat (str:concat "<generator object " (pyrt:field $x qualname)) " at ") (str:concat (pyrt:address $x) ">")))
        ((py:bmeth $bt $bn) (pyrt:bmeth-repr $bt $bn))
        ((py:Bytes $b) (pyrt:bytes-repr $b))
        ((py:slice $sa $sb $sc) (str:concat (str:concat "slice(" (pyrt:join-repr ($sa $sb $sc))) ")"))
        ((py:bmeth-bound $bt $bn $bs) (pyrt:bmeth-bound-repr $bt $bn $bs))
        ((py:coro $id) (str:concat (str:concat (str:concat "<coroutine object " (pyrt:field $x qualname)) " at ") (str:concat (pyrt:address $x) ">")))
        ((py:agen $id) (str:concat (str:concat (str:concat "<async_generator object " (pyrt:field $x qualname)) " at ") (str:concat (pyrt:address $x) ">")))
        ($other
         (if (pyrt:is-tuple $x)
             (if (== (size-atom $x) 2) (str:concat (str:concat "(" (pyrt:repr (index-atom $x 1))) ",)")
               (str:concat (str:concat "(" (pyrt:join-repr (cdr-atom $x))) ")"))
             (pyrt:scalar-str $x)))))))
(= (pyrt:join-repr $xs)
   (if (== $xs ()) ""
     (let ($x $rest) (decons-atom $xs)
       (if (== $rest ()) (pyrt:repr $x) (str:concat (str:concat (pyrt:repr $x) ", ") (pyrt:join-repr $rest))))))
(= (pyrt:join-pairs $ps)
   (if (== $ps ()) ""
     (let ((py:pair $k $v) $rest) (decons-atom $ps)
       (let $one (str:concat (str:concat (pyrt:repr $k) ": ") (pyrt:repr $v))
         (if (== $rest ()) $one (str:concat (str:concat $one ", ") (pyrt:join-pairs $rest)))))))
; a str's repr: single quotes unless the text has a single quote and no
; double one; backslash, the quote and control characters escaped
(= (pyrt:str-repr $s)
   (let $cps (str:codepoint-values $s)
     (let $q (if (pyrt:has-cp $cps 39) (if (pyrt:has-cp $cps 34) 39 34) 39)
       (str:concat (str:concat (str:from-codepoints (cons-atom $q ())) (pyrt:escape-cps $cps $q)) (str:from-codepoints (cons-atom $q ()))))))
; whether a value is among a list's (one answer: is-member answers once per
; occurrence)
(= (pyrt:has-cp $cps $c)
   (if (== (size-atom $cps) 0) false
     (let ($x $rest) (decons-atom $cps) (if (== $x $c) true (pyrt:has-cp $rest $c)))))
(= (pyrt:escape-cps $cps $q)
   (if (== (size-atom $cps) 0) ""
     (let ($c $rest) (decons-atom $cps) (str:concat (pyrt:escape-cp $c $q) (pyrt:escape-cps $rest $q)))))
(= (pyrt:escape-cp $c $q)
   (if (== $c 92) "\\\\"
     (if (== $c $q) (str:concat "\\" (str:from-codepoints (cons-atom $c ())))
       (if (== $c 10) "\\n"
         (if (== $c 13) "\\r"
           (if (== $c 9) "\\t"
             (if (< $c 32) (pyrt:hex-escape $c)
               (if (< $c 127) (str:from-codepoints (cons-atom $c ()))
                 (if (== $c 127) (pyrt:hex-escape $c)
                   (if (pyrt:printable-cp $c) (str:from-codepoints-generalized (cons-atom $c ()))
                     (pyrt:wide-escape $c)))))))))))
; unicode_repr: a character Py_UNICODE_ISPRINTABLE refuses (a separator or an
; "other" character but the space) as \xHH, \uHHHH or \UHHHHHHHH
(= (pyrt:printable-cp $c) (== (integer:and (str:code-props $c) 1069547520) 0))
(= (pyrt:wide-escape $c)
   (if (<= $c 255) (pyrt:hex-escape $c)
     (if (<= $c 65535) (str:concat "\\u" (pyrt:hex-n $c 4)) (str:concat "\\U" (pyrt:hex-n $c 8)))))
(= (pyrt:hex-n $c $n)
   (if (== $n 0) "" (str:concat (pyrt:hex-n (/ (- $c (% $c 16)) 16) (- $n 1)) (pyrt:hex-digit (% $c 16)))))
(= (pyrt:hex-escape $c) (str:concat "\\x" (str:concat (pyrt:hex-digit (/ (- $c (% $c 16)) 16)) (pyrt:hex-digit (% $c 16)))))
(= (pyrt:hex-digit $d) (str:from-codepoints (cons-atom (if (< $d 10) (+ 48 $d) (+ 87 $d)) ())))

; ---------------------------------------------------------------- bytes
; A bytes value is (py:Bytes VIEW): VIEW a str of the code points 0 to 255,
; one for each byte (the Latin-1 reading of the bytes), so that finding,
; splitting, slicing and comparing are the str operations, and the bytes
; are read only where they leave it (decoding, files)

; the bytes a bytes-like value holds, for the readers of buffers: a bytes
; value's; an instance's of a subclass of bytes, its builtin part's; a
; bytearray's, its items (ints from 0 to 255) as a text; else missing
(= (pyrt:bytes-view-of $x)
   (case $x
     (((py:Bytes $v) $v)
      ((py:obj $oid) (pyrt:object-bytes-view $x))
      ($o (m:missing)))))
(= (pyrt:object-bytes-view $x)
   (let $data (pyrt:field $x data)
     (if (if (== $data (m:missing)) false (pyrt:instance-of-builtin $x bytearray))
         (str:from-codepoints (pyrt:list-items $data))
         (case (pyrt:field $x value) (((py:Bytes $w) $w) ($none (m:missing)))))))
; b[i] an int, b[i:j:k] a bytes value
(= (pyrt:bytes-getitem $b $i)
   (if (pyrt:is-slice $i)
       (case (pyrt:slice-indices $i (str:codepoint-count $b))
         (((m:val $sl) (m:val (py:Bytes (str:join "" (pyrt:slice-take (pyrt:chars $b) $sl))))) ($e $e)))
       (case (pyrt:seq-index $i (str:codepoint-count $b) "index out of range" "byte")
         (((m:val $j) (m:val (index-atom (pyrt:seq-copy (str:codepoint-values $b)) $j))) ($e $e)))))
; bytes_repr: b'...' (b"..." when the data has a ' and no "), the quote and
; the backslash escaped, \t \n \r, any other byte below 0x20 or from 0x7f
; as \xhh
(= (pyrt:bytes-repr $b)
   (let $cps (str:codepoint-values $b)
     (let $q (if (pyrt:has-cp $cps 39) (if (pyrt:has-cp $cps 34) 39 34) 39)
       (let $quote (str:from-codepoints (cons-atom $q ()))
         (str:concat (str:concat (str:concat "b" $quote) (pyrt:bytes-escape $cps $q)) $quote)))))
(= (pyrt:bytes-escape $cps $q)
   (if (== (size-atom $cps) 0) ""
     (let ($c $rest) (decons-atom $cps) (str:concat (pyrt:bytes-escape-one $c $q) (pyrt:bytes-escape $rest $q)))))
(= (pyrt:bytes-escape-one $c $q)
   (if (== $c 92) "\\\\"
     (if (== $c $q) (str:concat "\\" (str:from-codepoints (cons-atom $c ())))
       (if (== $c 9) "\\t"
         (if (== $c 10) "\\n"
           (if (== $c 13) "\\r"
             (if (if (< $c 32) true (>= $c 127)) (pyrt:hex-escape $c)
               (str:from-codepoints (cons-atom $c ())))))))))

; ---------------------------------------------------------------- built-in functions

(= (pyrt:is-builtin-name $n)
   (case $n
     (("print" true) ("len" true) ("range" true) ("list" true) ("tuple" true) ("dict" true) ("set" true)
      ("str" true) ("repr" true) ("int" true) ("abs" true) ("sum" true) ("min" true) ("max" true)
      ("sorted" true) ("reversed" true) ("enumerate" true) ("zip" true) ("iter" true) ("next" true)
      ("bool" true) ("any" true) ("all" true) ("map" true) ("filter" true) ("isinstance" true) ("issubclass" true)
      ("round" true) ("divmod" true) ("hash" true) ("globals" true) ("locals" true) ("vars" true)
      ("dir" true) ("exec" true) ("eval" true) ("chr" true) ("ord" true) ($other false))))
; chr(i): the one-character string of a code point (surrogates too: Python's
; str); ord(c): the code point of a one-character string
(= (pyrt:chr $x)
   (if (pyrt:intish $x)
       (let $n (pyrt:int-of $x)
         (if (if (>= $n 0) (<= $n 1114111) false)
             (m:val (str:from-codepoints-generalized (cons-atom $n ())))
             (m:err (pyrt:exc "ValueError" "chr() arg not in range(0x110000)"))))
       (m:err (pyrt:exc "TypeError" (str:concat (pyrt:quoted (pyrt:type-name $x)) " object cannot be interpreted as an integer")))))
(= (pyrt:ord $x)
   (if (pyrt:is-str $x)
       (let $n (str:codepoint-count $x)
         (if (== $n 1) (m:val (car-atom (str:codepoint-values $x)))
             (m:err (pyrt:exc "TypeError" (str:concat (str:concat "ord() expected a character, but string of length " (str:number->string $n)) " found")))))
       (m:err (pyrt:exc "TypeError" (str:concat (str:concat "ord() expected string of length 1, but " (pyrt:type-name $x)) " found")))))
; a builtin name: a builtin type (py:type NAME), a builtin constant, a
; builtin function, else one of the prelude's
(= (pyrt:builtin-of $n)
   (let $t (pyrt:builtin-type-name $n)
     (if (== $t (m:missing))
         (if (== $n "NotImplemented") (py:NotImplemented)
           (if (== $n "Ellipsis") (py:Ellipsis)
           (if (== $n "True") True
           (if (== $n "False") False
           (if (== $n "None") (py:None)
           (if (== $n "__debug__") True
             (if (pyrt:is-builtin-name $n) (py:builtin (str:string->symbol $n))
               ; the runtime's intrinsics, for the modules written in Python it holds
               (if (str:starts-with $n "_cetta_") (py:builtin (str:string->symbol $n)) (pyrt:prelude-export $n)))))))))
         (py:type $t))))
(= (pyrt:kw-get $kws $name $default)
   (if (== $kws ()) $default
     (let ((py:pair $n $v) $rest) (decons-atom $kws) (if (== $n $name) $v (pyrt:kw-get $rest $name $default)))))

; float(x): a float as it is, an int or bool the nearest float, a str read
; as a float's literal (whitespace around it; underscores only between
; digits); anything else is a TypeError
; float(x) (PyNumber_Float): a number, a str's text; an object by its
; __float__ or __index__, or as a str subclass's text (the prelude's)
(= (pyrt:to-float $x)
   (if (pyrt:is-number $x) (pyrt:as-float $x)
     (if (pyrt:is-str $x) (pyrt:float-of-str $x)
       (if (pyrt:needs-python $x) (pyrt:prelude-call "_number_float" (cons-atom $x ()))
         (m:err (pyrt:exc "TypeError" (str:concat (str:concat "float() argument must be a string or a real number, not " (pyrt:quoted (pyrt:type-name $x))) "")))))))
(= (pyrt:float-of-str $s)
   (let $t (str:trim $s)
     (if (pyrt:underscores-between-digits (str:codepoint-values $t) 0)
         (case (float:parse (str:join "" (str:split "_" $t)))
           (((Error $call $why) (m:err (pyrt:exc "ValueError" (str:concat "could not convert string to float: " (pyrt:repr $s)))))
            ($f (m:val $f))))
         (m:err (pyrt:exc "ValueError" (str:concat "could not convert string to float: " (pyrt:repr $s)))))))
; whether each underscore stands between two ASCII digits (PEP 515)
(= (pyrt:underscores-between-digits $cps $prev)
   (if (== (size-atom $cps) 0) true
     (let ($c $rest) (decons-atom $cps)
       (if (== $c 95)
           (if (pyrt:ascii-digit $prev)
               (if (== (size-atom $rest) 0) false (if (pyrt:ascii-digit (car-atom $rest)) (pyrt:underscores-between-digits $rest $c) false))
               false)
           (pyrt:underscores-between-digits $rest $c)))))
(= (pyrt:ascii-digit $c) (if (>= $c 48) (<= $c 57) false))
; round(x) and round(x, n): CPython's.  An int stays an int, or is rounded
; half to even at a power of ten; a float without n is rounded half to even
; to an int; a float with n is rounded correctly at that decimal place
; (C's printf), returned as is for n above 323 and as a zero for n below -308
; round(number, ndigits=None) (builtin_round_impl): an int's and a float's
; here; an object's type(number).__round__ (the prelude's _round)
(= (pyrt:round $x $n)
   (if (pyrt:needs-python $x) (pyrt:prelude-call "_round" (cons-atom $x (cons-atom $n ())))
   (if (pyrt:is-float $x)
       (if (== $n (py:None)) (pyrt:float-round-int $x)
         (if (pyrt:intish $n) (m:val (pyrt:float-round-digits $x (pyrt:int-of $n)))
           (m:err (pyrt:exc "TypeError" (str:concat (pyrt:quoted (pyrt:type-name $n)) " object cannot be interpreted as an integer")))))
       (if (pyrt:intish $x)
           (if (== $n (py:None)) (m:val (pyrt:int-of $x))
             (if (pyrt:intish $n) (m:val (pyrt:int-round (pyrt:int-of $x) (pyrt:int-of $n)))
               (m:err (pyrt:exc "TypeError" (str:concat (pyrt:quoted (pyrt:type-name $n)) " object cannot be interpreted as an integer")))))
           (m:err (pyrt:exc "TypeError" (str:concat (str:concat "type " (pyrt:type-name $x)) " doesn't define __round__ method")))))))
(= (pyrt:float-round-int $x)
   (if (float:is-nan $x) (m:err (pyrt:exc "ValueError" "cannot convert float NaN to integer"))
     (if (float:is-inf $x) (m:err (pyrt:exc "OverflowError" "cannot convert float infinity to integer"))
       (let $f (float:floor $x)
         (let $c (float:compare (float:sub $x $f) 0.5)
           (let $r (if (== $c 1) (float:add $f 1.0)
                     (if (== $c -1) $f
                       (if (pyrt:float-zero (float:fmod $f 2.0)) $f (float:add $f 1.0))))
             (m:val (float:to-int $r))))))))
(= (pyrt:float-round-digits $x $n)
   (if (if (float:is-finite $x) (<= $n 323) false)
       (if (< $n -308) (float:copysign 0.0 $x)
         (if (>= $n 0) (float:parse (float:format $x f $n))
           (pyrt:float-round-left $x $n)))
       $x))
; rounding left of the point: S significant digits kept; with none kept, the
; result is a zero or the next power of ten, a tie going to the zero
(= (pyrt:float-round-left $x $n)
   (if (pyrt:float-zero $x) $x
     (case (float:digits $x)
       (((FloatDigits $sign $ds $decpt)
         (let $s (+ $decpt $n)
           (if (> $s 0) (float:parse (float:format $x e (- $s 1)))
             (if (< $s 0) (float:copysign 0.0 $x)
               (if (if (>= (car-atom (str:codepoint-values $ds)) 53) (!= $ds "5") false)
                   (float:copysign (float:parse (str:concat "1e" (str:number->string (- 0 $n)))) $x)
                   (float:copysign 0.0 $x))))))))))
(= (pyrt:int-round $x $n)
   (if (>= $n 0) $x
     (let $p (pyrt:int-pow 10 (- 0 $n) 1)
       (let $r (% $x $p)
         (let $q (- $x $r)
           (let $twice (* 2 $r)
             (if (> $twice $p) (+ $q $p)
               (if (< $twice $p) $q
                 (if (== (% (/ $q $p) 2) 0) $q (+ $q $p))))))))))
; divmod(x, y): (x // y, x % y)
(= (pyrt:divmod $x $y)
   (case (pyrt:now (pyrt:binop (py:FloorDiv) $x $y))
     (((m:val $q) (case (pyrt:now (pyrt:binop (py:Mod) $x $y)) (((m:val $r) (m:val (py:tuple $q $r))) ($e $e))))
      ($e $e))))

; next(it) of a program's iterator: its __next__ (Python, from the host);
; StopIteration ends it
(= (pyrt:python-next $it)
   (case (pyrt:prelude-sync "_next" (cons-atom $it ()))
     (((m:val $v) (m:val $v))
      ((m:err $e) (if (pyrt:exc-is (pyrt:exc-instance $e) StopIteration) (m:val (m:exhausted)) (m:err $e))))))
; a program's object as a dict key: unhashable when its class's __hash__ is
; None; itself (by identity) under object's hash; else hashed by its
; __hash__ (equal keys with equal hashes are not yet resolved to one entry)
; an object's key: its identity under object's __hash__; its builtin part's
; key when its class keeps a builtin type's __hash__ and __eq__ (a str
; subclass's instance hashes and compares as its str); else its __hash__
(= (pyrt:object-key $k)
   (let $t (pyrt:type-of $k)
     (let $h (pyrt:mro-lookup $t "__hash__")
       (if (== $h (py:None)) (pyrt:unhashable $k)
         (if (== $h (pyrt:mro-lookup (py:type object) "__hash__")) (m:val $k)
           (if (pyrt:builtin-hash-eq $h (pyrt:mro-lookup $t "__eq__")) (pyrt:key (pyrt:base-of $k))
             (case (pyrt:prelude-sync "_hash" (cons-atom $k ())) (((m:val $n) (m:val (py:hkey $n $k))) ($e $e)))))))))
(= (pyrt:builtin-hash-eq $h $e)
   (case $h
     (((py:bmeth $t "__hash__") (case $e (((py:bmeth $t2 "__eq__") true) ($o false))))
      ($o false))))
; hash(x): a program's object's __hash__; an int's (CPython's numeric hash,
; modulo 2**61 - 1, -1 being -2)
(= (pyrt:hash $x)
   (if (pyrt:needs-python $x) (pyrt:prelude-call "_hash" (cons-atom $x ()))
     (case (pyrt:key $x) (((m:val $k) (m:val (pyrt:key-hash $k $x))) ($e $e)))))
(= (pyrt:int-hash $n)
   (let $p 2305843009213693951
     (let $h (if (< $n 0) (- 0 (% (- 0 $n) $p)) (% $n $p))
       (if (== $h -1) -2 $h))))

; any (STOP true) and all (STOP false): the first item whose truth is STOP
; decides, each item's truth taken in turn (__bool__ or __len__ for objects)
(= (pyrt:truth-fold $es $stop)
   (if (== (size-atom $es) 0) (m:val (if $stop False True))
     (let ($e $rest) (decons-atom $es)
       (case (pyrt:truth-of $e)
         (((m:val $b) (if (== (if $b true false) $stop) (m:val (if $stop True False)) (pyrt:truth-fold $rest $stop)))
          ($err $err))))))

; one equation per name: a call takes exactly one way
(= (pyrt:call-builtin-kw $b $xs $kws)
   (if (pyrt:is-intrinsic $b) (pyrt:intrinsic $b $xs)
   (if (== $b dict) (pyrt:dict-call $xs $kws)
   (if (== $b print) (pyrt:print $xs $kws)
     (if (== $b sorted) (pyrt:sorted-call $xs $kws)
       (if (== $b min) (pyrt:extreme-call $xs $kws (py:Lt) "min")
         (if (== $b max) (pyrt:extreme-call $xs $kws (py:Gt) "max")
           (if (== $kws ()) (pyrt:call-builtin-pos $b $xs)
             (if (== $b int) (pyrt:call-kw (pyrt:prelude "_int_new") $xs $kws) (pyrt:call-builtin-other $b $xs $kws))))))))))
; a builtin with no case of its own: a type whose instances calling it does
; not make is refused (type_call, tp_new NULL); a type whose __new__ the
; prelude writes (a struct sequence's) is called as type.__call__ calls it;
; else the arguments are wrong for the builtin
(= (pyrt:call-builtin-other $b $xs $ks)
   (if (pyrt:no-instances $b)
       (m:err (pyrt:exc "TypeError" (str:concat (str:concat "cannot create '" (pyrt:tp-name-of (py:type $b))) "' instances")))
       (if (== (pyrt:bmeth-impl $b "__new__") (m:missing))
           (if (== $ks ()) (pyrt:arg-count-error $b 1 (size-atom $xs)) (pyrt:no-keywords $b))
           (pyrt:call-class (py:type $b) $xs $ks))))
; dict(), dict(mapping or pairs), and keyword arguments added after
(= (pyrt:dict-call $xs $kws)
   (let $d (pyrt:dict-new)
     (case (if (== (size-atom $xs) 0) (m:val (py:None))
             (if (== (size-atom $xs) 1) (pyrt:dict-fill-from $d (car-atom $xs))
               (m:err (pyrt:exc "TypeError" (str:concat "dict expected at most 1 argument, got " (str:number->string (size-atom $xs)))))))
       (((m:val $u) (case (pyrt:fill-dict $d $kws) (((m:val $d2) (m:val $d)) ($e $e))))
        ($e $e)))))
; dict.update(arg=None, /, **kwds) (dict_update_common): a mapping or an
; iterable of pairs, then the keywords
(= (pyrt:dict-update $d $xs $ks) (pyrt:dict-update-as $d $xs $ks "update"))
(= (pyrt:dict-update-as $d $xs $ks $name)
   (if (> (size-atom $xs) 1)
       (m:err (pyrt:exc "TypeError" (str:concat (str:concat $name " expected at most 1 argument, got ") (str:number->string (size-atom $xs)))))
       (case (if (== $xs ()) (m:val (py:None)) (pyrt:dict-fill-from $d (car-atom $xs)))
         (((m:val $u) (case (pyrt:fill-dict $d $ks) (((m:val $d2) (m:val (py:None))) ($e $e))))
          ($e $e)))))
; dict.popitem(): the entry inserted last, removed; KeyError when empty
(= (pyrt:dict-popitem (py:dict $id))
   (let $es (sort-atom (collapse (match &py-heap (h:dent $id $k $o $v $s) (e $s $k $o $v))))
     (if (== (size-atom $es) 0) (m:err (pyrt:exc-args "KeyError" (cons-atom "popitem(): dictionary is empty" ())))
       (let (e $s $k $o $v) (index-atom $es (- (size-atom $es) 1))
         (let $r (pyrt:heap-remove (h:dent $id $k $o $v $s)) (m:val (py:tuple $o $v)))))))
; the argument of dict(x) or d.update(x) (dict_update_arg): a dict's entries;
; a program's object as a mapping when it has keys(), else as pairs (the
; prelude's); any other iterable as pairs
(= (pyrt:dict-fill-from $d $x)
   (case $x
     (((py:dict $id) (case (pyrt:fill-dict $d (pyrt:dict-entries $x)) (((m:val $d2) (m:val (py:None))) ($e $e))))
      ((py:obj $id) (pyrt:prelude-sync "_dict_update_arg" (cons-atom $d (cons-atom $x ()))))
      ($other (case (pyrt:elements $x)
                (((m:val $es) (case (pyrt:pairs-of-items $es ()) (((m:val $ps) (case (pyrt:fill-dict $d $ps) (((m:val $d2) (m:val (py:None))) ($e $e)))) ($e $e))))
                 ($e $e)))))))
(= (pyrt:pairs-of-items $es $acc)
   (if (== (size-atom $es) 0) (m:val (pyrt:rev-onto $acc ()))
     (let ($e $rest) (decons-atom $es)
       (case (pyrt:seq2-element $e (size-atom $acc))
         (((m:val $kv) (if (== (size-atom $kv) 2) (pyrt:pairs-of-items $rest (cons-atom (py:pair (car-atom $kv) (index-atom $kv 1)) $acc))
                         (m:err (pyrt:exc "ValueError" (str:concat (str:concat (str:concat "dictionary update sequence element #" (str:number->string (size-atom $acc))) " has length ") (str:concat (str:number->string (size-atom $kv)) "; 2 is required"))))))
          ($e2 $e2))))))
; a dict update sequence's element as its items (PySequence_Fast): a list's
; or tuple's own, any other through the prelude's _dict_seq2_element (an
; element that is not iterable a TypeError with a note naming it)
(= (pyrt:seq2-element $e $i)
   (case $e
     (((py:list $id) (m:val (pyrt:list-items $e)))
      ($other (if (pyrt:is-tuple $e) (m:val (cdr-atom $e))
                (case (pyrt:prelude-sync "_dict_seq2_element" (cons-atom $e (cons-atom $i ())))
                  (((m:val $l) (m:val (pyrt:list-items $l))) ($err $err))))))))
; sorted(iterable, /, *, key=None, reverse=False): its arity, then the items
; into a new list, then list.sort's keywords
(= (pyrt:sorted-call $xs $kws)
   (if (== (size-atom $xs) 1)
       (case (pyrt:elements (car-atom $xs))
         (((m:val $es) (case (pyrt:sort-options $kws (so (py:None) False))
                         (((so $key $rev) (case (pyrt:sort-values $es $key $rev) (((m:val $s) (m:val (pyrt:list-new $s))) ($e $e))))
                          ($e $e))))
          ($e $e)))
       (m:err (pyrt:exc "TypeError" (str:concat "sorted expected 1 argument, got " (str:number->string (size-atom $xs)))))))
; list.sort's keywords: key (None for none) and reverse (an int's truth)
(= (pyrt:sort-options $kws $acc)
   (if (== (size-atom $kws) 0) $acc
     (let ((py:pair $name $v) $rest) (decons-atom $kws)
       (let (so $key $rev) $acc
         (if (== $name "key") (pyrt:sort-options $rest (so $v $rev))
           (if (== $name "reverse")
               (if (pyrt:intish $v) (pyrt:sort-options $rest (so $key (!= (pyrt:int-of $v) 0)))
                   (m:err (pyrt:exc "TypeError" (str:concat (pyrt:quoted (pyrt:type-name $v)) " object cannot be interpreted as an integer"))))
               (m:err (pyrt:exc "TypeError" (str:concat (str:concat "sort() got an unexpected keyword argument '" $name) "'")))))))))
; min and max: one iterable, or two or more arguments; key=, and default= with
; an iterable; the first of equal items wins (CPython's min_max)
(= (pyrt:extreme-call $xs $kws $o $what)
   (let $n (size-atom $xs)
     (if (== $n 0) (m:err (pyrt:exc "TypeError" (str:concat $what " expected at least 1 argument, got 0")))
       (case (pyrt:extreme-options $kws $what (xo (py:None) (m:missing)))
         (((xo $key $dflt)
           (if (== $n 1)
               (case (pyrt:elements (car-atom $xs))
                 (((m:val $es)
                   (if (== (size-atom $es) 0)
                       (if (== $dflt (m:missing)) (m:err (pyrt:exc "ValueError" (str:concat $what "() iterable argument is empty"))) (m:val $dflt))
                       (pyrt:extreme-keyed $es $key $o)))
                  ($e $e)))
               (if (== $dflt (m:missing)) (pyrt:extreme-keyed $xs $key $o)
                   (m:err (pyrt:exc "TypeError" (str:concat (str:concat "Cannot specify a default for " $what) "() with multiple positional arguments"))))))
          ($e $e))))))
(= (pyrt:extreme-options $kws $what $acc)
   (if (== (size-atom $kws) 0) $acc
     (let ((py:pair $name $v) $rest) (decons-atom $kws)
       (let (xo $key $dflt) $acc
         (if (== $name "key") (pyrt:extreme-options $rest $what (xo $v $dflt))
           (if (== $name "default") (pyrt:extreme-options $rest $what (xo $key $v))
               (m:err (pyrt:exc "TypeError" (str:concat (str:concat (str:concat $what "() got an unexpected keyword argument '") $name) "'")))))))))
(= (pyrt:extreme-keyed $es $key $o)
   (let ($x $rest) (decons-atom $es)
     (case (pyrt:key-of $key $x)
       (((m:val $k) (pyrt:extreme-keyed-from $rest $key $o $x $k)) ($e $e)))))
(= (pyrt:extreme-keyed-from $es $key $o $best $bk)
   (if (== (size-atom $es) 0) (m:val $best)
     (let ($x $rest) (decons-atom $es)
       (case (pyrt:key-of $key $x)
         (((m:val $k) (case (pyrt:now (pyrt:compare $o $k $bk))
                        (((m:val $better) (if $better (pyrt:extreme-keyed-from $rest $key $o $x $k) (pyrt:extreme-keyed-from $rest $key $o $best $bk)))
                         ($e $e))))
          ($e $e))))))
; an item's key: the item without a key function, else the key function's
; value, called from the host
(= (pyrt:key-of $key $x) (if (== $key (py:None)) (m:val $x) (pyrt:call-sync $key (cons-atom $x ()) ())))
; list.sort: every key first, in order; then a stable merge sort of the
; (key, item) pairs under Python's <; reverse=True reverses before and after
; a stable ascending sort, so items with equal keys keep their order
(= (pyrt:sort-values $es $key $rev)
   (case (pyrt:keys-of $es $key ())
     (((m:val $ps)
       (case (pyrt:sort-pairs (if $rev $ps (pyrt:rev-onto $ps ())))
         (((m:val $s) (m:val (pyrt:items-of-pairs (if $rev (pyrt:rev-onto $s ()) $s) ()))) ($e $e))))
      ($e $e))))
; the (key, item) pairs, collected in reverse
(= (pyrt:keys-of $es $key $acc)
   (if (== (size-atom $es) 0) (m:val $acc)
     (let ($x $rest) (decons-atom $es)
       (case (pyrt:key-of $key $x) (((m:val $k) (pyrt:keys-of $rest $key (cons-atom (sp $k $x) $acc))) ($e $e))))))
(= (pyrt:items-of-pairs $ps $acc)
   (if (== (size-atom $ps) 0) (pyrt:rev-onto $acc ())
     (let ((sp $k $x) $rest) (decons-atom $ps) (pyrt:items-of-pairs $rest (cons-atom $x $acc)))))
(= (pyrt:sort-pairs $ps)
   (if (<= (size-atom $ps) 1) (m:val $ps)
     (let $h (/ (- (size-atom $ps) (% (size-atom $ps) 2)) 2)
       (case (pyrt:sort-pairs (pyrt:take $ps $h))
         (((m:val $a) (case (pyrt:sort-pairs (pyrt:drop $ps $h))
                        (((m:val $b) (pyrt:merge-pairs $a $b ())) ($e $e))))
          ($e $e))))))
; the later pair goes first only when its key is less: stable
(= (pyrt:merge-pairs $a $b $acc)
   (if (== (size-atom $a) 0) (m:val (pyrt:rev-onto $acc $b))
     (if (== (size-atom $b) 0) (m:val (pyrt:rev-onto $acc $a))
       (let ((sp $ka $x) $ra) (decons-atom $a)
         (let ((sp $kb $y) $rb) (decons-atom $b)
           (case (pyrt:now (pyrt:compare (py:Lt) $kb $ka))
             (((m:val $lt) (if $lt (pyrt:merge-pairs $a $rb (cons-atom (sp $kb $y) $acc)) (pyrt:merge-pairs $ra $b (cons-atom (sp $ka $x) $acc))))
              ($e $e))))))))
; list.sort(*, key=None, reverse=False), in place
(= (pyrt:list-sort $l $xs $ks)
   (if (> (size-atom $xs) 0) (m:err (pyrt:exc "TypeError" "sort() takes no positional arguments"))
     (case (pyrt:sort-options $ks (so (py:None) False))
       (((so $key $rev) (case (pyrt:sort-values (pyrt:list-items $l) $key $rev)
                          (((m:val $s) (let $r (pyrt:list-reset $l $s) (m:val (py:None)))) ($e $e))))
        ($e $e)))))
; a built-in method called with keyword arguments: list.sort takes them; any
; other refuses them, in CPython's words
(= (pyrt:call-bound-kw $o $name $xs $ks)
   (if (== (size-atom $ks) 0) (pyrt:call-bound $o $name $xs)
     (if (if (== (pyrt:kind-of $o) list) (== $name "sort") false)
         (pyrt:list-sort $o $xs $ks)
       (if (if (== (pyrt:kind-of $o) dict) (== $name "update") false)
         (pyrt:dict-update $o $xs $ks)
         (pyrt:call-type-method $o $name $xs $ks)))))
; map(func, *iterables) and filter(function or None, iterable): lazy
; iterators that call the function from the host at each next
(= (pyrt:map-new $f $its)
   (case (pyrt:iters-of $its ())
     (((m:val $is) (pyrt:iter-new (it:map $f $is))) ($e $e))))
(= (pyrt:iters-of $its $acc)
   (if (== (size-atom $its) 0) (m:val (pyrt:rev-onto $acc ()))
     (let ($x $rest) (decons-atom $its)
       (case (pyrt:now (pyrt:iter $x)) (((m:val $i) (pyrt:iters-of $rest (cons-atom $i $acc))) ($e $e))))))
(= (pyrt:nexts-of $is $acc)
   (if (== (size-atom $is) 0) (m:val (pyrt:rev-onto $acc ()))
     (let ($i $rest) (decons-atom $is)
       (case (pyrt:next-of $i)
         (((m:val (m:exhausted)) (m:val (m:exhausted)))
          ((m:val $x) (pyrt:nexts-of $rest (cons-atom $x $acc)))
          ($e $e))))))
(= (pyrt:filter-next $f $i)
   (case (pyrt:next-of $i)
     (((m:val (m:exhausted)) (m:val (m:exhausted)))
      ((m:val $x)
       (if (== $f (py:None))
           (if (pyrt:truthy $x) (m:val $x) (pyrt:filter-next $f $i))
           (case (pyrt:call-sync $f (cons-atom $x ()) ())
             (((m:val $t) (if (pyrt:truthy $t) (m:val $x) (pyrt:filter-next $f $i))) ($e $e)))))
      ($e $e))))
; print(*args, sep=' ', end='\n'): each argument's str, written at once
; print(*objects, sep=' ', end='\n', file=None, flush=False) (builtin_print):
; sep and end None or str; the texts written to FILE, else sys.stdout
(= (pyrt:print $xs $kws)
   (let $sep (pyrt:none-default (pyrt:kw-get $kws "sep" " ") " ")
     (let $end (pyrt:none-default (pyrt:kw-get $kws "end" "\n") "\n")
       (if (not (pyrt:is-str $sep)) (m:err (pyrt:exc "TypeError" (str:concat "sep must be None or a string, not " (pyrt:type-name $sep))))
         (if (not (pyrt:is-str $end)) (m:err (pyrt:exc "TypeError" (str:concat "end must be None or a string, not " (pyrt:type-name $end))))
           (case (pyrt:strs-of $xs ())
             (((m:val $ss) (pyrt:print-to (pyrt:print-file (pyrt:kw-get $kws "file" (py:None))) $ss $sep $end (pyrt:kw-get $kws "flush" False)))
              ($e $e))))))))
; print's stream: FILE, else sys.stdout (gone: RuntimeError); the process's
; own stdout and stderr streams written at once, any other by its write
; method, a text at a time (the prelude's _print_to)
(= (pyrt:print-file $file)
   (if (== $file (py:None)) (pyrt:dict-get (pyrt:object-dict (pyrt:prelude "_sys")) "stdout") $file))
(= (pyrt:print-to $f $ss $sep $end $flush)
   (if (== $f (m:missing)) (m:err (pyrt:exc "RuntimeError" "lost sys.stdout"))
     (if (== $f (pyrt:prelude "_stdout")) (let $w (system:write stdout (str:concat (str:join $sep $ss) $end)) (m:val (py:None)))
       (if (== $f (pyrt:prelude "_stderr")) (let $w (system:write stderr (str:concat (str:join $sep $ss) $end)) (m:val (py:None)))
         (pyrt:prelude-call "_print_to" (cons-atom $f (cons-atom (pyrt:list-new $ss) (cons-atom $sep (cons-atom $end (cons-atom $flush ()))))))))))
(= (pyrt:strs-of $xs $acc)
   (if (== (size-atom $xs) 0) (m:val (pyrt:rev-onto $acc ()))
     (let ($x $rest) (decons-atom $xs)
       (case (pyrt:str-of $x) (((m:val $s) (pyrt:strs-of $rest (cons-atom $s $acc))) ($e $e))))))
(= (pyrt:none-default $v $d) (if (== $v (py:None)) $d $v))
(= (pyrt:join-str-sep $xs $sep)
   (if (== $xs ()) ""
     (let ($x $rest) (decons-atom $xs)
       (if (== $rest ()) (pyrt:str $x) (str:concat (str:concat (pyrt:str $x) $sep) (pyrt:join-str-sep $rest $sep))))))
(= (pyrt:no-keywords $b)
   (m:err (pyrt:exc "TypeError" (str:concat (str:symbol->string $b) "() takes no keyword arguments"))))
(= (pyrt:arg-count-error $b $n $given)
   (m:err (pyrt:exc "TypeError"
            (str:concat (str:concat (str:concat (str:symbol->string $b) "() takes exactly one argument (")
                                    (str:number->string $given)) " given)"))))

(= (pyrt:call-builtin-pos $b $xs)
   (case (bcall $b $xs)
     (((bcall len ($x)) (pyrt:len $x))
      ((bcall repr ($x)) (pyrt:repr-of $x))
      ((bcall str ()) (m:val ""))
      ((bcall str ($x)) (pyrt:str-of $x))
      ((bcall bool ()) (m:val False))
      ((bcall bool ($x)) (pyrt:truth-of $x))
      ((bcall int ()) (m:val 0))
      ((bcall int ($x)) (pyrt:to-int $x))
      ((bcall int ($x $base)) (pyrt:prelude-call "_int_new" $xs))
      ((bcall float ()) (m:val 0.0))
      ((bcall float ($x)) (pyrt:to-float $x))
      ((bcall round ($x)) (pyrt:round $x (py:None)))
      ((bcall round ($x $n)) (pyrt:round $x $n))
      ((bcall divmod ($x $y)) (pyrt:divmod $x $y))
      ((bcall hash ($x)) (pyrt:hash $x))
      ((bcall abs ($x)) (if (pyrt:needs-python $x) (pyrt:prelude-call "_abs" (cons-atom $x ())) (if (pyrt:is-float $x) (m:val (float:abs $x)) (if (pyrt:intish $x) (m:val (pyrt:abs (pyrt:int-of $x))) (m:err (pyrt:exc "TypeError" (str:concat "bad operand type for abs(): " (pyrt:quoted (pyrt:type-name $x)))))))))
      ((bcall slice $sargs) (pyrt:make-slice-of $sargs))
      ((bcall range ($b1)) (pyrt:make-range 0 $b1 1))
      ((bcall range ($a1 $b1)) (pyrt:make-range $a1 $b1 1))
      ((bcall range ($a1 $b1 $s1)) (pyrt:make-range $a1 $b1 $s1))
      ((bcall list ()) (m:val (pyrt:list-new ())))
      ((bcall list ($x)) (case (pyrt:elements $x) (((m:val $es) (m:val (pyrt:list-new $es))) ($e $e))))
      ((bcall tuple ()) (m:val (py:tuple)))
      ((bcall tuple ($x)) (case (pyrt:elements $x) (((m:val $es) (m:val (cons-atom py:tuple $es))) ($e $e))))
      ((bcall set ()) (m:val (pyrt:set-new)))
      ((bcall set ($x)) (let $s (pyrt:set-new) (pyrt:set-update $s $x)))
      ((bcall frozenset ()) (m:val (pyrt:freeze (pyrt:set-new))))
      ((bcall frozenset ($x))
       (case $x
         (((py:frozenset $i) (m:val $x))
          ($other (let $s (pyrt:set-new) (case (pyrt:set-update $s $x) (((m:val $t) (m:val (pyrt:freeze $t))) ($e $e))))))))
      ((bcall dict ()) (m:val (pyrt:dict-new)))
      ((bcall sum ($x)) (pyrt:sum-call $x 0))
      ((bcall sum ($x $s)) (pyrt:sum-call $x $s))
      ((bcall filter ($f $it)) (case (pyrt:now (pyrt:iter $it)) (((m:val $i) (pyrt:iter-new (it:filter $f $i))) ($e $e))))
      ((bcall filter $fargs) (m:err (pyrt:exc "TypeError" (str:concat "filter expected 2 arguments, got " (str:number->string (size-atom $fargs))))))
      ((bcall map $margs) (if (< (size-atom $margs) 2) (m:err (pyrt:exc "TypeError" "map() must have at least two arguments."))
                            (let ($f $its) (decons-atom $margs) (pyrt:map-new $f $its))))
      ((bcall reversed ($x)) (case (pyrt:elements $x) (((m:val $es) (pyrt:iter-new (it:seq (pyrt:reverse $es) 0))) ($e $e))))
      ((bcall enumerate ($x)) (case (pyrt:now (pyrt:iter $x)) (((m:val $i) (pyrt:iter-new (it:enumerate $i 0))) ($e $e))))
      ((bcall enumerate ($x $s)) (if (pyrt:intish $s) (case (pyrt:now (pyrt:iter $x)) (((m:val $i) (pyrt:iter-new (it:enumerate $i (pyrt:int-of $s)))) ($e $e)))
                                     (m:err (pyrt:exc "TypeError" (str:concat (pyrt:quoted (pyrt:type-name $s)) " object cannot be interpreted as an integer")))))
      ((bcall zip $its) (pyrt:zip-of $its))
      ((bcall iter ($x)) (pyrt:iter $x))
      ((bcall chr ($x)) (pyrt:chr $x))
      ((bcall ord ($x)) (pyrt:ord $x))
      ((bcall dir ($x)) (pyrt:prelude-call "_dir" (cons-atom $x ())))
      ; next(it) (builtin_next): the iterator's own exception passes on, its
      ; StopIteration's value too; a native iterator's end is StopIteration()
      ((bcall next ($x)) (case $x (((py:gen $id) (pyrt:gen-method-result (pyrt:gen-send $x (py:None))))
                                    ((py:obj $id) (pyrt:prelude-sync "_next" (cons-atom $x ())))
                                    ($o (case (pyrt:next-of $x) (((m:val (m:exhausted)) (m:err (pyrt:exc-args "StopIteration" ()))) ($r $r)))))))
      ((bcall next ($x $d)) (case (pyrt:next-of $x)
                              (((m:val (m:exhausted)) (m:val $d))
                               ((m:err $e) (let $i (pyrt:exc-instance $e) (if (pyrt:exc-is $i StopIteration) (m:val $d) (m:err $e))))
                               ($r $r))))
      ((bcall isinstance ($x $t)) (pyrt:isinstance $x $t))
      ((bcall issubclass ($c $t)) (pyrt:issubclass $c $t))
      ((bcall any ($x)) (case (pyrt:elements $x) (((m:val $es) (pyrt:truth-fold $es true)) ($e $e))))
      ((bcall all ($x)) (case (pyrt:elements $x) (((m:val $es) (pyrt:truth-fold $es false)) ($e $e))))
      ($other (pyrt:call-builtin-other $b $xs ())))))
(= (pyrt:next-of $x) (case $x (((py:iter $id) (pyrt:next $x)) ((py:gen $id) (pyrt:gen-next $x)) ((py:obj $id) (pyrt:python-next $x)) ($o (m:err (pyrt:exc "TypeError" (str:concat (pyrt:quoted (pyrt:type-name $x)) " object is not an iterator")))))))
; range(start, stop, step) (range_from_array): each by PyNumber_Index, in
; order; a zero step refused
(= (pyrt:make-range $a0 $b0 $s0)
   (case (pyrt:index-int $a0)
     (((m:val $a)
       (case (pyrt:index-int $b0)
         (((m:val $b)
           (case (pyrt:index-int $s0)
             (((m:val $s) (if (== $s 0) (m:err (pyrt:exc "ValueError" "range() arg 3 must not be zero")) (m:val (py:range $a $b $s))))
              ((m:missing) (pyrt:not-an-integer $s0))
              ($e $e))))
          ((m:missing) (pyrt:not-an-integer $b0))
          ($e $e))))
      ((m:missing) (pyrt:not-an-integer $a0))
      ($e $e))))
(= (pyrt:not-an-integer $x)
   (m:err (pyrt:exc "TypeError" (str:concat (pyrt:quoted (pyrt:type-name $x)) " object cannot be interpreted as an integer"))))
; PyNumber_Index: an int; an int subclass's instance by its int (its
; __index__ not asked); an object whose type has __index__ by that (the
; prelude's _index); else missing
(= (pyrt:index-int $i)
   (if (pyrt:intish $i) (m:val (pyrt:int-of $i))
     (let $b (pyrt:base-of $i)
       (if (pyrt:intish $b) (m:val (pyrt:int-of $b))
         (if (pyrt:needs-python $i)
             (if (== (pyrt:mro-lookup (pyrt:type-of $i) "__index__") (m:missing)) (m:missing)
               (case (pyrt:prelude-sync "_index" (cons-atom $i ()))
                 (((m:val $r) (m:val (pyrt:int-of (pyrt:base-of $r))))
                  ($e $e))))
             (m:missing))))))
; int(x) (PyNumber_Long): an int, a float truncated, a str of a sign and
; decimal digits here; any other text, and an object's __int__, __index__ or
; __trunc__, by the prelude's (PyLong_FromString's rules, its messages)
(= (pyrt:to-int $x)
   (if (pyrt:intish $x) (m:val (pyrt:int-of $x))
     (if (pyrt:is-str $x)
         (let $t (str:trim $x)
           (if (pyrt:decimal-text $t) (m:val (str:string->number $t))
             (pyrt:prelude-call "_int_new" (cons-atom $x ()))))
         (if (pyrt:is-float $x)
             (if (float:is-nan $x) (m:err (pyrt:exc "ValueError" "cannot convert float NaN to integer"))
               (if (float:is-inf $x) (m:err (pyrt:exc "OverflowError" "cannot convert float infinity to integer"))
                 (m:val (float:to-int $x))))
           (if (pyrt:needs-python $x) (pyrt:prelude-call "_number_long" (cons-atom $x ()))
             (m:err (pyrt:exc "TypeError" (str:concat (str:concat "int() argument must be a string, a bytes-like object or a real number, not " (pyrt:quoted (pyrt:type-name $x))) ""))))))))
; an optional sign and ASCII decimal digits, nothing else (the code points
; come as a native list: its end is size 0, not ==  to ())
(= (pyrt:decimal-text $t)
   (let $cs (str:codepoint-values $t)
     (if (== (size-atom $cs) 0) false
       (let ($c $rest) (decons-atom $cs)
         (if (if (== $c 43) true (== $c 45)) (pyrt:ascii-digits $rest) (pyrt:ascii-digits $cs))))))
(= (pyrt:ascii-digits $cs) (if (== (size-atom $cs) 0) false (pyrt:ascii-digits-from $cs)))
(= (pyrt:ascii-digits-from $cs)
   (if (== (size-atom $cs) 0) true
     (let ($c $rest) (decons-atom $cs) (if (if (>= $c 48) (<= $c 57) false) (pyrt:ascii-digits-from $rest) false))))
; sum(iterable, start) as builtin_sum_impl: a str start refused; while the
; total is an exact int, ints and bools added exactly; while it is a float,
; floats added with Neumaier's compensation and ints that fit a C long added
; as floats; from any other item on, +.  Items are taken one at a time
(= (pyrt:sum-call $x $start)
   (if (pyrt:is-str $start) (m:err (pyrt:exc "TypeError" "sum() can't sum strings [use ''.join(seq) instead]"))
     (case (pyrt:now (pyrt:iter $x)) (((m:val $it) (pyrt:sum-from $it $start)) ($e $e)))))
(= (pyrt:sum-from $it $r)
   (if (pyrt:exact-int $r) (pyrt:sum-ints $it (pyrt:int-of $r))
     (if (pyrt:is-float $r) (pyrt:sum-floats $it $r 0.0) (pyrt:sum-generic $it $r))))
(= (pyrt:exact-int $x) (if (pyrt:is-bool $x) false (pyrt:is-int $x)))
(= (pyrt:sum-ints $it $acc)
   (case (pyrt:next-of $it)
     (((m:val (m:exhausted)) (m:val $acc))
      ((m:val $x) (if (pyrt:intish $x) (pyrt:sum-ints $it (+ $acc (pyrt:int-of $x)))
                    (case (pyrt:now (pyrt:binop (py:Add) $acc $x))
                      (((m:val $r) (if (pyrt:is-float $r) (pyrt:sum-floats $it $r 0.0) (pyrt:sum-generic $it $r))) ($e $e)))))
      ($e $e))))
(= (pyrt:sum-floats $it $f $c)
   (case (pyrt:next-of $it)
     (((m:val (m:exhausted)) (m:val (pyrt:sum-final $f $c)))
      ((m:val $x)
       (if (pyrt:is-float $x)
           (let $t (float:add $f $x)
             (pyrt:sum-floats $it $t
               (float:add $c (if (>= (float:abs $f) (float:abs $x)) (float:add (float:sub $f $t) $x) (float:add (float:sub $x $t) $f)))))
           (if (if (pyrt:intish $x) (pyrt:fits-long (pyrt:int-of $x)) false)
               (pyrt:sum-floats $it (float:add $f (float:from-int (pyrt:int-of $x))) $c)
               (case (pyrt:now (pyrt:binop (py:Add) (pyrt:sum-final $f $c) $x)) (((m:val $r) (pyrt:sum-generic $it $r)) ($e $e))))))
      ($e $e))))
(= (pyrt:sum-final $f $c) (if (if (float:is-finite $c) (not (== $c 0.0)) false) (float:add $f $c) $f))
(= (pyrt:fits-long $n) (if (>= $n -9223372036854775808) (<= $n 9223372036854775807) false))
(= (pyrt:sum-generic $it $r)
   (case (pyrt:next-of $it)
     (((m:val (m:exhausted)) (m:val $r))
      ((m:val $x) (case (pyrt:now (pyrt:binop (py:Add) $r $x)) (((m:val $r2) (pyrt:sum-generic $it $r2)) ($e $e))))
      ($e $e))))
(= (pyrt:take $es $n) (if (== $n 0) () (let ($x $rest) (decons-atom $es) (cons-atom $x (pyrt:take $rest (- $n 1))))))
(= (pyrt:drop $es $n) (if (== $n 0) $es (pyrt:drop (cdr-atom $es) (- $n 1))))
(= (pyrt:reverse $es) (pyrt:rev-onto $es ()))
(= (pyrt:numbered $es $i) (if (== $es ()) () (let ($x $rest) (decons-atom $es) (cons-atom (py:tuple $i $x) (pyrt:numbered $rest (+ $i 1))))))
(= (pyrt:zip-of $its) (case (pyrt:iters-of $its ()) (((m:val $is) (pyrt:iter-new (it:zip $is))) ($e $e))))
(= (pyrt:all-elements $its $acc)
   (if (== $its ()) (m:val $acc)
     (let ($x $rest) (decons-atom $its)
       (case (pyrt:elements $x) (((m:val $es) (pyrt:all-elements $rest (pyrt:append $acc (cons-atom $es ())))) ($e $e))))))
(= (pyrt:zip-lists $ls)
   (if (== (size-atom $ls) 0) ()
     (if (pyrt:any-empty $ls) () (cons-atom (cons-atom py:tuple (pyrt:heads $ls)) (pyrt:zip-lists (pyrt:tails $ls))))))
; emptiness by length: a list compared with == is evaluated first
(= (pyrt:any-empty $ls) (if (== (size-atom $ls) 0) false (if (== (size-atom (car-atom $ls)) 0) true (pyrt:any-empty (cdr-atom $ls)))))
(= (pyrt:heads $ls) (if (== $ls ()) () (cons-atom (car-atom (car-atom $ls)) (pyrt:heads (cdr-atom $ls)))))
(= (pyrt:tails $ls) (if (== $ls ()) () (cons-atom (cdr-atom (car-atom $ls)) (pyrt:tails (cdr-atom $ls)))))
(= (pyrt:no-attribute $x $name)
   (m:err (pyrt:exc "AttributeError" (str:concat (str:concat (pyrt:quoted (pyrt:type-name $x)) " object has no attribute ") (pyrt:quoted $name)))))
(= (pyrt:has-method $kind $name)
   (case (mname $kind $name)
     (((mname list "append") true) ((mname list "remove") true) ((mname list "pop") true) ((mname list "extend") true) ((mname list "insert") true)
      ((mname list "index") true) ((mname list "count") true) ((mname list "reverse") true) ((mname list "sort") true)
      ((mname tuple "index") true) ((mname tuple "count") true)
      ((mname list "copy") true) ((mname list "clear") true)
      ((mname dict "get") true) ((mname dict "keys") true) ((mname dict "values") true) ((mname dict "items") true)
      ((mname dict "pop") true) ((mname dict "setdefault") true) ((mname dict "update") true) ((mname dict "copy") true)
      ((mname dict "clear") true) ((mname dict "popitem") true)
      ((mname str "join") true) ((mname str "split") true) ((mname str "strip") true) ((mname str "upper") true)
      ((mname str "lower") true) ((mname str "replace") true) ((mname str "startswith") true) ((mname str "endswith") true)
      ((mname str "find") true) ((mname str "count") true) ((mname str "isdigit") true)
      ((mname set "add") true)
      ($other false))))
(= (pyrt:kind-of $x)
   (if (pyrt:is-str $x) str
     (if (pyrt:is-tuple $x) tuple
       (case $x (((py:list $i) list) ((py:dict $i) dict) ((py:set $i) set) ((py:frozenset $i) frozenset) ($o none))))))
; obj.name: a bound method of a built-in type, (py:method OBJ NAME)
; obj.name: an object's or a class's through the prelude's protocol
; (getattr, __getattribute__ then __getattr__); a builtin value's here
(= (pyrt:getattr $x $name)
   (case $x
     (((py:type $t) (pyrt:prelude-call "_getattr" (cons-atom $x (cons-atom $name ()))))
      ((py:obj $id) (pyrt:object-getattr $x $name))
      ((py:func $id) (pyrt:prelude-call "_getattr" (cons-atom $x (cons-atom $name ()))))
      ((py:gen $id) (pyrt:prelude-call "_getattr" (cons-atom $x (cons-atom $name ()))))
      ((py:coro $id) (pyrt:prelude-call "_getattr" (cons-atom $x (cons-atom $name ()))))
      ((py:agen $id) (pyrt:prelude-call "_getattr" (cons-atom $x (cons-atom $name ()))))
      ((py:bound $id $f $s) (pyrt:bound-attribute $x $name))
      ($other (pyrt:getattr-native $x $name)))))
; obj.name for a program's object (PyObject_GenericGetAttr) when its class
; keeps object's __getattribute__ and has no __getattr__, here for the
; attributes whose descriptor needs no Python: none, a plain value of a
; builtin type, a function (bound to the object), a builtin member's
; descriptor (bound through its own get).  None of these is a data
; descriptor, so the object's own attribute comes first.  Any other
; descriptor (a property, a classmethod, a program's descriptor), any other
; class, and a __dict__ that is not a dict take the prelude's protocol.
(= (pyrt:object-getattr $o $name)
   (let $tp (pyrt:type-of $o)
     (if (pyrt:generic-getattr-class $tp)
         (let $descr (pyrt:mro-lookup $tp $name)
           (let $k (pyrt:plain-descriptor-kind $descr)
             (if (== $k other) (pyrt:prelude-call "_object_getattribute" (cons-atom $o (cons-atom $name ())))
               (let $own (pyrt:own-attribute $o $name)
                 (if (== $own (m:unknown)) (pyrt:prelude-call "_object_getattribute" (cons-atom $o (cons-atom $name ())))
                   (if (== $own (m:missing)) (pyrt:class-attribute-for $k $descr $o $tp $name) (m:val $own)))))))
         (pyrt:prelude-call "_getattr" (cons-atom $o (cons-atom $name ()))))))
(= (pyrt:generic-getattr-class $tp)
   (if (== (pyrt:mro-lookup $tp "__getattribute__") (py:bmeth object "__getattribute__"))
       (== (pyrt:mro-lookup $tp "__getattr__") (m:missing))
       false))
; a class attribute's kind, for the generic lookup: absent; a value whose type
; has no __get__ (a builtin type's value); a function; a builtin member's
; descriptor; or other
(= (pyrt:plain-descriptor-kind $d)
   (if (== $d (m:missing)) absent
     (if (pyrt:plain-builtin-value $d) value
       (case $d
         (((py:func $fid) function)
          ((py:bmeth $bt $bn) member)
          ($o other))))))
(= (pyrt:plain-builtin-value $d)
   (if (== $d (py:None)) true
     (if (pyrt:is-bool $d) true
       (if (pyrt:is-int $d) true
         (if (pyrt:is-str $d) true
           (if (pyrt:is-float $d) true
             (if (pyrt:is-tuple $d) true
               (case $d
                 (((py:list $i) true) ((py:dict $i) true) ((py:set $i) true) ((py:frozenset $i) true)
                  ((py:Bytes $v) true) ((py:range $p $q $r) true) ((py:slice $p $q $r) true) ($o false))))))))))
; the object's own attribute: its __dict__'s entry, (m:missing), or
; (m:unknown) when its __dict__ is not a dict
(= (pyrt:own-attribute $o $name)
   (let $d (pyrt:object-dict $o)
     (if (== $d (py:None)) (m:missing)
       (let $b (pyrt:base-of $d)
         (case $b
           (((py:dict $did) (pyrt:dict-get $b $name))
            ($nd (m:unknown))))))))
; no attribute of the object's own: the class's, by its kind
(= (pyrt:class-attribute-for $k $descr $o $tp $name)
   (case $k
     ((absent (m:err (pyrt:attribute-error $o $name)))
      (value (m:val $descr))
      (function (m:val (py:bound (pyrt:new-id) $descr $o)))
      (member (case $descr (((py:bmeth $bt $bn) (pyrt:bmeth-get $bt $bn $o $tp))))))))
; obj.name to its value, from the host
(= (pyrt:getattr-now $x $name)
   (case (pyrt:getattr $x $name)
     (((m:call (c:code $nm $q $a $ns $cs $fs $fl $body $cofile) $frame) (pyrt:run-to-value $body $frame))
      ($other $other))))
(= (pyrt:bound-attribute (py:bound $id $f $s) $name)
   (case $name
     (("__self__" (m:val $s)) ("__func__" (m:val $f))
      ($other (pyrt:getattr $f $name)))))
; a builtin value's attribute: its type's method the host implements, else
; what the data model finds (methods the prelude gives builtin types too)
(= (pyrt:getattr-native $x $name)
   (let $k (pyrt:kind-of $x)
     (if (if (== $k none) false (pyrt:has-method $k $name))
         (m:val (py:method $x $name))
         (pyrt:prelude-call "_getattr" (cons-atom $x (cons-atom $name ()))))))
(= (pyrt:call-method $x $name $xs $kws)
   (case (pyrt:getattr-now $x $name) (((m:val $m) (pyrt:call-kw $m $xs $kws)) ($e $e))))
; the arguments a builtin method reads as data: a str method's (but join's
; iterable), list.pop's and list.insert's index (PyUnicode_Check and
; PyLong_Check accept a subclass's instance, whose builtin part is read)
(= (pyrt:method-args $k $name $xs)
   (if (== $k str) (if (== $name "join") $xs (pyrt:base-each $xs))
     (if (== $k list) (if (pyrt:in-flat $name ("pop" "insert")) (pyrt:first-base-of $xs) $xs) $xs)))
(= (pyrt:base-each $xs) (if (== (size-atom $xs) 0) () (cons-atom (pyrt:base-of (car-atom $xs)) (pyrt:base-each (cdr-atom $xs)))))
(= (pyrt:first-base-of $xs) (if (== (size-atom $xs) 0) () (cons-atom (pyrt:base-of (car-atom $xs)) (cdr-atom $xs))))
(= (pyrt:call-bound $x $name $xs0)
   (let $k (pyrt:kind-of $x)
    (let $xs (pyrt:method-args $k $name $xs0)
     (case (mcall $k $name $xs)
       (((mcall list "append" ($v)) (let $a (pyrt:list-append $x $v) (m:val (py:None))))
        ((mcall list "pop" ()) (if (== (pyrt:list-len $x) 0) (m:err (pyrt:exc "IndexError" "pop from empty list")) (m:val (pyrt:list-pop-last $x))))
        ; list.pop(i) (list_pop_impl): a negative index from the end; the
        ; items after it move down
        ((mcall list "pop" ($i))
         (if (pyrt:intish $i)
             (let $n (pyrt:list-len $x)
               (if (== $n 0) (m:err (pyrt:exc "IndexError" "pop from empty list"))
                 (let $j (if (< (pyrt:int-of $i) 0) (+ (pyrt:int-of $i) $n) (pyrt:int-of $i))
                   (if (if (< $j 0) true (>= $j $n)) (m:err (pyrt:exc "IndexError" "pop index out of range"))
                     (let $v (pyrt:list-get $x $j)
                       (let $r (pyrt:list-splice $x $j (+ $j 1) ()) (m:val $v)))))))
             (m:err (pyrt:exc "TypeError" (str:concat (str:concat "'" (pyrt:type-name $i)) "' object cannot be interpreted as an integer")))))
        ; list.extend (list_extend): the items first (the list's own, when it
        ; extends itself), then each appended
        ((mcall list "extend" ($it)) (case (pyrt:elements $it) (((m:val $es) (let $r (pyrt:list-append-all $x $es) (m:val (py:None)))) ($e $e))))
        ((mcall list "insert" ($i $v))
         (case (pyrt:index-int $i)
           (((m:val $j0) (let $r (pyrt:list-splice $x (pyrt:insert-index $j0 (pyrt:list-len $x)) (pyrt:insert-index $j0 (pyrt:list-len $x)) (cons-atom $v ())) (m:val (py:None))))
            ((m:missing) (pyrt:not-an-integer $i))
            ($e $e))))
        ((mcall list "index" $args) (pyrt:seq-index "list" (pyrt:list-items $x) $args))
        ((mcall list "count" $args) (pyrt:seq-count "list" (pyrt:list-items $x) $args))
        ((mcall tuple "index" $args) (pyrt:seq-index "tuple" (cdr-atom $x) $args))
        ((mcall tuple "count" $args) (pyrt:seq-count "tuple" (cdr-atom $x) $args))
        ((mcall list "remove" ($v)) (pyrt:list-remove $x (pyrt:list-items $x) $v 0))
        ((mcall list "reverse" ()) (let $r (pyrt:list-reset $x (pyrt:reverse (pyrt:list-items $x))) (m:val (py:None))))
        ((mcall list "sort" $sargs) (pyrt:list-sort $x $sargs ()))
        ((mcall list "copy" ()) (m:val (pyrt:list-new (pyrt:list-items $x))))
        ((mcall list "clear" ()) (let $r (pyrt:list-reset $x ()) (m:val (py:None))))
        ((mcall dict "get" ($key)) (pyrt:dict-lookup $x $key (py:None)))
        ((mcall dict "get" ($key $d)) (pyrt:dict-lookup $x $key $d))
        ((mcall dict "keys" ()) (m:val (py:view keys $x)))
        ((mcall dict "values" ()) (m:val (py:view values $x)))
        ((mcall dict "items" ()) (m:val (py:view items $x)))
        ((mcall dict "setdefault" ($key $d)) (case (pyrt:dict-key $key) (((m:val $ck) (let $v (pyrt:dict-get $x $ck) (if (== $v (m:missing)) (let $s (pyrt:dict-put $x $ck $key $d) (m:val $d)) (m:val $v)))) ($e $e))))
        ((mcall dict "pop" ($key)) (case (pyrt:dict-key $key) (((m:val $ck) (let $v (pyrt:dict-get $x $ck) (if (== $v (m:missing)) (m:err (pyrt:exc-args "KeyError" (cons-atom $key ()))) (let $d (pyrt:dict-delete $x $ck) (m:val $v))))) ($e $e))))
        ((mcall dict "pop" ($key $dflt)) (case (pyrt:dict-key $key) (((m:val $ck) (let $v (pyrt:dict-get $x $ck) (if (== $v (m:missing)) (m:val $dflt) (let $d (pyrt:dict-delete $x $ck) (m:val $v))))) ($e $e))))
        ((mcall dict "update" $us) (pyrt:dict-update $x $us ()))
        ((mcall dict "setdefault" ($key)) (case (pyrt:dict-key $key) (((m:val $ck) (let $v (pyrt:dict-get $x $ck) (if (== $v (m:missing)) (let $s (pyrt:dict-put $x $ck $key (py:None)) (m:val (py:None))) (m:val $v)))) ($e $e))))
        ((mcall dict "popitem" ()) (pyrt:dict-popitem $x))
        ((mcall dict "copy" ()) (pyrt:build-dict (pyrt:dict-entries $x)))
        ((mcall dict "clear" ()) (let $c (pyrt:dict-clear $x) (m:val (py:None))))
        ((mcall str "join" ($it)) (case (pyrt:elements $it) (((m:val $es) (pyrt:str-join $x $es)) ($e $e))))
        ; the forms of str's methods the host takes whole (the Unicode data of
        ; the string library); any other form is the prelude's method
        ((mcall str "split" ()) (m:val (pyrt:list-new (pyrt:seq-copy (str:split-props (pyrt:space-mask) -1 $x)))))
        ((mcall str "split" ($sep)) (if (pyrt:nonempty-str $sep) (m:val (pyrt:list-new (pyrt:split-on $sep $x))) (pyrt:call-type-method $x $name $xs ())))
        ((mcall str "strip" ()) (m:val (str:trim-props (pyrt:space-mask) "both" $x)))
        ((mcall str "upper" ()) (m:val (str:case-map "upper" $x)))
        ((mcall str "lower" ()) (m:val (str:case-map "lower" $x)))
        ((mcall str "replace" ($a $b)) (if (pyrt:nonempty-str $a) (if (pyrt:is-str $b) (m:val (str:join $b (pyrt:split-on $a $x))) (pyrt:call-type-method $x $name $xs ())) (pyrt:call-type-method $x $name $xs ())))
        ((mcall str "startswith" ($p)) (if (pyrt:is-str $p) (m:val (str:starts-with $x $p)) (pyrt:call-type-method $x $name $xs ())))
        ((mcall str "endswith" ($p)) (if (pyrt:is-str $p) (m:val (str:ends-with $x $p)) (pyrt:call-type-method $x $name $xs ())))
        ((mcall str "find" ($p)) (if (pyrt:is-str $p) (m:val (str:find-index $p $x)) (pyrt:call-type-method $x $name $xs ())))
        ((mcall str "count" ($p)) (if (pyrt:nonempty-str $p) (m:val (- (size-atom (pyrt:split-on $p $x)) 1)) (pyrt:call-type-method $x $name $xs ())))
        ((mcall str "isdigit" ()) (m:val (if (== $x "") False (if (str:props-all 206158430208 $x) True False))))
        ((mcall set "add" ($v)) (case (pyrt:set-key $v) (((m:val $kv) (let $a (pyrt:set-add $x $kv $v) (m:val (py:None)))) ($e $e))))
        ($other (pyrt:call-type-method $x $name $xs ())))))))
; a form of a builtin method the host does not take: the method the prelude
; gives the value's type, with its keywords; else the refusal
(= (pyrt:call-type-method $x $name $xs $ks)
   (let $f (pyrt:mro-lookup (pyrt:type-of $x) $name)
     (case (pyrt:method-implementation $f)
       (((py:func $id) (pyrt:call-kw (py:func $id) (cons-atom $x $xs) $ks))
        ($other (if (== $ks ())
                    (m:err (pyrt:exc "TypeError" (str:concat (str:concat (pyrt:type-name $x) ".") (str:concat $name "() was given the wrong arguments"))))
                    (m:err (pyrt:exc "TypeError" (str:concat (str:concat (str:concat (pyrt:type-name $x) ".") $name) "() takes no keyword arguments")))))))))
; a type's member as a function to call: a function, or the prelude's
; implementation of a builtin member
(= (pyrt:method-implementation $f)
   (case $f (((py:bmeth $t $n) (pyrt:bmeth-impl $t $n)) ($other $f))))
(= (pyrt:nonempty-str $x) (if (pyrt:is-str $x) (!= $x "") false))
; Python's whitespace (Py_UNICODE_ISSPACE: category Zs, bidirectional class
; WS, B or S), as the string library's property bits
(= (pyrt:space-mask) 3848294891520)
(= (pyrt:dict-lookup $d $k $default)
   (case (pyrt:dict-key $k) (((m:val $ck) (let $v (pyrt:dict-get $d $ck) (m:val (if (== $v (m:missing)) $default $v)))) ($e $e))))
(= (pyrt:dict-clear (py:dict $id))
   (let $es (collapse (match &py-heap (h:dent $id $k $o $v $s) (h:dent $id $k $o $v $s))) (pyrt:remove-all $es)))
; remove-atom does not evaluate its atom: each is taken first
(= (pyrt:remove-all $as)
   (if (== $as ()) () (let ($a $rest) (decons-atom $as) (let $r (pyrt:heap-remove $a) (pyrt:remove-all $rest)))))
; list.index and tuple.index (list_index_impl, tuple_index_impl): VALUE's
; first place from START to STOP, each an index (_PyEval_SliceIndexNotNone; a
; negative one counted from the end, START then clamped at 0, STOP at the
; length)
(= (pyrt:seq-index $what $es $args)
   (let $k (size-atom $args)
     (if (< $k 1) (m:err (pyrt:exc "TypeError" (str:concat "index expected at least 1 argument, got " (str:number->string $k))))
       (if (> $k 3) (m:err (pyrt:exc "TypeError" (str:concat "index expected at most 3 arguments, got " (str:number->string $k))))
         (let $n (size-atom $es)
           (case (pyrt:slice-bound-at $args 1 0)
             (((m:val $lo0)
               (case (pyrt:slice-bound-at $args 2 9223372036854775807)
                 (((m:val $hi0)
                   (let $lo (if (< $lo0 0) (if (< (+ $lo0 $n) 0) 0 (+ $lo0 $n)) $lo0)
                     (let $hi (if (< $hi0 0) (+ $hi0 $n) (if (> $hi0 $n) $n $hi0))
                       (pyrt:index-between $what $es (car-atom $args) $lo $hi 0))))
                  ($e $e))))
              ($e $e))))))))
(= (pyrt:slice-bound-at $args $i $default)
   (if (< $i (size-atom $args))
       (case (pyrt:index-int (index-atom $args $i))
         (((m:val $v) (m:val $v))
          ((m:missing) (m:err (pyrt:exc "TypeError" "slice indices must be integers or have an __index__ method")))
          ($e $e)))
       (m:val $default)))
(= (pyrt:index-between $what $es $v $lo $hi $i)
   (if (if (>= $i $hi) true (== $es ()))
       (m:err (pyrt:exc "ValueError" (str:concat (str:concat (str:concat $what ".index(x): x not in ") $what) "")))
       (if (< $i $lo) (pyrt:index-between $what (cdr-atom $es) $v $lo $hi (+ $i 1))
         (case (pyrt:eq (car-atom $es) $v)
           (((m:val $b) (if $b (m:val $i) (pyrt:index-between $what (cdr-atom $es) $v $lo $hi (+ $i 1))))
            ($e $e))))))
; list.count and tuple.count (METH_O): VALUE's number of occurrences
(= (pyrt:seq-count $what $es $args)
   (if (== (size-atom $args) 1) (pyrt:count-in $es (car-atom $args) 0)
     (m:err (pyrt:exc "TypeError" (str:concat (str:concat (str:concat $what ".count() takes exactly one argument (")
                                                         (str:number->string (size-atom $args))) " given)")))))
; list.remove(x): the first item equal to X removed, else ValueError
(= (pyrt:list-remove $l $es $v $i)
   (if (== (size-atom $es) 0) (m:err (pyrt:exc "ValueError" "list.remove(x): x not in list"))
     (case (pyrt:eq (car-atom $es) $v)
       (((m:val $b) (if $b
                        (let $items (pyrt:list-items $l)
                          (let $r (pyrt:list-reset $l (pyrt:append (pyrt:take $items $i) (pyrt:drop $items (+ $i 1)))) (m:val (py:None))))
                        (pyrt:list-remove $l (cdr-atom $es) $v (+ $i 1))))
        ($e $e)))))
(= (pyrt:count-in $es $v $n)
   (if (== $es ()) (m:val $n)
     (case (pyrt:eq (car-atom $es) $v) (((m:val $b) (pyrt:count-in (cdr-atom $es) $v (if $b (+ $n 1) $n))) ($e $e)))))
; a sequence rebuilt as an ordinary expression: a list taken out of a value
; may be held in another representation, which the str natives refuse
(= (pyrt:seq-copy $es) (if (== (size-atom $es) 0) () (cons-atom (car-atom $es) (pyrt:seq-copy (cdr-atom $es)))))
; PyUnicode_Join: each item a str (a str subclass's instance by its str)
(= (pyrt:str-join $sep $es)
   (if (pyrt:all-str $es 0) (m:val (str:join $sep (pyrt:str-datas $es)))
     (m:err (pyrt:exc "TypeError" (str:concat (str:concat "sequence item " (str:number->string (pyrt:first-non-str $es 0))) (str:concat ": expected str instance, " (str:concat (pyrt:type-name (index-atom $es (pyrt:first-non-str $es 0))) " found")))))))
; a str's data: the str, or a str subclass's instance's str part
(= (pyrt:str-data $x) (if (pyrt:is-str $x) $x (let $b (pyrt:base-of $x) (if (pyrt:is-str $b) $b (m:missing)))))
(= (pyrt:str-datas $es) (if (== (size-atom $es) 0) () (cons-atom (pyrt:str-data (car-atom $es)) (pyrt:str-datas (cdr-atom $es)))))
(= (pyrt:all-str $es $i) (if (== (size-atom $es) 0) true (if (== (pyrt:str-data (car-atom $es)) (m:missing)) false (pyrt:all-str (cdr-atom $es) $i))))
(= (pyrt:first-non-str $es $i) (if (== (pyrt:str-data (car-atom $es)) (m:missing)) $i (pyrt:first-non-str (cdr-atom $es) (+ $i 1))))
(= (pyrt:split-on $sep $s) (str:split $sep $s))
(= (pyrt:find-in $s $p) (let $parts (str:split $p $s) (if (== (size-atom $parts) 1) -1 (str:codepoint-count (car-atom $parts)))))
(= (pyrt:map-ascii $s $lo $hi $delta) (str:from-codepoints (pyrt:map-cps (str:codepoint-values $s) $lo $hi $delta)))
(= (pyrt:map-cps $cps $lo $hi $delta)
   (if (== (size-atom $cps) 0) ()
     (let ($c $rest) (decons-atom $cps)
       (cons-atom (if (if (>= $c $lo) (<= $c $hi) false) (+ $c $delta) $c) (pyrt:map-cps $rest $lo $hi $delta)))))
(= (pyrt:is-digits $s) (let $cps (str:codepoint-values $s) (if (== (size-atom $cps) 0) False (pyrt:all-digit-cps $cps))))
(= (pyrt:all-digit-cps $cps)
   (if (== (size-atom $cps) 0) True
     (let ($c $rest) (decons-atom $cps) (if (if (>= $c 48) (<= $c 57) false) (pyrt:all-digit-cps $rest) False))))

(= (pyrt:abs $x) (if (< $x 0) (- 0 $x) $x))

; ---------------------------------------------------------------- unpacking

; a value unpacked to exactly N values, a tuple of them: a list or tuple by
; its length; any other iterable read through its iterator, at most N + 1
; values (CPython's UNPACK_SEQUENCE)
(= (pyrt:unpack $x $n)
   (case $x
     (((py:list $id) (pyrt:unpack-exact (pyrt:list-items $x) $n))
      ($other
       (if (pyrt:is-tuple $x) (pyrt:unpack-exact (cdr-atom $x) $n)
         (case (pyrt:now (pyrt:iter $x))
           (((m:val $it) (pyrt:unpack-read $it $n ()))
            ($e (m:err (pyrt:exc "TypeError" (str:concat (str:concat "cannot unpack non-iterable " (pyrt:type-name $x)) " object")))))))))))
; UNPACK_EX (unpack_iterable with a starred target): BEFORE items, then a
; list of the rest, its last AFTER items taken back off its end
(= (pyrt:unpack-ex $x $b $af)
   (case (pyrt:now (pyrt:iter $x))
     (((m:val $it) (pyrt:unpack-ex-read $it $b $af ()))
      ($e (m:err (pyrt:exc "TypeError" (str:concat (str:concat "cannot unpack non-iterable " (pyrt:type-name $x)) " object")))))))
(= (pyrt:unpack-ex-read $it $b $af $acc)
   (if (== (size-atom $acc) $b)
       (case (pyrt:drain $it ())
         (((m:val $rest)
           (let $ll (size-atom $rest)
             (if (< $ll $af)
                 (pyrt:not-enough-at-least (+ $b $af) (+ $b $ll))
                 (m:val (cons-atom py:tuple
                          (pyrt:append $acc (cons-atom (pyrt:list-new (pyrt:take $rest (- $ll $af))) (pyrt:drop $rest (- $ll $af)))))))))
          ($e $e)))
       (case (pyrt:next-of $it)
         (((m:val (m:exhausted)) (pyrt:not-enough-at-least (+ $b $af) (size-atom $acc)))
          ((m:val $v) (pyrt:unpack-ex-read $it $b $af (pyrt:append $acc (cons-atom $v ()))))
          ($e $e)))))
(= (pyrt:not-enough-at-least $n $k)
   (m:err (pyrt:exc "ValueError"
            (str:concat (str:concat (str:concat (str:concat "not enough values to unpack (expected at least " (str:number->string $n)) ", got ")
                                    (str:number->string $k)) ")"))))
(= (pyrt:unpack-exact $xs $n)
   (let $k (size-atom $xs)
     (if (== $k $n) (m:val (cons-atom py:tuple $xs))
       (if (> $k $n) (pyrt:too-many-of $n $k) (pyrt:not-enough $n $k)))))
(= (pyrt:unpack-read $it $n $acc)
   (let $k (size-atom $acc)
     (case (pyrt:next-of $it)
       (((m:val (m:exhausted)) (if (== $k $n) (m:val (cons-atom py:tuple $acc)) (pyrt:not-enough $n $k)))
        ((m:val $v) (if (== $k $n) (pyrt:too-many $n) (pyrt:unpack-read $it $n (pyrt:append $acc (cons-atom $v ())))))
        ($e $e)))))
(= (pyrt:too-many $n)
   (m:err (pyrt:exc "ValueError" (str:concat (str:concat "too many values to unpack (expected " (str:number->string $n)) ")"))))
; an exact list, tuple or dict says how many it held
(= (pyrt:too-many-of $n $k)
   (m:err (pyrt:exc "ValueError" (str:concat (str:concat (str:concat (str:concat "too many values to unpack (expected " (str:number->string $n)) ", got ") (str:number->string $k)) ")"))))
(= (pyrt:not-enough $n $k)
   (m:err (pyrt:exc "ValueError"
            (str:concat (str:concat (str:concat (str:concat "not enough values to unpack (expected " (str:number->string $n)) ", got ")
                                    (str:number->string $k)) ")"))))

; two sets are equal when they hold the same elements
(= (pyrt:set-eq $x $y)
   (if (== (pyrt:set-len $x) (pyrt:set-len $y)) (pyrt:all-in-set (pyrt:set-items $x) $y) false))
(= (pyrt:all-in-set $es $y)
   (if (== (size-atom $es) 0) true
     (let ($e $rest) (decons-atom $es)
       (case (pyrt:key $e)
         (((m:val $ke) (if (== (pyrt:set-entry $y $ke) (m:missing)) false (pyrt:all-in-set $rest $y)))
          ($err false))))))
