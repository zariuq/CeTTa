; What Scope.module's symbol tables ask the host: private-name mangling,
; sets of names (sorted lists without repeats), the grouping of a block's
; flag events into symbols, and the flags as CPython numbers them.
!(import! &self str)

; ---------------------------------------------------------------- mangling

; _Py_Mangle: __x inside class C is _C__x, the class name's leading
; underscores dropped; a name ending with __, or holding a dot, is kept, and
; so is any name when the class name is only underscores or there is none
(= (pyhost:mangle $class $name)
   (if (== $class (py:None)) $name
     (if (pyhost:private $name)
         (let $stripped (pyhost:drop-underscores (str:codepoints $class))
           (if (== $stripped "") $name (str:concat (str:concat "_" $stripped) $name)))
         $name)))

(= (pyhost:private $name)
   (if (str:starts-with $name "__")
       (if (str:ends-with $name "__") false (== (size-atom (str:split "." $name)) 1))
       false))

(= (pyhost:drop-underscores $cs)
   (if (== (size-atom $cs) 0) ""
     (let ($c $rest) (decons-atom $cs)
       (if (== $c "_") (pyhost:drop-underscores $rest) (str:join "" $cs)))))

; within a generic class's type parameter scope only its type parameters are
; mangled (MANGLED is the set of them so far, or (py:None) elsewhere)
(= (pyhost:maybe-mangle $class $mangled $name)
   (if (== $mangled (py:None)) (pyhost:mangle $class $name)
     (if (is-member $name $mangled) (pyhost:mangle $class $name) $name)))

; ---------------------------------------------------------------- sets

(= (pyhost:has $set $x) (if (is-member $x $set) (py:True) (py:False)))
(= (pyhost:set-add $set $x) (sort-atom (unique-atom (cons-atom $x $set))))
(= (pyhost:set-union $a $b) (sort-atom (unique-atom (union-atom $a $b))))
(= (pyhost:set-diff $a $b) (subtraction-atom $a $b))
(= (pyhost:set-remove $set $x) (subtraction-atom $set ($x)))

; ---------------------------------------------------------------- symbols

; lists as the rules build them, (LCons x rest) ... (LNil), and as flat
; expressions, which the set operations take
(= (pyhost:to-flat $l) (case $l (((LNil) ()) ((LCons $x $rest) (cons-atom $x (pyhost:to-flat $rest))))))
(= (pyhost:to-lcons $e)
   (if (== (size-atom $e) 0) (LNil) (let ($x $rest) (decons-atom $e) (LCons $x (pyhost:to-lcons $rest)))))
(= (pyhost:empty-set) ())
(= (pyhost:set-list $set) (pyhost:to-lcons $set))

; a block's events (st:ev NAME FLAG), grouped: (st:sym NAME FLAGS) for each
; name, FLAGS the sorted set of its flags (a flat expression)
(= (pyhost:group $events)
   (pyhost:to-lcons (pyhost:group-sorted (sort-atom (unique-atom (pyhost:to-flat (pyhost:flag-events $events)))) ())))
(= (pyhost:group-sorted $evs $acc)
   (if (== (size-atom $evs) 0) $acc
     (let ($e $rest) (decons-atom $evs)
       (let (st:ev $name $flag) $e
         (pyhost:group-sorted $rest (pyhost:group-add $acc $name $flag))))))
; events arrive sorted by name, so a name's symbol is the last one made
(= (pyhost:group-add $acc $name $flag)
   (if (== (size-atom $acc) 0) ((st:sym $name ($flag)))
     (let ($first $rest) (decons-atom $acc)
       (let (st:sym $n $flags) $first
         (if (== $n $name)
             (cons-atom (st:sym $n (sort-atom (unique-atom (cons-atom $flag $flags)))) $rest)
             (cons-atom (st:sym $name ($flag)) $acc))))))

; CPython's number for a symbol: its flags' bits (distinct powers of two, so
; their sum is their union) and its scope above bit 12
(= (pyhost:flag-value (st:global)) 1)
(= (pyhost:flag-value (st:local)) 2)
(= (pyhost:flag-value (st:param)) 4)
(= (pyhost:flag-value (st:nonlocal)) 8)
(= (pyhost:flag-value (st:use)) 16)
(= (pyhost:flag-value (st:free)) 32)
(= (pyhost:flag-value (st:free-class)) 64)
(= (pyhost:flag-value (st:import)) 128)
(= (pyhost:flag-value (st:annot)) 256)
(= (pyhost:flag-value (st:comp-iter)) 512)
(= (pyhost:flag-value (st:type-param)) 1024)
(= (pyhost:flag-value (st:comp-cell)) 2048)
(= (pyhost:scope-value (st:scope-none)) 0)
(= (pyhost:scope-value (st:scope-local)) 1)
(= (pyhost:scope-value (st:scope-global-explicit)) 2)
(= (pyhost:scope-value (st:scope-global-implicit)) 3)
(= (pyhost:scope-value (st:scope-free)) 4)
(= (pyhost:scope-value (st:scope-cell)) 5)
(= (pyhost:flags-number $flags $scope) (pyhost:flags-sum $flags (* 4096 (pyhost:scope-value $scope))))
(= (pyhost:flags-sum $flags $acc)
   (if (== (size-atom $flags) 0) $acc
     (let ($f $rest) (decons-atom $flags) (pyhost:flags-sum $rest (+ $acc (pyhost:flag-value $f))))))

; the name an import binds: import a.b.c binds a
(= (pyhost:dotted-head $name) (let ($head $rest) (decons-atom (str:split "." $name)) $head))

; ---------------------------------------------------------------- names

; the names the symbol tables state
(= (pyhost:name (st:n-class)) "__class__")
(= (pyhost:name (st:n-classdict)) "__classdict__")
(= (pyhost:name (st:n-annotate)) "__annotate__")
(= (pyhost:name (st:n-dot-format)) ".format")
(= (pyhost:name (st:n-super)) "super")
(= (pyhost:name (st:n-dot0)) ".0")
(= (pyhost:name (st:n-type-params)) ".type_params")
(= (pyhost:name (st:n-generic-base)) ".generic_base")
(= (pyhost:name (st:n-defaults)) ".defaults")
(= (pyhost:name (st:n-kwdefaults)) ".kwdefaults")
(= (pyhost:name (st:n-dunder-type-params)) "__type_params__")
(= (pyhost:name (st:n-lambda)) "<lambda>")
(= (pyhost:name (st:n-listcomp)) "<listcomp>")
(= (pyhost:name (st:n-genexpr)) "<genexpr>")
(= (pyhost:name (st:n-setcomp)) "<setcomp>")
(= (pyhost:name (st:n-dictcomp)) "<dictcomp>")
(= (pyhost:name (st:n-top)) "top")
(= (pyhost:name (st:n-star)) "*")
(= (pyhost:name (st:n-cond-annotations)) "__conditional_annotations__")
(= (pyhost:name-is $name $tag) (if (== $name (pyhost:name $tag)) (py:True) (py:False)))

; whether the module's future imports (its first statements, after a
; docstring) name annotations
; whether a body annotates in its own scope (an annotated assignment
; anywhere outside nested functions and classes): a module's
; (pyhost:any-annotation), or only in a conditional block, the compound
; statements' bodies (for, while, if, match, try, with), a class's
; (pyhost:cond-annotations) (symtable_visit_annotation)
; the module with its imports marked for the symbol tables' checks: a lazy
; import in a try statement of its own block, its is_lazy (st:lazy-try); a
; from __future__ import after the module's leading ones (future_parse's
; ff_location, the last of them), its module (st:late-future)
(= (pyhost:mark-imports $body) (pyhost:mark-stmts $body false (pyhost:future-location $body)))
(= (pyhost:future-location $body) (pyhost:future-loc-scan (pyhost:skip-docstring $body) (fl -1 -1 -1 -1)))
(= (pyhost:future-loc-scan $ss $loc)
   (case $ss
     (((LCons (py:ImportFrom "__future__" $ns 0 $lz $l $c $el $ec) $rest) (pyhost:future-loc-scan $rest (fl $l $c $el $ec)))
      ($other $loc))))
(= (pyhost:mark-stmts $ss $intry $fut)
   (case $ss
     (((LNil) (LNil))
      ((LCons $s $rest)
       (let $s2 (pyhost:mark-stmt $s $intry $fut) (let $r2 (pyhost:mark-stmts $rest $intry $fut) (LCons $s2 $r2)))))))
(= (pyhost:mark-stmt $s $intry $fut)
   (case $s
     (((py:Import $ns $lz $l $c $el $ec) (let $lz2 (pyhost:lazy-mark $lz $intry) (py:Import $ns $lz2 $l $c $el $ec)))
      ((py:ImportFrom $m $ns $lv $lz $l $c $el $ec)
       (let $m2 (pyhost:future-mark $m $lv $l $c $fut) (let $lz2 (pyhost:lazy-mark $lz $intry) (py:ImportFrom $m2 $ns $lv $lz2 $l $c $el $ec))))
      ((py:FunctionDef $n $a $b $ds $r $tc $tps $l $c $el $ec)
       (let $b2 (pyhost:mark-stmts $b false $fut) (py:FunctionDef $n $a $b2 $ds $r $tc $tps $l $c $el $ec)))
      ((py:AsyncFunctionDef $n $a $b $ds $r $tc $tps $l $c $el $ec)
       (let $b2 (pyhost:mark-stmts $b false $fut) (py:AsyncFunctionDef $n $a $b2 $ds $r $tc $tps $l $c $el $ec)))
      ((py:ClassDef $n $bs $ks $b $ds $tps $l $c $el $ec)
       (let $b2 (pyhost:mark-stmts $b false $fut) (py:ClassDef $n $bs $ks $b2 $ds $tps $l $c $el $ec)))
      ((py:For $t $it $b $o $tc $l $c $el $ec)
       (let $b2 (pyhost:mark-stmts $b $intry $fut) (let $o2 (pyhost:mark-stmts $o $intry $fut) (py:For $t $it $b2 $o2 $tc $l $c $el $ec))))
      ((py:AsyncFor $t $it $b $o $tc $l $c $el $ec)
       (let $b2 (pyhost:mark-stmts $b $intry $fut) (let $o2 (pyhost:mark-stmts $o $intry $fut) (py:AsyncFor $t $it $b2 $o2 $tc $l $c $el $ec))))
      ((py:While $t $b $o $l $c $el $ec)
       (let $b2 (pyhost:mark-stmts $b $intry $fut) (let $o2 (pyhost:mark-stmts $o $intry $fut) (py:While $t $b2 $o2 $l $c $el $ec))))
      ((py:If $t $b $o $l $c $el $ec)
       (let $b2 (pyhost:mark-stmts $b $intry $fut) (let $o2 (pyhost:mark-stmts $o $intry $fut) (py:If $t $b2 $o2 $l $c $el $ec))))
      ((py:With $its $b $tc $l $c $el $ec) (let $b2 (pyhost:mark-stmts $b $intry $fut) (py:With $its $b2 $tc $l $c $el $ec)))
      ((py:AsyncWith $its $b $tc $l $c $el $ec) (let $b2 (pyhost:mark-stmts $b $intry $fut) (py:AsyncWith $its $b2 $tc $l $c $el $ec)))
      ((py:Try $b $hs $o $f $l $c $el $ec) (pyhost:mark-try py:Try $b $hs $o $f $l $c $el $ec $fut))
      ((py:TryStar $b $hs $o $f $l $c $el $ec) (pyhost:mark-try py:TryStar $b $hs $o $f $l $c $el $ec $fut))
      ((py:Match $subj $cs $l $c $el $ec) (let $cs2 (pyhost:mark-cases $cs $intry $fut) (py:Match $subj $cs2 $l $c $el $ec)))
      ($other $other))))
; a try statement: all its parts in the try block
(= (pyhost:mark-try $head $b $hs $o $f $l $c $el $ec $fut)
   (let $b2 (pyhost:mark-stmts $b true $fut)
     (let $hs2 (pyhost:mark-handlers $hs $fut)
       (let $o2 (pyhost:mark-stmts $o true $fut)
         (let $f2 (pyhost:mark-stmts $f true $fut)
           (if (== $head py:Try) (py:Try $b2 $hs2 $o2 $f2 $l $c $el $ec) (py:TryStar $b2 $hs2 $o2 $f2 $l $c $el $ec)))))))
(= (pyhost:mark-handlers $hs $fut)
   (case $hs
     (((LNil) (LNil))
      ((LCons (py:ExceptHandler $t $n $b $l $c $el $ec) $rest)
       (let $b2 (pyhost:mark-stmts $b true $fut)
         (let $r2 (pyhost:mark-handlers $rest $fut) (LCons (py:ExceptHandler $t $n $b2 $l $c $el $ec) $r2)))))))
(= (pyhost:mark-cases $cs $intry $fut)
   (case $cs
     (((LNil) (LNil))
      ((LCons (py:match_case $p $g $b) $rest)
       (let $b2 (pyhost:mark-stmts $b $intry $fut)
         (let $r2 (pyhost:mark-cases $rest $intry $fut) (LCons (py:match_case $p $g $b2) $r2)))))))
(= (pyhost:lazy-mark $lz $intry) (if (== $lz 0) $lz (if $intry (st:lazy-try) $lz)))
(= (pyhost:future-mark $m $lv $l $c $fut)
   (if (if (== $m "__future__") (== $lv 0) false)
       (let (fl $fl $fc $fel $fec) $fut
         (if (if (> $l $fl) true (if (== $l $fel) (> $c $fec) false)) (st:late-future) $m))
       $m))
(= (pyhost:lazy-kind $lz) (case $lz (((st:lazy-try) (st:lazy-try)) ($n (if (== $n 0) (st:lazy-no) (st:lazy-yes))))))
(= (pyhost:lazy-star $lk $ns) (if (== $lk (st:lazy-no)) (py:False) (pyhost:names-star $ns)))
(= (pyhost:names-star $ns)
   (case $ns
     (((LNil) (py:False))
      ((LCons (py:alias "*" $as $l $c $el $ec) $rest) (py:True))
      ((LCons $a $rest) (pyhost:names-star $rest)))))
(= (pyhost:late-future $m) (if (== $m (st:late-future)) (py:True) (py:False)))
(= (pyhost:any-annotation $body) (pyhost:ann-in-stmts $body true))
(= (pyhost:cond-annotations $body) (pyhost:ann-in-stmts $body false))
(= (pyhost:ann-in-stmts $ss $cond)
   (case $ss
     (((LNil) (py:False))
      ((LCons $s $rest) (if (== (pyhost:ann-in-stmt $s $cond) (py:True)) (py:True) (pyhost:ann-in-stmts $rest $cond))))))
(= (pyhost:ann-in-stmt $s $cond)
   (case $s
     (((py:AnnAssign $t $an $v $simple $l $c $el $ec) (if $cond (py:True) (py:False)))
      ((py:For $t $it $b $o $tc $l $c $el $ec) (pyhost:ann-in-pair $b $o))
      ((py:AsyncFor $t $it $b $o $tc $l $c $el $ec) (pyhost:ann-in-pair $b $o))
      ((py:While $t $b $o $l $c $el $ec) (pyhost:ann-in-pair $b $o))
      ((py:If $t $b $o $l $c $el $ec) (pyhost:ann-in-pair $b $o))
      ((py:With $its $b $tc $l $c $el $ec) (pyhost:ann-in-stmts $b true))
      ((py:AsyncWith $its $b $tc $l $c $el $ec) (pyhost:ann-in-stmts $b true))
      ((py:Try $b $hs $o $f $l $c $el $ec) (pyhost:ann-in-try $b $hs $o $f))
      ((py:TryStar $b $hs $o $f $l $c $el $ec) (pyhost:ann-in-try $b $hs $o $f))
      ((py:Match $subj $cases $l $c $el $ec) (pyhost:ann-in-cases $cases))
      ($other (py:False)))))
(= (pyhost:ann-in-pair $b $o)
   (if (== (pyhost:ann-in-stmts $b true) (py:True)) (py:True) (pyhost:ann-in-stmts $o true)))
(= (pyhost:ann-in-try $b $hs $o $f)
   (if (== (pyhost:ann-in-stmts $b true) (py:True)) (py:True)
     (if (== (pyhost:ann-in-handlers $hs) (py:True)) (py:True) (pyhost:ann-in-pair $o $f))))
(= (pyhost:ann-in-handlers $hs)
   (case $hs
     (((LNil) (py:False))
      ((LCons (py:ExceptHandler $t $n $b $l $c $el $ec) $rest)
       (if (== (pyhost:ann-in-stmts $b true) (py:True)) (py:True) (pyhost:ann-in-handlers $rest))))))
(= (pyhost:ann-in-cases $cs)
   (case $cs
     (((LNil) (py:False))
      ((LCons (py:match_case $p $g $b) $rest)
       (if (== (pyhost:ann-in-stmts $b true) (py:True)) (py:True) (pyhost:ann-in-cases $rest))))))
(= (pyhost:future-annotations $body) (pyhost:future-scan (pyhost:skip-docstring $body)))
(= (pyhost:skip-docstring $body)
   (case $body
     (((LCons (py:Expr (py:Constant $v $k $l $c $el $ec) $l2 $c2 $el2 $ec2) $rest)
       (if (== (get-metatype $v) Grounded) (if (== (get-type $v) String) $rest $body) $body))
      ($other $body))))
(= (pyhost:future-scan $body)
   (case $body
     (((LCons (py:ImportFrom "__future__" $names 0 0 $l $c $el $ec) $rest)
       (if (pyhost:names-annotations $names) (py:True) (pyhost:future-scan $rest)))
      ($other (py:False)))))
(= (pyhost:names-annotations $names)
   (case $names
     (((LCons (py:alias "annotations" $as $l $c $el $ec) $rest) true)
      ((LCons $a $rest) (pyhost:names-annotations $rest))
      ($other false))))

; a symbol's flags in a list of (st:sym NAME FLAGS) or (st:asym NAME FLAGS
; SCOPE), or (py:None); a symbol's scope in a list of (st:fsym NAME FLAGS
; SCOPE), or (py:None)
(= (pyhost:lookup $syms $name)
   (case $syms
     (((LNil) (py:None))
      ((LCons (st:sym $n $f) $rest) (if (== $n $name) $f (pyhost:lookup $rest $name)))
      ((LCons (st:asym $n $f $s) $rest) (if (== $n $name) $f (pyhost:lookup $rest $name)))
      ((LCons (st:fsym $n $f $s) $rest) (if (== $n $name) $f (pyhost:lookup $rest $name))))))

; a final symbol's flags with one more flag
(= (pyhost:add-flag $syms $name $flag)
   (case $syms
     (((LNil) (LNil))
      ((LCons (st:fsym $n $f $s) $rest)
       (if (== $n $name)
           (LCons (st:fsym $n (pyhost:set-add $f $flag) $s) $rest)
           (LCons (st:fsym $n $f $s) (pyhost:add-flag $rest $name $flag))))
      ((LCons (st:asym $n $f $s) $rest)
       (if (== $n $name)
           (LCons (st:asym $n (pyhost:set-add $f $flag) $s) $rest)
           (LCons (st:asym $n $f $s) (pyhost:add-flag $rest $name $flag)))))))
(= (pyhost:lookup-scope $syms $name)
   (case $syms
     (((LNil) (py:None))
      ((LCons (st:fsym $n $f $s) $rest) (if (== $n $name) $s (pyhost:lookup-scope $rest $name))))))

; where an expression starts: the first two of its four positions
(= (pyhost:expr-pos $e) (st:pos (index-atom $e (- (size-atom $e) 4)) (index-atom $e (- (size-atom $e) 3))))

; whether an optional field is absent; whether a number is nonzero
(= (pyhost:is-none $x) (if (== $x (py:None)) (py:True) (py:False)))
(= (pyhost:truthy $x) (if (== $x 0) (py:False) (py:True)))

; ---------------------------------------------------------------- errors

; a block's group of events leaves out its directives: a directive (where a
; global or nonlocal declaration, or a comprehension's named expression,
; stands) is no flag
(= (pyhost:is-flag-event (st:ev $n (st:directive $l $c $el $ec))) false)
(= (pyhost:is-flag-event (st:ev $n (st:local))) true)
(= (pyhost:is-flag-event (st:ev $n (st:param))) true)
(= (pyhost:is-flag-event (st:ev $n (st:global))) true)
(= (pyhost:is-flag-event (st:ev $n (st:nonlocal))) true)
(= (pyhost:is-flag-event (st:ev $n (st:use))) true)
(= (pyhost:is-flag-event (st:ev $n (st:import))) true)
(= (pyhost:is-flag-event (st:ev $n (st:annot))) true)
(= (pyhost:is-flag-event (st:ev $n (st:comp-iter))) true)
(= (pyhost:is-flag-event (st:ev $n (st:type-param))) true)
(= (pyhost:is-flag-event (st:ev $n (st:walrus))) true)
(= (pyhost:flag-events $events)
   (case $events
     (((LNil) (LNil))
      ((LCons $e $rest)
       (if (pyhost:is-flag-event $e) (LCons $e (pyhost:flag-events $rest)) (pyhost:flag-events $rest))))))

; a name's flags in the block being read, as they stand: its events so far,
; and what the children read so far stated of it (MODE: py:fun the facts
; stated of the nearest function, py:mod of the module, py:none none)
(= (pyhost:flags-now $ev $es $mode $name)
   (pyhost:flags-escaped $es (pyhost:escape-target $mode) $name (pyhost:flags-events $ev $name ())))
(= (pyhost:escape-target (py:fun)) (st:to-function))
(= (pyhost:escape-target (py:mod)) (st:to-module))
(= (pyhost:escape-target (py:none)) (py:None))
(= (pyhost:flags-events $ev $name $acc)
   (case $ev
     (((LNil) $acc)
      ((LCons (st:ev $n $f) $rest)
       (pyhost:flags-events $rest $name (if (== $n $name) (pyhost:flag-join $acc $f) $acc))))))
(= (pyhost:flags-escaped $es $target $name $acc)
   (case $es
     (((LNil) $acc)
      ((LCons (st:esc $t $n $f) $rest)
       (pyhost:flags-escaped $rest $target $name
         (if (== $t $target) (if (== $n $name) (pyhost:flag-join $acc $f) $acc) $acc)))
      ((LCons $other $rest) (pyhost:flags-escaped $rest $target $name $acc)))))
(= (pyhost:flag-join $acc $f)
   (case $f (((st:directive $l $c $el $ec) $acc) ($flag (pyhost:set-add $acc $flag)))))

; the names a comprehension's events so far make iteration variables
(= (pyhost:comp-iters $ev) (pyhost:comp-iters-acc $ev ()))
(= (pyhost:comp-iters-acc $ev $acc)
   (case $ev
     (((LNil) $acc)
      ((LCons (st:ev $n (st:comp-iter)) $rest) (pyhost:comp-iters-acc $rest (pyhost:set-add $acc $n)))
      ((LCons $other $rest) (pyhost:comp-iters-acc $rest $acc)))))

; where the first directive naming NAME stands, (st:at L C EL EC), or
; (py:None) (the events are newest first)
(= (pyhost:directive $ev $name) (pyhost:directive-acc $ev $name (py:None)))
(= (pyhost:directive-acc $ev $name $found)
   (case $ev
     (((LNil) $found)
      ((LCons (st:ev $n (st:directive $l $c $el $ec)) $rest)
       (pyhost:directive-acc $rest $name (if (== $n $name) (st:at $l $c $el $ec) $found)))
      ((LCons $other $rest) (pyhost:directive-acc $rest $name $found)))))

; of a block's symbols whose scope is an error, (st:bad-at NAME TAG), the one
; whose name entered the block first (CPython reads a block's symbols in the
; order they entered it), or (py:None)
(= (pyhost:first-bad $asyms $ev)
   (let $bads (pyhost:bad-names $asyms)
     (if (== (size-atom $bads) 0) (py:None) (pyhost:pick-entered $bads $ev (py:None) -1))))
(= (pyhost:bad-names $asyms)
   (case $asyms
     (((LNil) ())
      ((LCons (st:asym $n $f (st:bad $tag)) $rest) (cons-atom (st:bad-at $n $tag) (pyhost:bad-names $rest)))
      ((LCons $other $rest) (pyhost:bad-names $rest)))))
(= (pyhost:pick-entered $bads $ev $best $bestpos)
   (if (== (size-atom $bads) 0) $best
     (let ($b $rest) (decons-atom $bads)
       (let (st:bad-at $n $tag) $b
         (let $p (pyhost:last-index $ev $n 0 -1)
           (if (> $p $bestpos)
               (pyhost:pick-entered $rest $ev $b $p)
               (pyhost:pick-entered $rest $ev $best $bestpos)))))))
(= (pyhost:last-index $ev $name $i $found)
   (case $ev
     (((LNil) $found)
      ((LCons (st:ev $n $f) $rest) (pyhost:last-index $rest $name (+ $i 1) (if (== $n $name) $i $found))))))

; a TypeVar's bound: a constraint when it is a tuple
(= (pyhost:bound-info $b) (if (== (car-atom $b) py:Tuple) (st:i-constraint) (st:i-bound)))

; CPython's messages
(= (pyhost:quoted $n) (str:concat (str:concat "'" $n) "'"))
(= (pyhost:named $n $tail) (str:concat (str:concat "name " (pyhost:quoted $n)) $tail))
(= (pyhost:what (st:w-named)) "named expression")
(= (pyhost:what (st:w-yield)) "yield expression")
(= (pyhost:what (st:w-await)) "await expression")
(= (pyhost:info (st:i-bound)) "a TypeVar bound")
(= (pyhost:info (st:i-constraint)) "a TypeVar constraint")
(= (pyhost:info (st:i-typevar-default)) "a TypeVar default")
(= (pyhost:info (st:i-typevartuple-default)) "a TypeVarTuple default")
(= (pyhost:info (st:i-paramspec-default)) "a ParamSpec default")
(= (pyhost:decl-text (st:d-param)) " is parameter and ")
(= (pyhost:decl-text (st:d-use)) " is used prior to ")
(= (pyhost:decl-text (st:d-local)) " is assigned to before ")
(= (pyhost:decl-word (st:global)) "global")
(= (pyhost:decl-word (st:nonlocal)) "nonlocal")
(= (pyhost:decl-tail (st:d-param) $d) (pyhost:decl-word $d))
(= (pyhost:decl-tail (st:d-use) $d) (str:concat (pyhost:decl-word $d) " declaration"))
(= (pyhost:decl-tail (st:d-local) $d) (str:concat (pyhost:decl-word $d) " declaration"))
(= (pyhost:message (st:m-decl $d (st:d-annot)) $n $b)
   (str:concat (str:concat "annotated name " (pyhost:quoted $n)) (str:concat " can't be " (pyhost:decl-word $d))))
(= (pyhost:message (st:m-decl $d (st:d-param)) $n $b) (pyhost:named $n (str:concat (pyhost:decl-text (st:d-param)) (pyhost:decl-tail (st:d-param) $d))))
(= (pyhost:message (st:m-decl $d (st:d-use)) $n $b) (pyhost:named $n (str:concat (pyhost:decl-text (st:d-use)) (pyhost:decl-tail (st:d-use) $d))))
(= (pyhost:message (st:m-decl $d (st:d-local)) $n $b) (pyhost:named $n (str:concat (pyhost:decl-text (st:d-local)) (pyhost:decl-tail (st:d-local) $d))))
(= (pyhost:message (st:m-dup-arg) $n $b) (str:concat (str:concat "duplicate argument " (pyhost:quoted $n)) " in function definition"))
(= (pyhost:message (st:m-dup-tp) $n $b) (str:concat "duplicate type parameter " (pyhost:quoted $n)))
(= (pyhost:message (st:m-inner-loop) $n $b) (str:concat "comprehension inner loop cannot rebind assignment expression target " (pyhost:quoted $n)))
(= (pyhost:message (st:m-import-star) $a $b) "import * only allowed at module level")
(= (pyhost:message (st:m-walrus-class) $a $b) "assignment expression within a comprehension cannot be used in a class body")
(= (pyhost:message (st:m-walrus-typevar) $a $b) "assignment expression within a comprehension cannot be used in a TypeVar bound")
(= (pyhost:message (st:m-lazy-try) (st:lz-import) $b) "lazy import not allowed inside try/except blocks")
(= (pyhost:message (st:m-lazy-try) (st:lz-from) $b) "lazy from ... import not allowed inside try/except blocks")
(= (pyhost:message (st:m-lazy-function) (st:lz-import) $b) "lazy import not allowed inside functions")
(= (pyhost:message (st:m-lazy-function) (st:lz-from) $b) "lazy from ... import not allowed inside functions")
(= (pyhost:message (st:m-lazy-class) (st:lz-import) $b) "lazy import not allowed inside classes")
(= (pyhost:message (st:m-lazy-class) (st:lz-from) $b) "lazy from ... import not allowed inside classes")
(= (pyhost:message (st:m-lazy-star) $a $b) "lazy from ... import * is not allowed")
(= (pyhost:message (st:m-late-future) $a $b) "from __future__ imports must occur at the beginning of the file")
(= (pyhost:message (st:m-walrus-typealias) $a $b) "assignment expression within a comprehension cannot be used in a type alias")
(= (pyhost:message (st:m-walrus-typeparam) $a $b) "assignment expression within a comprehension cannot be used within the definition of a generic")
(= (pyhost:message (st:m-walrus-conflict) $n $b) (str:concat "assignment expression cannot rebind comprehension iteration variable " (pyhost:quoted $n)))
(= (pyhost:message (st:m-walrus-iter) $a $b) "assignment expression cannot be used in a comprehension iterable expression")
(= (pyhost:message (st:m-in-annotation) $w $b) (str:concat (pyhost:what $w) " cannot be used within an annotation"))
(= (pyhost:message (st:m-in-typevar) $w $i) (str:concat (str:concat (pyhost:what $w) " cannot be used within ") (pyhost:info $i)))
(= (pyhost:message (st:m-in-alias) $w $b) (str:concat (pyhost:what $w) " cannot be used within a type alias"))
(= (pyhost:message (st:m-in-generic) $w $b) (str:concat (pyhost:what $w) " cannot be used within the definition of a generic"))
(= (pyhost:message (st:m-reserved) $n $b) (str:concat (str:concat "reserved name " (pyhost:quoted $n)) " cannot be used for type parameter"))
(= (pyhost:message (st:m-yield-comp) (st:n-listcomp) $b) "'yield' inside list comprehension")
(= (pyhost:message (st:m-yield-comp) (st:n-setcomp) $b) "'yield' inside set comprehension")
(= (pyhost:message (st:m-yield-comp) (st:n-dictcomp) $b) "'yield' inside dict comprehension")
(= (pyhost:message (st:m-yield-comp) (st:n-genexpr) $b) "'yield' inside generator expression")
(= (pyhost:message (st:m-nonlocal-global) $n $b) (pyhost:named $n " is nonlocal and global"))
(= (pyhost:message (st:m-nonlocal-module) $n $b) "nonlocal declaration not allowed at module level")
(= (pyhost:message (st:m-no-binding) $n $b) (str:concat (str:concat "no binding for nonlocal " (pyhost:quoted $n)) " found"))
(= (pyhost:message (st:m-nonlocal-tp) $n $b) (str:concat "nonlocal binding not allowed for type parameter " (pyhost:quoted $n)))

; the error the module's future statements (its first statements, after a
; docstring) raise, (st:err TEXT L C EL EC), or (py:None): a feature CPython
; does not know, or braces
(= (pyhost:future-error $body) (pyhost:future-error-scan (pyhost:skip-docstring $body)))
(= (pyhost:future-error-scan $body)
   (case $body
     (((LCons (py:ImportFrom "__future__" $names 0 0 $l $c $el $ec) $rest)
       (let $e (pyhost:future-names-error $names)
         (if (== $e (py:None)) (pyhost:future-error-scan $rest) $e)))
      ($other (py:None)))))
(= (pyhost:future-names-error $names)
   (case $names
     (((LNil) (py:None))
      ((LCons (py:alias $n $as $l $c $el $ec) $rest)
       (if (pyhost:future-known $n)
           (pyhost:future-names-error $rest)
           (st:err (pyhost:future-text $n) $l $c $el $ec))))))
(= (pyhost:future-known $n)
   (is-member $n ("nested_scopes" "generators" "division" "absolute_import" "with_statement"
                  "print_function" "unicode_literals" "barry_as_FLUFL" "generator_stop" "annotations")))
(= (pyhost:future-text $n)
   (if (== $n "braces") "not a chance"
     (str:concat (str:concat "future feature " (pyhost:first-bytes $n 100)) " is not defined")))
; at most N bytes of a text, as C's %.Ns prints it (a character cut short is
; U+FFFD)
(= (pyhost:first-bytes $text $n)
   (if (> (str:byte-length $text) $n) (pyhost:cut-short (str:byte-slice $text 0 $n)) $text))
(= (pyhost:cut-short $bytes)
   (let $decoder (str:utf8-new ignore)
   (let (Utf8DecodedV1 $whole (Utf8DecoderV1 $policy $tail $seen)) (str:utf8-feed $decoder $bytes false)
     (if (== $tail "") $whole (str:concat $whole "\uFFFD")))))
