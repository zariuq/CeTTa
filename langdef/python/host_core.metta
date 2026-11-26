; What Core.module asks the host: the names and constants the core states,
; a function's fast locals in slot order, and the table of a block by where
; it stands.  Lists are (LCons x rest) ... (LNil).
!(import! &self str)
!(import! &self float)
!(import! &self integer)
; the runtime's operations on builtin values, which constant folding applies
!(import! &self host_runtime)

; the runtime's constants: Python's booleans are CeTTa's; None, Ellipsis and
; the numbers and texts stay as the abstract syntax has them
; a float literal beyond the largest double stands in the abstract syntax as
; the symbol inf (as ast.dump writes it); its value is the float infinity
(= (pyhost:core-value $v) (case $v (((py:True) True) ((py:False) False) (inf (float:div 1.0 0.0)) ($other $other))))
(= (pyhost:core-none) (py:None))
(= (pyhost:core-name (c:n-module)) "<module>")
(= (pyhost:core-name (c:n-empty)) "")
(= (pyhost:core-name (c:n-lambda)) "<lambda>")
(= (pyhost:core-name (c:n-annotate)) "__annotate__")
(= (pyhost:core-name (c:n-annotate-func)) "__annotate_func__")
(= (pyhost:core-name (c:n-cond-annotations)) "__conditional_annotations__")
(= (pyhost:core-name (c:n-dunder-module)) "__module__")
(= (pyhost:core-name (c:n-dunder-name)) "__name__")
(= (pyhost:core-name (c:n-dunder-qualname)) "__qualname__")
(= (pyhost:core-name (c:n-dunder-firstlineno)) "__firstlineno__")
(= (pyhost:core-name (c:n-dunder-doc)) "__doc__")
(= (pyhost:core-name (c:n-dunder-annotations)) "__annotations__")
(= (pyhost:core-name (c:n-genexpr)) "<genexpr>")
(= (pyhost:core-name (c:n-dot0)) ".0")
(= (pyhost:core-name (c:n-dunder-static-attributes)) "__static_attributes__")
(= (pyhost:core-name (c:n-dunder-classcell)) "__classcell__")
(= (pyhost:core-name (c:n-dot-defaults)) ".defaults")
(= (pyhost:core-name (c:n-dot-kwdefaults)) ".kwdefaults")
(= (pyhost:core-name (c:n-dot-type-params)) ".type_params")
(= (pyhost:core-name (c:n-dunder-type-params)) "__type_params__")
(= (pyhost:core-name (c:n-dot-generic-base)) ".generic_base")
(= (pyhost:core-name (c:n-dunder-classdictcell)) "__classdictcell__")
; the compiler's errors (compiler_error in compile.c)
(= (pyhost:core-name (c:e-break)) "'break' outside loop")
(= (pyhost:core-name (c:e-continue)) "'continue' not properly in loop")
(= (pyhost:core-name (c:e-return)) "'return' outside function")
(= (pyhost:core-name (c:e-starred-target)) "starred assignment target must be in a list or tuple")
(= (pyhost:core-name (c:e-starred-here)) "can't use starred expression here")
(= (pyhost:core-name (c:e-many-starred)) "multiple starred expressions in assignment")
(= (pyhost:core-name (c:e-too-many-starred)) "too many expressions in star-unpacking assignment")
(= (pyhost:core-name (c:e-yield)) "'yield' outside function")
(= (pyhost:core-name (c:e-yield-from)) "'yield from' outside function")
(= (pyhost:core-name (c:e-yield-from-async)) "'yield from' inside async function")
(= (pyhost:core-name (c:e-await)) "'await' outside function")
(= (pyhost:core-name (c:e-await-sync)) "'await' outside async function")
(= (pyhost:core-name (c:e-async-for)) "'async for' outside async function")
(= (pyhost:core-name (c:e-async-with)) "'async with' outside async function")
(= (pyhost:core-name (c:e-return-async-gen)) "'return' with value in async generator")
(= (pyhost:core-name (c:e-async-comp)) "asynchronous comprehension outside of an asynchronous function")
; the runtime's constants the core states
(= (pyhost:core-name (c:t-false)) False)
(= (pyhost:core-name (c:t-true)) True)
(= (pyhost:core-name (c:t-type)) (py:type type))
(= (pyhost:core-name (c:t-stop-async-iteration)) (py:type StopAsyncIteration))

; a name's slot: its position in the slot list
(= (pyhost:slot $slots $name) (pyhost:slot-at $slots $name 0))
(= (pyhost:slot-at $slots $name $i)
   (case $slots
     (((LCons $n $rest) (if (== $n $name) $i (pyhost:slot-at $rest $name (+ $i 1))))
      ((LNil) (empty)))))
(= (pyhost:slot-count $slots)
   (case $slots (((LNil) 0) ((LCons $x $rest) (+ 1 (pyhost:slot-count $rest))))))
; one list after another; whether a name is in a list
(= (pyhost:append-list $a $b)
   (case $a (((LNil) $b) ((LCons $x $rest) (LCons $x (pyhost:append-list $rest $b))))))
(= (pyhost:in-list $x $l)
   (case $l (((LNil) false) ((LCons $y $rest) (if (== $x $y) true (pyhost:in-list $x $rest))))))

; a function's frame: (c:layout SLOTS SPEC CELLS FREES FREENAMES).  SLOTS: the
; positional-only, positional and keyword-only parameters, *args, **kwargs, the
; other locals, the cells that are no parameter, the free names (CPython's
; co_varnames, cellvars and freevars, each of the last three sorted); SPEC
; (c:args NPOSONLY NPOS NKWONLY STAR DSTAR SLOTS); CELLS the slots of cell
; names; FREES the slots of the free names, in the closure's order
(= (pyhost:code-layout (py:arguments $po $pa $va $ko $kd $kw $ds) (st:table $k $n $p $syms $tabs) $private)
   (let $posonly (pyhost:mangle-all $private (pyhost:arg-names $po))
   (let $pos (pyhost:mangle-all $private (pyhost:arg-names $pa))
   (let $kwonly (pyhost:mangle-all $private (pyhost:arg-names $ko))
   (let $star (pyhost:mangle-all $private (pyhost:opt-arg-name $va))
   (let $dstar (pyhost:mangle-all $private (pyhost:opt-arg-name $kw))
   (let $params (pyhost:append-list $posonly (pyhost:append-list $pos (pyhost:append-list $kwonly (pyhost:append-list $star $dstar))))
   (let $locals (pyhost:to-lcons (sort-atom (pyhost:names-of-scope $syms (st:scope-local) $params)))
   (let $cellnames (pyhost:to-lcons (sort-atom (pyhost:names-of-scope $syms (st:scope-cell) $params)))
   (let $freenames (pyhost:to-lcons (sort-atom (pyhost:names-of-scope $syms (st:scope-free) (LNil))))
   (let $slots (pyhost:append-list $params (pyhost:append-list $locals (pyhost:append-list $cellnames $freenames)))
     (c:layout $slots
       (c:args (pyhost:slot-count $posonly) (pyhost:slot-count $pos) (pyhost:slot-count $kwonly)
               (pyhost:non-empty $star) (pyhost:non-empty $dstar) $slots)
       (pyhost:cell-slots $slots $syms 0)
       (pyhost:slots-of $freenames $slots)
       $freenames))))))))))))
(= (pyhost:arg-names $args)
   (case $args
     (((LNil) (LNil))
      ((LCons (py:arg $n $an $tc $l $c $el $ec) $rest) (LCons $n (pyhost:arg-names $rest))))))
(= (pyhost:opt-arg-name $a) (case $a (((py:arg $n $an $tc $l $c $el $ec) (LCons $n (LNil))) ($none (LNil)))))
(= (pyhost:non-empty $l) (case $l (((LNil) (py:False)) ($other (py:True)))))
; names as the symbol table holds them: mangled by the private name
; (pyhost:mangle, of the scope stage's host, which the core stage loads)
(= (pyhost:mangle-all $private $names)
   (case $names
     (((LNil) (LNil))
      ((LCons $n $rest) (LCons (pyhost:mangle $private $n) (pyhost:mangle-all $private $rest))))))
; the names with SCOPE that are not in EXCLUDE, as a flat expression
(= (pyhost:names-of-scope $syms $scope $exclude)
   (case $syms
     (((LNil) ())
      ((LCons (st:fsym $n $f $s) $rest)
       (if (== $s $scope)
           (if (pyhost:in-list $n $exclude) (pyhost:names-of-scope $rest $scope $exclude) (cons-atom $n (pyhost:names-of-scope $rest $scope $exclude)))
           (pyhost:names-of-scope $rest $scope $exclude))))))
(= (pyhost:cell-slots $slots $syms $i)
   (case $slots
     (((LNil) (LNil))
      ((LCons $n $rest)
       (if (== (pyhost:lookup-scope $syms $n) (st:scope-cell))
           (LCons $i (pyhost:cell-slots $rest $syms (+ $i 1)))
           (pyhost:cell-slots $rest $syms (+ $i 1)))))))
(= (pyhost:slots-of $names $slots)
   (case $names (((LNil) (LNil)) ((LCons $n $rest) (LCons (pyhost:slot $slots $n) (pyhost:slots-of $rest $slots))))))
; a function's qualified name, and the prefix of the names it holds
(= (pyhost:qualname $prefix $name) (str:concat $prefix $name))
(= (pyhost:qualname-inner $qn) (str:concat $qn ".<locals>."))

; the table of a block of KIND that stands at POS, among a table's children
(= (pyhost:child-table $tabs $kind $pos)
   (case $tabs
     (((LNil) (empty))
      ((LCons (st:table $k $n $p $syms $kids) $rest)
       (if (== $k $kind)
           (if (== $p $pos) (st:table $k $n $p $syms $kids) (pyhost:child-table $rest $kind $pos))
           (pyhost:child-table $rest $kind $pos)))
      ((LCons (st:inlined $t) $rest) (pyhost:child-table $rest $kind $pos)))))
; an inlined comprehension's own table, by where it stands
(= (pyhost:comp-table $tabs $pos)
   (case $tabs
     (((LNil) (empty))
      ((LCons (st:inlined (st:table $k $n $p $syms $kids)) $rest)
       (if (== $p $pos) (st:table $k $n $p $syms $kids) (pyhost:comp-table $rest $pos)))
      ((LCons $t $rest) (pyhost:comp-table $rest $pos)))))
; the names an inlined comprehension sees, as CPython's compiler arranges them
; (push_inlined_comprehension_state): each name it binds is its own, in a new
; temporary, a new cell when it is a cell; in a class body its free names are
; the class's cells and its other names globals, never the class's; any other
; name is its block's.  (c:comp-names SYMBOLS CELLS NEXT): symbols that shadow
; the block's, the cells to make, the next free slot
(= (pyhost:comp-names $kind $syms $slots $n)
   (case $syms
     (((LNil) (c:comp-names (LNil) (LNil) $n))
      ((LCons (st:fsym $name $f $s) $rest)
       (if (is-member (st:param) $f) (pyhost:comp-names $kind $rest $slots $n)
         (if (pyhost:comp-own $f $s)
             (let (c:comp-names $p $i $n2) (pyhost:comp-names $kind $rest $slots (+ $n 1))
               (if (== $s (st:scope-cell))
                   (c:comp-names (LCons (st:fsym $name $f (c:renamed (c:cell $n))) $p)
                                 (LCons (c:assign (c:tmp $n) (c:new-cell)) $i) $n2)
                   (c:comp-names (LCons (st:fsym $name $f (c:renamed (c:tmp $n))) $p) $i $n2)))
             (if (== $kind (c:class))
                 (let (c:comp-names $p $i $n2) (pyhost:comp-names $kind $rest $slots $n)
                   (c:comp-names (LCons (st:fsym $name $f (pyhost:class-comp-scope $s $name $slots)) $p) $i $n2))
                 (pyhost:comp-names $kind $rest $slots $n))))))))
; a name the comprehension binds (a walrus target is its block's: nonlocal
; there, or global at a module's level)
(= (pyhost:comp-own $f $s)
   (if (is-member (st:local) $f)
       (if (is-member (st:nonlocal) $f) false (if (== $s (st:scope-local)) true (== $s (st:scope-cell))))
       false))
(= (pyhost:class-comp-scope $s $name $slots)
   (if (== $s (st:scope-free)) (c:renamed (c:cell (pyhost:slot $slots $name))) (st:scope-global-explicit)))

; ---------------------------------------------------------------- classes

(= (pyhost:class-inner $qn) (str:concat $qn "."))
; a class body's frame: a __class__ cell when a function in it uses __class__
; (or super), then its free names, sorted; its own names live in its
; namespace, not in slots
(= (pyhost:class-layout (st:table $k $n $p $syms $tabs) $body)
   (let $cellnames (pyhost:class-cell-names (pyhost:children-use $tabs "__class__") (pyhost:children-use $tabs "__classdict__")
                                            (if (== (pyhost:cond-annotations $body) (py:True)) true (pyhost:children-use $tabs "__conditional_annotations__")))
     (let $freenames (pyhost:to-lcons (sort-atom (pyhost:class-free-names $syms)))
       (let $slots (pyhost:append-list $cellnames $freenames)
         (c:layout $slots (c:args 0 0 0 (py:False) (py:False) $slots)
                   (pyhost:slots-of $cellnames $slots) (pyhost:slots-of $freenames $slots) $freenames)))))
; a class's implicit cells (compiler_enter_scope): __class__ when a child has
; it free (ste_needs_class_closure), then __classdict__ when a child has it
; free (ste_needs_classdict: a scope in it that sees the class), then
; __conditional_annotations__ when it annotates in a conditional block or a
; child has it free (ste_has_conditional_annotations)
(= (pyhost:class-cell-names $uses-class $uses-classdict $cond)
   (let $t2 (if $cond (LCons "__conditional_annotations__" (LNil)) (LNil))
     (let $tail (if $uses-classdict (LCons "__classdict__" $t2) $t2)
       (if $uses-class (LCons "__class__" $tail) $tail))))
(= (pyhost:children-use $tabs $name)
   (case $tabs
     (((LNil) false)
      ((LCons (st:table $k $n $p $syms $kids) $rest)
       (if (== (pyhost:lookup-scope $syms $name) (st:scope-free)) true (pyhost:children-use $rest $name)))
      ((LCons (st:inlined $t) $rest) (pyhost:children-use $rest $name)))))
; a class body's own cells, by name: (c:class-cells CLASS CLASSDICT CONDITIONAL)
(= (pyhost:class-cells $t $body)
   (let (c:layout $slots $spec $cells $frees $fnames) (pyhost:class-layout $t $body)
     (c:class-cells (pyhost:cell-of $slots "__class__") (pyhost:cell-of $slots "__classdict__") (pyhost:cell-of $slots "__conditional_annotations__"))))
; a name a closure takes from its block (_PyCompile_GetRefType): a class's
; __class__, __classdict__ and __conditional_annotations__ are always its own
; cells, whatever its symbol table says of them
(= (pyhost:closure-scope $kind $syms $nm)
   (if (if (== $kind (c:class)) (pyhost:in-list $nm (LCons "__class__" (LCons "__classdict__" (LCons "__conditional_annotations__" (LNil))))) false)
       (py:None)
       (pyhost:lookup-scope $syms $nm)))
(= (pyhost:classdict-slot $slots) (pyhost:cell-of $slots "__classdict__"))
(= (pyhost:cell-of $slots $name)
   (let $found (collapse (pyhost:slot $slots $name))
     (if (== $found ()) (c:no-cell) (c:cell (car-atom $found)))))
; a class body's free names (dictbytype(FREE, DEF_FREE_CLASS)): those free in
; it and those it binds that a child takes from an enclosing function
(= (pyhost:class-free-names $syms)
   (case $syms
     (((LNil) ())
      ((LCons (st:fsym $n $f $s) $rest)
       (if (if (== $s (st:scope-free)) true (is-member (st:free-class) $f))
           (cons-atom $n (pyhost:class-free-names $rest))
           (pyhost:class-free-names $rest))))))
; a body's docstring: its first statement, when that is a string constant
(= (pyhost:docstring $body)
   (case $body
     (((LCons (py:Expr (py:Constant $v $kind $l $c $el $ec) $l2 $c2 $el2 $ec2) $rest)
       (if (pyhost:is-text $v) (c:doc $v $rest) (c:no-doc)))
      ($other (c:no-doc)))))
(= (pyhost:is-text $v) (if (== (get-metatype $v) Grounded) (== (get-type $v) String) false))
; a class's first line: its first decorator's, else its own
(= (pyhost:first-line $ds $l)
   (case $ds (((LCons $d $rest) (index-atom $d (- (size-atom $d) 4))) ($none $l))))
; __static_attributes__: the names X of the attributes self.X stored, sorted,
; each once (CPython's compiler_maybe_add_static_attribute_to_class).  A store
; is the class's that most nearly encloses its compile unit, the unit itself
; excluded.  From the class's own body (own), a function, a lambda or a
; generator expression is a unit below it (inner); the body of a class met
; there is a unit whose nearest class is this one (classbody), and the units
; within that body are its class's.  List, set and dict comprehensions are
; inlined (PEP 709), no unit of their own.  An augmented assignment's target
; and del are not stores here.
(= (pyhost:static-attributes $body)
   (cons-atom py:tuple (pyhost:distinct-sorted (sort-atom (pyhost:self-stores $body own ())) ())))
(= (pyhost:distinct-sorted $xs $last)
   (if (== (size-atom $xs) 0) ()
     (let ($x $rest) (decons-atom $xs)
       (if (== $x $last) (pyhost:distinct-sorted $rest $last) (cons-atom $x (pyhost:distinct-sorted $rest $x))))))
(= (pyhost:self-stores $term $mode $acc)
   (if (== (get-metatype $term) Expression)
       (if (== (size-atom $term) 0) $acc
         (let $h (car-atom $term)
           (case $h
             ((py:FunctionDef (pyhost:self-stores-unit $term $mode $acc))
              (py:AsyncFunctionDef (pyhost:self-stores-unit $term $mode $acc))
              (py:Lambda (pyhost:self-stores-unit $term $mode $acc))
              (py:GeneratorExp (pyhost:self-stores-unit $term $mode $acc))
              (py:ClassDef (if (== $mode classbody) $acc (pyhost:self-stores-all (cdr-atom $term) classbody $acc)))
              (py:AugAssign (pyhost:self-stores-aug (cdr-atom $term) $mode $acc))
              (py:Attribute (pyhost:self-store $term $mode $acc))
              ($other (pyhost:self-stores-all (cdr-atom $term) $mode $acc))))))
       $acc))
; a function-like unit: ours below our own body or one of our units; the
; nested class's within that class's body
(= (pyhost:self-stores-unit $term $mode $acc)
   (if (== $mode classbody) $acc (pyhost:self-stores-all (cdr-atom $term) inner $acc)))
(= (pyhost:self-stores-all $terms $mode $acc)
   (if (== (size-atom $terms) 0) $acc
     (let ($t $rest) (decons-atom $terms)
       (pyhost:self-stores-all $rest $mode (pyhost:self-stores $t $mode $acc)))))
(= (pyhost:self-store (py:Attribute $v $at $cx $l $c $el $ec) $mode $acc)
   (let $acc1 (pyhost:self-stores $v $mode $acc)
     (if (if (== $mode own) false (if (== $cx (py:Store)) (pyhost:is-name-self $v) false)) (cons-atom $at $acc1) $acc1)))
(= (pyhost:is-name-self $v) (case $v (((py:Name "self" $cx $l $c $el $ec) true) ($o false))))
; x += v: the target's own attribute is not a store here; what is inside it is
(= (pyhost:self-stores-aug ($target $op $value $l $c $el $ec) $mode $acc)
   (let $acc1 (case $target
                (((py:Attribute $v $at $cx $al $ac $ael $aec) (pyhost:self-stores $v $mode $acc))
                 ($other (pyhost:self-stores $target $mode $acc))))
     (pyhost:self-stores $value $mode $acc1)))

; how CPython compiles a call (compiler_call): with a starred argument or a
; ** keyword, through one sequence and one dict; else a method call when the
; callee is an attribute reference; else a plain call
(= (pyhost:call-shape $f $args $kws)
   (if (pyhost:any-starred $args) (c:shape-ex)
     (if (pyhost:any-dstar $kws) (c:shape-ex)
       (if (== (car-atom $f) py:Attribute) (c:shape-method) (c:shape-function)))))
(= (pyhost:any-starred $es)
   (case $es (((LNil) false) ((LCons $e $rest) (if (== (car-atom $e) py:Starred) true (pyhost:any-starred $rest))))))
(= (pyhost:any-dstar $ks)
   (case $ks
     (((LNil) false)
      ((LCons (py:keyword $a $v $kl $kc $kel $kec) $rest) (if (== $a (py:None)) true (pyhost:any-dstar $rest))))))
(= (pyhost:is-starred $e) (if (== (car-atom $e) py:Starred) (py:True) (py:False)))

; ---------------------------------------------------------------- annotations

; a function's annotations as its code gets them (compiler_visit_annotations):
; the positional parameters' first, then the positional-only ones', *args',
; the keyword-only ones', **kwargs' and the return's; names mangled
; a function's annotations (PEP 649, 3.14+): its __annotate__ function's,
; in codegen_annotations_in_scope's order: positional-only, positional,
; *args, keyword-only, **kwargs, then the return annotation, names mangled
(= (pyhost:fn-annotation-pairs (py:arguments $po $pa $va $ko $kd $kw $ds) $returns $private)
   (pyhost:append-list (pyhost:arg-annotations $po $private)
   (pyhost:append-list (pyhost:arg-annotations $pa $private)
   (pyhost:append-list (pyhost:arg-annotations (pyhost:opt-arg $va) $private)
   (pyhost:append-list (pyhost:arg-annotations $ko $private)
   (pyhost:append-list (pyhost:arg-annotations (pyhost:opt-arg $kw) $private)
                       (if (== $returns (py:None)) (LNil) (LCons (c:kwv "return" $returns) (LNil)))))))))
(= (pyhost:fn-has-annotations $args $rt)
   (if (== (pyhost:fn-annotation-pairs $args $rt (py:None)) (LNil)) (py:False) (py:True)))
(= (pyhost:annotate-qualname $qn) (str:concat $qn ".__annotate__"))
; its one positional-only parameter, .format in the symbol tables
(= (pyhost:annotate-args (st:pos $l $c))
   (py:arguments (LCons (py:arg ".format" (py:None) (py:None) $l $c $l $c) (LNil)) (LNil) (py:None) (LNil) (LNil) (py:None) (LNil)))
; its statements: a format above VALUE_WITH_FAKE_GLOBALS (2) raises
; NotImplementedError (codegen_emit_annotations_prologue); then the dict
(= (pyhost:annotate-body $args $rt $private (st:pos $l $c))
   (let $pairs (pyhost:fn-annotation-pairs $args $rt $private)
     (let $keys (pyhost:pair-keys $pairs $l $c)
       (let $values (pyhost:pair-values $pairs)
         (pyhost:annotate-stmts (py:Dict $keys $values $l $c $l $c) $l $c)))))
(= (pyhost:annotate-stmts $result $l $c)
   (pyhost:append-list (pyhost:annotate-prologue (st:pos $l $c)) (LCons (py:Return $result $l $c $l $c) (LNil))))
; (its parameter read from its slot, LOAD_FAST 0)
(= (pyhost:annotate-prologue (st:pos $l $c))
   (LCons (py:If (py:Compare (c:pre (c:local 0)) (LCons (py:Gt) (LNil)) (LCons (py:Constant 2 (py:None) $l $c $l $c) (LNil)) $l $c $l $c)
                 (LCons (py:Raise (c:common-expr (c:cc-not-implemented-error)) (py:None) $l $c $l $c) (LNil))
                 (LNil) $l $c $l $c)
          (LNil)))
(= (pyhost:pair-keys $ps $l $c)
   (case $ps (((LNil) (LNil)) ((LCons (c:kwv $k $v) $rest) (LCons (py:Constant $k (py:None) $l $c $l $c) (pyhost:pair-keys $rest $l $c))))))
(= (pyhost:pair-values $ps)
   (case $ps (((LNil) (LNil)) ((LCons (c:kwv $k $v) $rest) (LCons $v (pyhost:pair-values $rest))))))
; its block: the symbol table's, or under from __future__ import annotations,
; whose annotation blocks the table leaves out (symtable_enter_existing_block),
; one of no names, its parameter aside
(= (pyhost:annotation-table $tabs $pos)
   (let $found (collapse (pyhost:child-table $tabs (st:annotation) $pos))
     (if (== $found ()) (st:table (st:annotation) "__annotate__" $pos (LNil) (LNil)) (car-atom $found))))
; its spec: the parameter shown as format (CPython renames it in
; co_localsplusnames, so a name format in the annotations is not it)
(= (pyhost:annotate-spec (c:args $po $pa $ko $st $ds $names)) (c:args $po $pa $ko $st $ds (pyhost:rename-format $names)))
(= (pyhost:rename-format $names) (case $names (((LCons ".format" $rest) (LCons "format" $rest)) ($other $other))))
(= (pyhost:opt-arg $a) (if (== $a (py:None)) (LNil) (LCons $a (LNil))))
(= (pyhost:arg-annotations $args $private)
   (case $args
     (((LNil) (LNil))
      ((LCons (py:arg $n $an $tc $l $c $el $ec) $rest)
       (if (== $an (py:None)) (pyhost:arg-annotations $rest $private)
           (LCons (c:kwv (pyhost:mangle $private $n) $an) (pyhost:arg-annotations $rest $private)))))))

; what CPython evaluates of a bare annotated subscript's index
; (check_ann_subscr): a slice's bounds, each element's of a tuple, else the
; index itself
(= (pyhost:ann-subscript-parts $e)
   (case $e
     (((py:Slice $lo $up $st $l $c $el $ec) (pyhost:present-parts (LCons $lo (LCons $up (LCons $st (LNil))))))
      ((py:Tuple $es $cx $l $c $el $ec) (pyhost:ann-subscript-parts-each $es))
      ($other (LCons $e (LNil))))))
(= (pyhost:ann-subscript-parts-each $es)
   (case $es
     (((LNil) (LNil))
      ((LCons $e $rest) (pyhost:append-list (pyhost:ann-subscript-parts $e) (pyhost:ann-subscript-parts-each $rest))))))
(= (pyhost:present-parts $es)
   (case $es
     (((LNil) (LNil))
      ((LCons $e $rest) (if (== $e (py:None)) (pyhost:present-parts $rest) (LCons $e (pyhost:present-parts $rest)))))))

; ---------------------------------------------------------------- deferred annotations

; a module's or class's annotations of simple names, deferred to its
; __annotate__ (codegen_annassign, _PyCompile_AddDeferredAnnotation): each
; gets an index, at a module's level always, in a class only inside a
; conditional block (for, while, if, match, try, with); the statement adds its
; index to __conditional_annotations__ when it runs, and __annotate__ reads an
; indexed annotation only when its index is there.  The statements are taken
; in the code generator's order, a finally body once for each copy it makes
; of it: where a break or continue leaves through it, on the normal path,
; then on the exception path; so the indices are CPython's.  The result is
; (c:deferred ENTRIES BODY): ENTRIES (c:dann KEY ANNOTATION INDEX L C EL EC)
; in that order (INDEX -1: unconditional), BODY the statements with each
; simple target's flag replaced by (c:simple INDEX), the index of its copy on
; the normal path
(= (pyhost:deferred-annotations $kind $body $private)
   (let (c:dr $b (c:ds $es $nx)) (pyhost:dw-stmts $body (c:dk $kind $private) (LNil) false (c:ds (LNil) 0))
     (c:deferred (pyhost:lrev-onto $es (LNil)) $b)))
(= (pyhost:lrev-onto $xs $tail)
   (case $xs (((LNil) $tail) ((LCons $x $rest) (pyhost:lrev-onto $rest (LCons $x $tail))))))
; (c:dr REWRITTEN STATE), STATE (c:ds ENTRIES-REVERSED NEXT); the stack holds
; what a break meets: (c:fb-loop), (c:fb-finally BODY) or (c:fb-other)
(= (pyhost:dw-stmts $ss $dk $stack $cond $st)
   (case $ss
     (((LNil) (c:dr (LNil) $st))
      ((LCons $s $rest)
       (let (c:dr $s2 $st1) (pyhost:dw-stmt $s $dk $stack $cond $st)
         (let (c:dr $rest2 $st2) (pyhost:dw-stmts $rest $dk $stack $cond $st1)
           (c:dr (LCons $s2 $rest2) $st2)))))))
(= (pyhost:dw-stmt $s $dk $stack $cond $st)
   (case $s
     (((py:AnnAssign $t $a $v $simple $l $c $el $ec) (pyhost:dw-ann $t $a $v $simple $l $c $el $ec $dk $cond $st))
      ((py:For $t $i $b $o $tc $l $c $el $ec)
       (let (c:dr (c:pair $b2 $o2) $st1) (pyhost:dw-loop $b $o $dk $stack $st) (c:dr (py:For $t $i $b2 $o2 $tc $l $c $el $ec) $st1)))
      ((py:AsyncFor $t $i $b $o $tc $l $c $el $ec)
       (let (c:dr (c:pair $b2 $o2) $st1) (pyhost:dw-loop $b $o $dk $stack $st) (c:dr (py:AsyncFor $t $i $b2 $o2 $tc $l $c $el $ec) $st1)))
      ((py:While $t $b $o $l $c $el $ec)
       (let (c:dr (c:pair $b2 $o2) $st1) (pyhost:dw-loop $b $o $dk $stack $st) (c:dr (py:While $t $b2 $o2 $l $c $el $ec) $st1)))
      ((py:If $t $b $o $l $c $el $ec)
       (let (c:dr $b2 $st1) (pyhost:dw-stmts $b $dk $stack true $st)
         (let (c:dr $o2 $st2) (pyhost:dw-stmts $o $dk $stack true $st1) (c:dr (py:If $t $b2 $o2 $l $c $el $ec) $st2))))
      ((py:With $is $b $tc $l $c $el $ec)
       (let (c:dr $b2 $st1) (pyhost:dw-stmts $b $dk (LCons (c:fb-other) $stack) true $st) (c:dr (py:With $is $b2 $tc $l $c $el $ec) $st1)))
      ((py:AsyncWith $is $b $tc $l $c $el $ec)
       (let (c:dr $b2 $st1) (pyhost:dw-stmts $b $dk (LCons (c:fb-other) $stack) true $st) (c:dr (py:AsyncWith $is $b2 $tc $l $c $el $ec) $st1)))
      ((py:Match $x $cases $l $c $el $ec)
       (let (c:dr $cs2 $st1) (pyhost:dw-cases $cases $dk $stack $st) (c:dr (py:Match $x $cs2 $l $c $el $ec) $st1)))
      ((py:Try $b $hs $o $f $l $c $el $ec)
       (let (c:dr (c:try-parts $b2 $hs2 $o2 $f2) $st1) (pyhost:dw-try false $b $hs $o $f $dk $stack $st) (c:dr (py:Try $b2 $hs2 $o2 $f2 $l $c $el $ec) $st1)))
      ((py:TryStar $b $hs $o $f $l $c $el $ec)
       (let (c:dr (c:try-parts $b2 $hs2 $o2 $f2) $st1) (pyhost:dw-try true $b $hs $o $f $dk $stack $st) (c:dr (py:TryStar $b2 $hs2 $o2 $f2 $l $c $el $ec) $st1)))
      ((py:Break $l $c $el $ec) (c:dr $s (pyhost:dw-unwind $stack $dk $st)))
      ((py:Continue $l $c $el $ec) (c:dr $s (pyhost:dw-unwind $stack $dk $st)))
      ($other (c:dr $s $st)))))
(= (pyhost:dw-ann $t $a $v $simple $l $c $el $ec (c:dk $kind $private) $cond (c:ds $es $nx))
   (if (if (== $simple 0) true (pyhost:future-marked $simple))
       (c:dr (py:AnnAssign $t $a $v $simple $l $c $el $ec) (c:ds $es $nx))
       (let $key (pyhost:mangle $private (index-atom $t 1))
         (if (if (== $kind (c:module)) true $cond)
             (c:dr (py:AnnAssign $t $a $v (c:simple $nx) $l $c $el $ec) (c:ds (LCons (c:dann $key $a $nx $l $c $el $ec) $es) (+ $nx 1)))
             (c:dr (py:AnnAssign $t $a $v (c:simple -1) $l $c $el $ec) (c:ds (LCons (c:dann $key $a -1 $l $c $el $ec) $es) $nx))))))
; a loop's body (a break there ends the loop), then its else
(= (pyhost:dw-loop $b $o $dk $stack $st)
   (let (c:dr $b2 $st1) (pyhost:dw-stmts $b $dk (LCons (c:fb-loop) $stack) true $st)
     (let (c:dr $o2 $st2) (pyhost:dw-stmts $o $dk $stack true $st1) (c:dr (c:pair $b2 $o2) $st2))))
(= (pyhost:dw-cases $cs $dk $stack $st)
   (case $cs
     (((LNil) (c:dr (LNil) $st))
      ((LCons (py:match_case $p $g $b) $rest)
       (let (c:dr $b2 $st1) (pyhost:dw-stmts $b $dk $stack true $st)
         (let (c:dr $rest2 $st2) (pyhost:dw-cases $rest $dk $stack $st1) (c:dr (LCons (py:match_case $p $g $b2) $rest2) $st2)))))))
(= (pyhost:dw-handlers $hs $dk $stack $st)
   (case $hs
     (((LNil) (c:dr (LNil) $st))
      ((LCons (py:ExceptHandler $t $n $b $l $c $el $ec) $rest)
       (let (c:dr $b2 $st1) (pyhost:dw-stmts $b $dk $stack true $st)
         (let (c:dr $rest2 $st2) (pyhost:dw-handlers $rest $dk $stack $st1)
           (c:dr (LCons (py:ExceptHandler $t $n $b2 $l $c $el $ec) $rest2) $st2)))))))
; try (codegen_try_finally, codegen_try_except, codegen_try_star_except): its
; body, then for try its else and its handlers, for try* its handlers and its
; else, all within the finally; then the finally body on the normal path (the
; copy whose index the statement registers), then on the exception path
(= (pyhost:dw-try $star $b $hs $o $f $dk $stack $st)
   (if (== $f (LNil))
       (let (c:dr (c:pair $b2 (c:pair $hs2 $o2)) $st1) (pyhost:dw-try-parts $star $b $hs $o $dk $stack $st)
         (c:dr (c:try-parts $b2 $hs2 $o2 $f) $st1))
       (let (c:dr (c:pair $b2 (c:pair $hs2 $o2)) $st1) (pyhost:dw-try-parts $star $b $hs $o $dk (LCons (c:fb-finally $f) $stack) $st)
         (let (c:dr $f2 $st2) (pyhost:dw-stmts $f $dk $stack true $st1)
           (let (c:dr $f3 $st3) (pyhost:dw-stmts $f $dk (LCons (c:fb-other) $stack) true $st2)
             (c:dr (c:try-parts $b2 $hs2 $o2 $f2) $st3))))))
(= (pyhost:dw-try-parts $star $b $hs $o $dk $stack $st)
   (let (c:dr $b2 $st1) (pyhost:dw-stmts $b $dk (LCons (c:fb-other) $stack) true $st)
     (if $star
         (let (c:dr $hs2 $st2) (pyhost:dw-handlers $hs $dk (LCons (c:fb-other) $stack) $st1)
           (let (c:dr $o2 $st3) (pyhost:dw-stmts $o $dk $stack true $st2) (c:dr (c:pair $b2 (c:pair $hs2 $o2)) $st3)))
         (let (c:dr $o2 $st2) (pyhost:dw-stmts $o $dk $stack true $st1)
           (let (c:dr $hs2 $st3) (pyhost:dw-handlers $hs $dk (LCons (c:fb-other) $stack) $st2) (c:dr (c:pair $b2 (c:pair $hs2 $o2)) $st3))))))
; a break or continue (codegen_unwind_fblock_stack): each finally body it
; leaves through, innermost first, copied where it stands, until its loop
(= (pyhost:dw-unwind $stack $dk $st)
   (case $stack
     (((LNil) $st)
      ((LCons (c:fb-loop) $rest) $st)
      ((LCons (c:fb-finally $f) $rest)
       (let (c:dr $f2 $st1) (pyhost:dw-stmts $f $dk $rest true $st) (pyhost:dw-unwind $rest $dk $st1)))
      ((LCons $other $rest) (pyhost:dw-unwind $rest $dk $st)))))
; what an annotated name registers: (c:register INDEX) for a deferred one
; with an index, (c:future-store) for one stored under from __future__
; import annotations, else (c:no-register)
(= (pyhost:ann-register $s)
   (case $s
     (((c:simple $i) (if (== $i -1) (c:no-register) (c:register $i)))
      ((c:future) (c:future-store))
      ($other (c:no-register)))))
(= (pyhost:future-marked $s) (case $s (((c:future) true) ((c:future-complex) true) ($other false))))
; where the annotation block of a module or class stands (symtable_visit_annotation):
; at its first annotation, in the symbol table's order (a try's handlers before its else)
(= (pyhost:first-annotation-pos $body) (pyhost:expr-pos (index-atom (pyhost:first-ann-stmts $body) 2)))
; whether a module or class body under from __future__ import annotations
; annotates anything (ste_annotations_used): it then makes __annotations__
; first (SETUP_ANNOTATIONS)
(= (pyhost:setup-annotations $body)
   (case (pyhost:first-ann-stmts $body)
     (((py:AnnAssign $t $a $v $simple $l $c $el $ec) (if (pyhost:future-marked $simple) (py:True) (py:False)))
      ($none (py:False)))))
(= (pyhost:first-ann-stmts $ss)
   (case $ss
     (((LNil) (c:none))
      ((LCons $s $rest) (let $f (pyhost:first-ann-stmt $s) (if (== $f (c:none)) (pyhost:first-ann-stmts $rest) $f))))))
(= (pyhost:first-ann-stmt $s)
   (case $s
     (((py:AnnAssign $t $a $v $simple $l $c $el $ec) $s)
      ((py:For $t $i $b $o $tc $l $c $el $ec) (pyhost:first-ann-stmts (pyhost:append-list $b $o)))
      ((py:AsyncFor $t $i $b $o $tc $l $c $el $ec) (pyhost:first-ann-stmts (pyhost:append-list $b $o)))
      ((py:While $t $b $o $l $c $el $ec) (pyhost:first-ann-stmts (pyhost:append-list $b $o)))
      ((py:If $t $b $o $l $c $el $ec) (pyhost:first-ann-stmts (pyhost:append-list $b $o)))
      ((py:With $is $b $tc $l $c $el $ec) (pyhost:first-ann-stmts $b))
      ((py:AsyncWith $is $b $tc $l $c $el $ec) (pyhost:first-ann-stmts $b))
      ((py:Match $x $cases $l $c $el $ec) (pyhost:first-ann-stmts (pyhost:case-stmts $cases)))
      ((py:Try $b $hs $o $f $l $c $el $ec) (pyhost:first-ann-stmts (pyhost:try-stmts $b $hs $o $f)))
      ((py:TryStar $b $hs $o $f $l $c $el $ec) (pyhost:first-ann-stmts (pyhost:try-stmts $b $hs $o $f)))
      ($other (c:none)))))
(= (pyhost:case-stmts $cs)
   (case $cs (((LNil) (LNil)) ((LCons (py:match_case $p $g $b) $rest) (pyhost:append-list $b (pyhost:case-stmts $rest))))))
(= (pyhost:try-stmts $b $hs $o $f)
   (pyhost:append-list $b (pyhost:append-list (pyhost:handler-stmts $hs) (pyhost:append-list $o $f))))
(= (pyhost:handler-stmts $hs)
   (case $hs (((LNil) (LNil)) ((LCons (py:ExceptHandler $t $n $b $l $c $el $ec) $rest) (pyhost:append-list $b (pyhost:handler-stmts $rest))))))
; the frame of a module's or class's __annotate__: a function's, and in a
; class whose annotations are conditional __conditional_annotations__ free
; after the others (codegen_deferred_annotations_body adds it to co_freevars)
(= (pyhost:deferred-layout $kind $es $args $t $private)
   (let $ly (pyhost:code-layout $args $t $private)
     (if (if (== $kind (c:class)) (pyhost:any-conditional $es) false)
         (pyhost:layout-add-free $ly "__conditional_annotations__")
         $ly)))
(= (pyhost:any-conditional $es)
   (case $es (((LNil) false) ((LCons (c:dann $k $a $i $l $c $el $ec) $rest) (if (== $i -1) (pyhost:any-conditional $rest) true)))))
(= (pyhost:layout-add-free (c:layout $slots (c:args $po $pa $ko $st $ds $s0) $cells $frees $fnames) $name)
   (if (pyhost:in-list $name $fnames)
       (c:layout $slots (c:args $po $pa $ko $st $ds $s0) $cells $frees $fnames)
       (let $slots2 (pyhost:append-list $slots (LCons $name (LNil)))
         (c:layout $slots2 (c:args $po $pa $ko $st $ds $slots2) $cells
                   (pyhost:append-list $frees (LCons (pyhost:slot-count $slots) (LNil)))
                   (pyhost:append-list $fnames (LCons $name (LNil)))))))
; ---------------------------------------------------------------- the file of a compilation

; a compilation's code objects, each nested one too, given where they come
; from: the core's (c:line FIRST) becomes (c:src FILE FIRST) (co_filename,
; co_firstlineno), (c:code NAME QUALNAME ARGS SLOTS CELLS FREES KIND BODY
; (c:src FILE FIRST)); FILE (c:builtin) for code that stands for CPython's C
; code (the prelude, the modules CPython writes in C), whose frames no
; traceback shows
(= (pyhost:with-file $t $file)
   (if (== (get-metatype $t) Expression)
       (if (== $t ()) $t
         (if (if (== (car-atom $t) c:code) (== (size-atom $t) 10) false)
             (let ($h $nm $q $a $nx $cs $fs $fl $body (c:line $first)) $t
               (c:code $nm $q $a $nx $cs $fs $fl (pyhost:with-file $body $file) (c:src $file $first)))
             (pyhost:with-file-each $t $file)))
       $t))
(= (pyhost:with-file-each $t $file)
   (if (== $t ()) ()
     (let ($x $rest) (decons-atom $t) (cons-atom (pyhost:with-file $x $file) (pyhost:with-file-each $rest $file)))))
; ---------------------------------------------------------------- from __future__ import annotations

; a module under from __future__ import annotations (PEP 563, as 3.14 keeps
; it): each annotation compiled as its text (codegen_visit_annexpr), and an
; annotated target of a module or class stored in its __annotations__
; (made first, SETUP_ANNOTATIONS) instead of deferred.  The module's
; statements are given with each annotation replaced by its text, a string
; constant, and each annotated target of a module or class marked (c:future)
; when simple, else (c:future-complex); other modules unchanged
; ---------------------------------------------------------------- lazy imports
; IMPORT_NAME's mode (codegen_import, codegen_from_import): an explicit lazy
; import is lazy; another import at module level outside an exception
; handler's region (_PyCompile_InExceptionHandler: a try body, its handlers,
; and its else too when a finally follows) and not of * may be lazy (under
; sys.set_lazy_imports("all") or the module's __lazy_modules__); any other is
; eager.  The module's import nodes are marked (c:imp-lazy) or (c:imp-maybe);
; those left unmarked are eager.  A finally body is marked as its normal
; path compiles it (outside any region; CPython's copy for the exception path
; is eager)
(= (pyhost:import-modes $m)
   (case $m
     (((py:Module $body $ti) (py:Module (pyhost:im-stmts $body false) $ti))
      ($other $other))))
(= (pyhost:im-stmts $ss $h)
   (case $ss (((LNil) (LNil)) ((LCons $s $rest) (LCons (pyhost:im-stmt $s $h) (pyhost:im-stmts $rest $h))))))
(= (pyhost:im-stmt $s $h)
   (case $s
     (((py:Import $as $lz $l $c $el $ec) (py:Import $as (pyhost:im-mark $lz $h false) $l $c $el $ec))
      ((py:ImportFrom $m $as $lv $lz $l $c $el $ec) (py:ImportFrom $m $as $lv (pyhost:im-mark $lz $h (pyhost:star-import $as)) $l $c $el $ec))
      ((py:For $t $i $b $o $tc $l $c $el $ec) (py:For $t $i (pyhost:im-stmts $b $h) (pyhost:im-stmts $o $h) $tc $l $c $el $ec))
      ((py:AsyncFor $t $i $b $o $tc $l $c $el $ec) (py:AsyncFor $t $i (pyhost:im-stmts $b $h) (pyhost:im-stmts $o $h) $tc $l $c $el $ec))
      ((py:While $t $b $o $l $c $el $ec) (py:While $t (pyhost:im-stmts $b $h) (pyhost:im-stmts $o $h) $l $c $el $ec))
      ((py:If $t $b $o $l $c $el $ec) (py:If $t (pyhost:im-stmts $b $h) (pyhost:im-stmts $o $h) $l $c $el $ec))
      ((py:With $is $b $tc $l $c $el $ec) (py:With $is (pyhost:im-stmts $b $h) $tc $l $c $el $ec))
      ((py:AsyncWith $is $b $tc $l $c $el $ec) (py:AsyncWith $is (pyhost:im-stmts $b $h) $tc $l $c $el $ec))
      ((py:Match $x $cases $l $c $el $ec) (py:Match $x (pyhost:im-cases $cases $h) $l $c $el $ec))
      ((py:Try $b $hs $o $f $l $c $el $ec)
       (py:Try (pyhost:im-stmts $b true) (pyhost:im-handlers $hs) (pyhost:im-stmts $o (pyhost:im-else-region $f $h))
               (pyhost:im-stmts $f $h) $l $c $el $ec))
      ((py:TryStar $b $hs $o $f $l $c $el $ec)
       (py:TryStar (pyhost:im-stmts $b true) (pyhost:im-handlers $hs) (pyhost:im-stmts $o (pyhost:im-else-region $f $h))
                   (pyhost:im-stmts $f $h) $l $c $el $ec))
      ($other $other))))
; a try's else is in the region when a finally follows (FINALLY_TRY covers it)
(= (pyhost:im-else-region $f $h) (if (== $f (LNil)) $h true))
(= (pyhost:im-handlers $hs)
   (case $hs
     (((LNil) (LNil))
      ((LCons (py:ExceptHandler $t $n $b $l $c $el $ec) $rest)
       (LCons (py:ExceptHandler $t $n (pyhost:im-stmts $b true) $l $c $el $ec) (pyhost:im-handlers $rest))))))
(= (pyhost:im-cases $cs $h)
   (case $cs
     (((LNil) (LNil))
      ((LCons (py:match_case $p $g $b) $rest) (LCons (py:match_case $p $g (pyhost:im-stmts $b $h)) (pyhost:im-cases $rest $h))))))
(= (pyhost:im-mark $lz $h $star)
   (if (== $lz 1) (c:imp-lazy) (if (if $h true $star) $lz (c:imp-maybe))))
(= (pyhost:star-import $as)
   (case $as (((LCons (py:alias "*" $asn $l $c $el $ec) $rest) true) ($other false))))
; the Core's reading of an import's mark
(= (pyhost:import-mode $lz)
   (case $lz (((c:imp-lazy) (c:imp-lazy)) ((c:imp-maybe) (c:imp-maybe)) ($other (c:imp-eager)))))

(= (pyhost:future-module $m)
   (case $m
     (((py:Module $body $ti)
       (if (== (pyhost:future-annotations $body) (py:True)) (py:Module (pyhost:fa-stmts $body true) $ti) $m))
      ($other $other))))
; MARK: whether the statements are a module's or a class's
(= (pyhost:fa-stmts $ss $mark)
   (case $ss (((LNil) (LNil)) ((LCons $s $rest) (LCons (pyhost:fa-stmt $s $mark) (pyhost:fa-stmts $rest $mark))))))
(= (pyhost:fa-stmt $s $mark)
   (case $s
     (((py:AnnAssign $t $a $v $simple $l $c $el $ec)
       (py:AnnAssign $t (pyhost:ann-text $a) $v (if $mark (if (== $simple 0) (c:future-complex) (c:future)) $simple) $l $c $el $ec))
      ((py:FunctionDef $n $a $b $ds $r $tc $tps $l $c $el $ec)
       (py:FunctionDef $n (pyhost:fa-arguments $a) (pyhost:fa-stmts $b false) $ds (pyhost:fa-opt $r) $tc $tps $l $c $el $ec))
      ((py:AsyncFunctionDef $n $a $b $ds $r $tc $tps $l $c $el $ec)
       (py:AsyncFunctionDef $n (pyhost:fa-arguments $a) (pyhost:fa-stmts $b false) $ds (pyhost:fa-opt $r) $tc $tps $l $c $el $ec))
      ((py:ClassDef $n $bs $ks $b $ds $tps $l $c $el $ec) (py:ClassDef $n $bs $ks (pyhost:fa-stmts $b true) $ds $tps $l $c $el $ec))
      ((py:For $t $i $b $o $tc $l $c $el $ec) (py:For $t $i (pyhost:fa-stmts $b $mark) (pyhost:fa-stmts $o $mark) $tc $l $c $el $ec))
      ((py:AsyncFor $t $i $b $o $tc $l $c $el $ec) (py:AsyncFor $t $i (pyhost:fa-stmts $b $mark) (pyhost:fa-stmts $o $mark) $tc $l $c $el $ec))
      ((py:While $t $b $o $l $c $el $ec) (py:While $t (pyhost:fa-stmts $b $mark) (pyhost:fa-stmts $o $mark) $l $c $el $ec))
      ((py:If $t $b $o $l $c $el $ec) (py:If $t (pyhost:fa-stmts $b $mark) (pyhost:fa-stmts $o $mark) $l $c $el $ec))
      ((py:With $is $b $tc $l $c $el $ec) (py:With $is (pyhost:fa-stmts $b $mark) $tc $l $c $el $ec))
      ((py:AsyncWith $is $b $tc $l $c $el $ec) (py:AsyncWith $is (pyhost:fa-stmts $b $mark) $tc $l $c $el $ec))
      ((py:Match $x $cases $l $c $el $ec) (py:Match $x (pyhost:fa-cases $cases $mark) $l $c $el $ec))
      ((py:Try $b $hs $o $f $l $c $el $ec)
       (py:Try (pyhost:fa-stmts $b $mark) (pyhost:fa-handlers $hs $mark) (pyhost:fa-stmts $o $mark) (pyhost:fa-stmts $f $mark) $l $c $el $ec))
      ((py:TryStar $b $hs $o $f $l $c $el $ec)
       (py:TryStar (pyhost:fa-stmts $b $mark) (pyhost:fa-handlers $hs $mark) (pyhost:fa-stmts $o $mark) (pyhost:fa-stmts $f $mark) $l $c $el $ec))
      ($other $other))))
(= (pyhost:fa-cases $cs $mark)
   (case $cs (((LNil) (LNil)) ((LCons (py:match_case $p $g $b) $rest) (LCons (py:match_case $p $g (pyhost:fa-stmts $b $mark)) (pyhost:fa-cases $rest $mark))))))
(= (pyhost:fa-handlers $hs $mark)
   (case $hs
     (((LNil) (LNil))
      ((LCons (py:ExceptHandler $t $n $b $l $c $el $ec) $rest)
       (LCons (py:ExceptHandler $t $n (pyhost:fa-stmts $b $mark) $l $c $el $ec) (pyhost:fa-handlers $rest $mark))))))
(= (pyhost:fa-arguments (py:arguments $po $pa $va $ko $kd $kw $ds))
   (py:arguments (pyhost:fa-args $po) (pyhost:fa-args $pa) (pyhost:fa-arg $va) (pyhost:fa-args $ko) $kd (pyhost:fa-arg $kw) $ds))
(= (pyhost:fa-args $as) (case $as (((LNil) (LNil)) ((LCons $a $rest) (LCons (pyhost:fa-arg $a) (pyhost:fa-args $rest))))))
(= (pyhost:fa-arg $a)
   (case $a (((py:arg $n $an $tc $l $c $el $ec) (py:arg $n (pyhost:fa-opt $an) $tc $l $c $el $ec)) ($none $none))))
(= (pyhost:fa-opt $e) (if (== $e (py:None)) $e (pyhost:ann-text $e)))
(= (pyhost:ann-text $e)
   (let (st:pos $l $c) (pyhost:expr-pos $e)
     (py:Constant (pyhost:unparse $e) (py:None) $l $c (index-atom $e (- (size-atom $e) 2)) (index-atom $e (- (size-atom $e) 1)))))

; an annotation's text (Python/ast_unparse.c, _PyAST_ExprAsUnicode): written
; at the level of a test, each operand in parentheses when its operator binds
; less tightly than its place needs; the levels: 0 tuple, 1 test (if-else,
; lambda), 2 or, 3 and, 4 not, 5 comparison, 6 |, 7 ^, 8 &, 9 shifts,
; 10 + -, 11 * @ / % //, 12 unary + - ~, 13 **, 14 await, 15 atom
(= (pyhost:unparse $e) (pyhost:up $e 1))
(= (pyhost:up-par $open $s) (if $open (str:concat (str:concat "(" $s) ")") $s))
(= (pyhost:up $e $lv)
   (case $e
     (((py:BoolOp $op $vs $l $c $el $ec)
       (let $pr (if (== $op (py:And)) 3 2)
         (pyhost:up-par (> $lv $pr) (pyhost:up-sep $vs (if (== $op (py:And)) " and " " or ") (+ $pr 1)))))
      ((py:BinOp $a $op $b $l $c $el $ec)
       (let (c:op $s $pr $ra) (pyhost:up-binop $op)
         (pyhost:up-par (> $lv $pr)
           (str:concat (str:concat (pyhost:up $a (if $ra (+ $pr 1) $pr)) $s) (pyhost:up $b (if $ra $pr (+ $pr 1)))))))
      ((py:UnaryOp $op $x $l $c $el $ec)
       (let (c:op $s $pr $ra) (pyhost:up-unop $op) (pyhost:up-par (> $lv $pr) (str:concat $s (pyhost:up $x $pr)))))
      ((py:Lambda $args $body $l $c $el $ec)
       (pyhost:up-par (> $lv 1)
         (str:concat (str:concat (if (pyhost:up-positional $args) "lambda " "lambda") (pyhost:up-args $args)) (str:concat ": " (pyhost:up $body 1)))))
      ((py:IfExp $t $b $o $l $c $el $ec)
       (pyhost:up-par (> $lv 1) (str:concat (str:concat (pyhost:up $b 2) (str:concat " if " (pyhost:up $t 2))) (str:concat " else " (pyhost:up $o 1)))))
      ((py:Dict $ks $vs $l $c $el $ec) (str:concat (str:concat "{" (pyhost:join-lcons ", " (pyhost:up-items $ks $vs))) "}"))
      ((py:Set $es $l $c $el $ec) (str:concat (str:concat "{" (pyhost:up-sep $es ", " 1)) "}"))
      ((py:List $es $cx $l $c $el $ec) (str:concat (str:concat "[" (pyhost:up-sep $es ", " 1)) "]"))
      ((py:Tuple $es $cx $l $c $el $ec)
       (if (== $es (LNil)) "()"
           (pyhost:up-par (> $lv 0) (str:concat (pyhost:up-sep $es ", " 1) (case $es (((LCons $x (LNil)) ",") ($more "")))))))
      ((py:GeneratorExp $x $gs $l $c $el $ec) (str:concat (str:concat (str:concat "(" (pyhost:up $x 1)) (pyhost:up-comps $gs)) ")"))
      ((py:ListComp $x $gs $l $c $el $ec) (str:concat (str:concat (str:concat "[" (pyhost:up $x 1)) (pyhost:up-comps $gs)) "]"))
      ((py:SetComp $x $gs $l $c $el $ec) (str:concat (str:concat (str:concat "{" (pyhost:up $x 1)) (pyhost:up-comps $gs)) "}"))
      ((py:DictComp $k $v $gs $l $c $el $ec)
       (str:concat (str:concat "{" (if (== $v (py:None)) (str:concat "**" (pyhost:up $k 1)) (str:concat (str:concat (pyhost:up $k 1) ": ") (pyhost:up $v 1))))
                   (str:concat (pyhost:up-comps $gs) "}")))
      ((py:Yield $v $l $c $el $ec) (if (== $v (py:None)) "(yield)" (str:concat (str:concat "(yield " (pyhost:up $v 1)) ")")))
      ((py:YieldFrom $v $l $c $el $ec) (str:concat (str:concat "(yield from " (pyhost:up $v 1)) ")"))
      ((py:Await $v $l $c $el $ec) (pyhost:up-par (> $lv 14) (str:concat "await " (pyhost:up $v 15))))
      ((py:Compare $a $ops $cs $l $c $el $ec) (pyhost:up-par (> $lv 5) (str:concat (pyhost:up $a 6) (pyhost:up-cmps $ops $cs))))
      ((py:Call $f $as $ks $l $c $el $ec) (str:concat (pyhost:up $f 15) (pyhost:up-call $as $ks)))
      ((py:Constant $v $k $l $c $el $ec)
       (if (== $v (py:Ellipsis)) "..." (str:concat (if (== $k (py:None)) "" $k) (pyhost:up-const $v))))
      ((py:JoinedStr $vs $l $c $el $ec) (str:concat "f" (pyrt:str-repr (pyhost:up-ft-body $vs))))
      ((py:TemplateStr $vs $l $c $el $ec) (str:concat "t" (pyrt:str-repr (pyhost:up-ft-body $vs))))
      ((py:FormattedValue $v $cv $fs $l $c $el $ec) (pyhost:up-field (pyhost:up $v 2) $cv $fs))
      ((py:Interpolation $v $s $cv $fs $l $c $el $ec) (pyhost:up-field $s $cv $fs))
      ((py:Attribute $v $at $cx $l $c $el $ec)
       (str:concat (str:concat (pyhost:up $v 15) (if (pyhost:up-int-constant $v) " ." ".")) $at))
      ((py:Subscript $v $s $cx $l $c $el $ec) (str:concat (str:concat (str:concat (pyhost:up $v 15) "[") (pyhost:up $s 0)) "]"))
      ((py:Starred $v $cx $l $c $el $ec) (str:concat "*" (pyhost:up $v 6)))
      ((py:Slice $a $b $s $l $c $el $ec)
       (str:concat (str:concat (str:concat (if (== $a (py:None)) "" (pyhost:up $a 1)) ":") (if (== $b (py:None)) "" (pyhost:up $b 1)))
                   (if (== $s (py:None)) "" (str:concat ":" (pyhost:up $s 1)))))
      ((py:Name $id $cx $l $c $el $ec) $id)
      ((py:NamedExpr $t $v $l $c $el $ec) (pyhost:up-par (> $lv 0) (str:concat (str:concat (pyhost:up $t 15) " := ") (pyhost:up $v 15)))))))
(= (pyhost:up-binop $op)
   (case $op
     (((py:Add) (c:op " + " 10 false)) ((py:Sub) (c:op " - " 10 false)) ((py:Mult) (c:op " * " 11 false))
      ((py:MatMult) (c:op " @ " 11 false)) ((py:Div) (c:op " / " 11 false)) ((py:Mod) (c:op " % " 11 false))
      ((py:LShift) (c:op " << " 9 false)) ((py:RShift) (c:op " >> " 9 false)) ((py:BitOr) (c:op " | " 6 false))
      ((py:BitXor) (c:op " ^ " 7 false)) ((py:BitAnd) (c:op " & " 8 false)) ((py:FloorDiv) (c:op " // " 11 false))
      ((py:Pow) (c:op " ** " 13 true)))))
(= (pyhost:up-unop $op)
   (case $op (((py:Invert) (c:op "~" 12 false)) ((py:Not) (c:op "not " 4 false)) ((py:UAdd) (c:op "+" 12 false)) ((py:USub) (c:op "-" 12 false)))))
(= (pyhost:up-cmps $ops $cs)
   (case $ops
     (((LNil) "")
      ((LCons $op $rest)
       (let (LCons $x $cs2) $cs
         (str:concat (str:concat (pyhost:up-cmp-op $op) (pyhost:up $x 6)) (pyhost:up-cmps $rest $cs2)))))))
(= (pyhost:up-cmp-op $op)
   (case $op
     (((py:Eq) " == ") ((py:NotEq) " != ") ((py:Lt) " < ") ((py:LtE) " <= ") ((py:Gt) " > ") ((py:GtE) " >= ")
      ((py:Is) " is ") ((py:IsNot) " is not ") ((py:In) " in ") ((py:NotIn) " not in "))))
(= (pyhost:up-sep $es $sep $lv)
   (case $es
     (((LNil) "")
      ((LCons $x (LNil)) (pyhost:up $x $lv))
      ((LCons $x $rest) (str:concat (str:concat (pyhost:up $x $lv) $sep) (pyhost:up-sep $rest $sep $lv))))))
(= (pyhost:join-lcons $sep $ps)
   (case $ps
     (((LNil) "")
      ((LCons $x (LNil)) $x)
      ((LCons $x $rest) (str:concat (str:concat $x $sep) (pyhost:join-lcons $sep $rest))))))
(= (pyhost:up-items $ks $vs)
   (case $ks
     (((LNil) (LNil))
      ((LCons $k $rest)
       (let (LCons $v $vs2) $vs
         (LCons (if (== $k (py:None)) (str:concat "**" (pyhost:up $v 6)) (str:concat (str:concat (pyhost:up $k 1) ": ") (pyhost:up $v 1)))
                (pyhost:up-items $rest $vs2)))))))
(= (pyhost:up-comps $gs)
   (case $gs
     (((LNil) "")
      ((LCons (py:comprehension $t $it $ifs $ia) $rest)
       (str:concat (str:concat (str:concat (if (== $ia 0) " for " " async for ") (pyhost:up $t 0))
                               (str:concat (str:concat " in " (pyhost:up $it 2)) (pyhost:up-ifs $ifs)))
                   (pyhost:up-comps $rest))))))
(= (pyhost:up-ifs $ifs)
   (case $ifs (((LNil) "") ((LCons $x $rest) (str:concat (str:concat " if " (pyhost:up $x 2)) (pyhost:up-ifs $rest))))))
; a call's arguments: a lone generator expression without parentheses of its own
(= (pyhost:up-call $as $ks)
   (case (c:pair $as $ks)
     (((c:pair (LCons (py:GeneratorExp $x $gs $l $c $el $ec) (LNil)) (LNil)) (pyhost:up (py:GeneratorExp $x $gs $l $c $el $ec) 1))
      ($other (str:concat (str:concat "(" (pyhost:join-lcons ", " (pyhost:append-list (pyhost:up-each $as 1) (pyhost:up-keywords $ks)))) ")")))))
(= (pyhost:up-each $es $lv) (case $es (((LNil) (LNil)) ((LCons $x $rest) (LCons (pyhost:up $x $lv) (pyhost:up-each $rest $lv))))))
(= (pyhost:up-keywords $ks)
   (case $ks
     (((LNil) (LNil))
      ((LCons (py:keyword $a $v $l $c $el $ec) $rest)
       (LCons (if (== $a (py:None)) (str:concat "**" (pyhost:up $v 1)) (str:concat (str:concat $a "=") (pyhost:up $v 1))) (pyhost:up-keywords $rest))))))
; a lambda's parameters (append_ast_args): positional ones with their
; defaults, ", /" after the positional-only ones; * or *args; keyword-only
; ones with theirs; **kwargs
(= (pyhost:up-positional (py:arguments $po $pa $va $ko $kd $kw $ds)) (if (== $po (LNil)) (!= $pa (LNil)) true))
(= (pyhost:up-args (py:arguments $po $pa $va $ko $kd $kw $ds))
   (let $npo (pyhost:slot-count $po)
     (let $first (- (+ $npo (pyhost:slot-count $pa)) (pyhost:slot-count $ds))
       (let $p1 (pyhost:up-pos-parts (pyhost:append-list $po $pa) 0 $npo $first $ds)
         (let $p2 (if (if (== $va (py:None)) (== $ko (LNil)) false) (LNil) (LCons (if (== $va (py:None)) "*" (str:concat "*" (pyhost:up-arg $va))) (LNil)))
           (let $p4 (if (== $kw (py:None)) (LNil) (LCons (str:concat "**" (pyhost:up-arg $kw)) (LNil)))
             (pyhost:join-lcons ", " (pyhost:append-list $p1 (pyhost:append-list $p2 (pyhost:append-list (pyhost:up-kw-parts $ko $kd) $p4))))))))))
(= (pyhost:up-pos-parts $as $i $npo $first $ds)
   (case $as
     (((LNil) (LNil))
      ((LCons $a $rest)
       (let $t0 (if (>= $i $first) (str:concat (str:concat (pyhost:up-arg $a) "=") (pyhost:up (pyhost:lnth $ds (- $i $first)) 1)) (pyhost:up-arg $a))
         (LCons (if (== (+ $i 1) $npo) (str:concat $t0 ", /") $t0) (pyhost:up-pos-parts $rest (+ $i 1) $npo $first $ds)))))))
(= (pyhost:up-kw-parts $ko $kd)
   (case $ko
     (((LNil) (LNil))
      ((LCons $a $rest)
       (let (LCons $d $kd2) $kd
         (LCons (if (== $d (py:None)) (pyhost:up-arg $a) (str:concat (str:concat (pyhost:up-arg $a) "=") (pyhost:up $d 1)))
                (pyhost:up-kw-parts $rest $kd2)))))))
(= (pyhost:up-arg (py:arg $n $an $tc $l $c $el $ec)) (if (== $an (py:None)) $n (str:concat (str:concat $n ": ") (pyhost:up $an 1))))
(= (pyhost:up-int-constant $v) (case $v (((py:Constant $x $k $l $c $el $ec) (pyrt:is-int $x)) ($other false))))
; a constant's repr (append_repr): an infinite float written 1e309, which reads back as it
(= (pyhost:up-const $v)
   (case $v
     (((py:True) "True") ((py:False) "False") ((py:None) "None") (inf "1e309")
      ((py:Complex $re $im) (str:concat (if (== $im inf) "1e309" (pyhost:repr-without-dot0 (pyrt:float-repr $im))) "j"))
      ($other (pyrt:repr $other)))))
(= (pyhost:repr-without-dot0 $s)
   (if (str:ends-with $s ".0") (let $cs (str:codepoints $s) (str:join "" (pyhost:take (- (size-atom $cs) 2) $cs))) $s))
; an f-string's or t-string's text between its quotes: literal parts with
; their braces doubled, each field {VALUE!CONVERSION:SPEC}
(= (pyhost:up-ft-body $vs)
   (case $vs (((LNil) "") ((LCons $x $rest) (str:concat (pyhost:up-ft-element $x) (pyhost:up-ft-body $rest))))))
(= (pyhost:up-ft-element $e)
   (case $e
     (((py:Constant $v $k $l $c $el $ec) (str:join "}}" (str:split "}" (str:join "{{" (str:split "{" $v)))))
      ($other (pyhost:up $e 1)))))
(= (pyhost:up-field $text $cv $fs)
   (str:concat (str:concat (str:concat (if (str:starts-with $text "{") "{ " "{") $text) (pyhost:up-conversion $cv))
               (str:concat (case $fs (((py:JoinedStr $vs $l $c $el $ec) (str:concat ":" (pyhost:up-ft-body $vs))) ($none ""))) "}")))
(= (pyhost:up-conversion $cv) (case $cv ((97 "!a") (114 "!r") (115 "!s") ($none ""))))

; how __annotate__ reads __conditional_annotations__: a module's as a global
; (LOAD_GLOBAL), a class's from its cell (LOAD_DEREF)
(= (pyhost:deferred-cond $kind $slots)
   (if (== $kind (c:class)) (pyhost:cell-of $slots "__conditional_annotations__") (c:global "__conditional_annotations__")))
; its statements after the prologue (codegen_deferred_annotations_body): each
; annotation stored in the dict DICT (a core atom) under its name, an indexed
; one only when its index is in __conditional_annotations__ (CA, a core atom)
(= (pyhost:deferred-stmts $es $dict $ca)
   (case $es
     (((LNil) (LNil))
      ((LCons (c:dann $key $a $i $l $c $el $ec) $rest)
       (let $store (py:Assign (LCons (py:Subscript (c:pre $dict) (py:Constant $key (py:None) $l $c $el $ec) (py:Store) $l $c $el $ec) (LNil))
                              $a (py:None) $l $c $el $ec)
         (LCons (if (== $i -1) $store
                    (py:If (py:Compare (py:Constant $i (py:None) $l $c $el $ec) (LCons (py:In) (LNil)) (LCons (c:pre $ca) (LNil)) $l $c $el $ec)
                           (LCons $store (LNil)) (LNil) $l $c $el $ec))
                (pyhost:deferred-stmts $rest $dict $ca)))))))

; ---------------------------------------------------------------- generators

; whether a function's body yields (CPython's ste_generator): a yield or a
; yield from anywhere in it, but not in a nested function, class, lambda or
; generator expression, of which only the parts evaluated where it stands
; count (decorators, defaults, annotations, bases, the first iterable); a
; generator expression's own body yields
(= (pyhost:has-yield $body)
   (case $body
     (((c:genexp-body $af $tg $ifs $gs $e) (py:True))
      ($other (if (pyhost:yields $body) (py:True) (py:False))))))
(= (pyhost:yields $t)
   (if (== (get-metatype $t) Expression)
       (if (== $t ()) false
         (case (car-atom $t)
           ((py:Yield true)
            (py:YieldFrom true)
            (py:FunctionDef (pyhost:yields-at $t (1 3 4)))
            (py:AsyncFunctionDef (pyhost:yields-at $t (1 3 4)))
            (py:Lambda (pyhost:yields-at $t (0)))
            (py:ClassDef (pyhost:yields-at $t (1 2 4)))
            (py:GeneratorExp (pyhost:yields (pyhost:first-iterable (index-atom $t 2))))
            ($other (pyhost:yields-each (cdr-atom $t))))))
       false))
(= (pyhost:yields-each $ts)
   (if (== $ts ()) false
     (let ($t $rest) (decons-atom $ts) (if (pyhost:yields $t) true (pyhost:yields-each $rest)))))
(= (pyhost:yields-at $node $is)
   (if (== $is ()) false
     (let ($i $rest) (decons-atom $is)
       (if (pyhost:yields (index-atom $node (+ $i 1))) true (pyhost:yields-at $node $rest)))))
(= (pyhost:first-iterable (LCons (py:comprehension $tg $it $ifs $ia) $rest)) $it)
; a generator expression's parameters: the first iterator, .0
(= (pyhost:genexp-args $l $c $el $ec)
   (py:arguments (LNil) (LCons (py:arg ".0" (py:None) (py:None) $l $c $el $ec) (LNil)) (py:None) (LNil) (LNil) (py:None) (LNil)))

; an f-string replacement field's conversion, as the AST numbers it
(= (pyhost:conversion-kind $n)
   (if (== $n 115) (c:conv-str) (if (== $n 114) (c:conv-repr) (if (== $n 97) (c:conv-ascii) (c:conv-none)))))

; ---------------------------------------------------------------- imports

; import NAME [as ASNAME]: the top-level name bound, or the dotted names
; after the first taken in turn and ASNAME bound
(= (pyhost:import-binding $name $asname)
   (if (== $asname (py:None))
       (c:bind-top (car-atom (str:split "." $name)))
       (c:bind-as (pyhost:to-lcons (cdr-atom (str:split "." $name))) $asname)))
(= (pyhost:import-module-name $mod) (if (== $mod (py:None)) "" $mod))
; from M import a as b, c: the fromlist ("a", "c") as a tuple value, each
; name with the name it binds; * stands alone
(= (pyhost:import-from-parts $aliases)
   (c:from-parts (cons-atom py:tuple (pyhost:alias-names $aliases)) (pyhost:alias-parts $aliases)))
(= (pyhost:alias-names $as)
   (case $as
     (((LNil) ())
      ((LCons (py:alias $n $asn $l $c $el $ec) $rest) (cons-atom $n (pyhost:alias-names $rest))))))
(= (pyhost:alias-parts $as)
   (case $as
     (((LNil) (LNil))
      ((LCons (py:alias $n $asn $l $c $el $ec) $rest)
       (LCons (if (== $n "*") (c:star) (c:from $n (if (== $asn (py:None)) $n $asn))) (pyhost:alias-parts $rest))))))


; ---------------------------------------------------------------- constant folding
; CPython's AST optimizer (Python/ast_opt.c) makes an expression of constants a
; constant before the compiler sees it: a unary or binary operation on
; constants, a tuple of constants, a constant subscripted by a constant, and
; __debug__.  The value is the runtime's own operation on the values, as the
; optimizer calls PyNumber_Add and the rest; an operation that fails, or whose
; result would exceed the optimizer's sizes, is left as it is.
; (c:folded VALUE), else (c:unfolded).
(= (pyhost:folded $e)
   (case $e
     (((py:Constant $v $k $l $c $el $ec) (c:folded (pyhost:core-value $v)))
      ((py:UnaryOp $o $a $l $c $el $ec) (pyhost:fold-unary $o (pyhost:folded $a)))
      ((py:BinOp $x $o $y $l $c $el $ec) (pyhost:fold-binop $o (pyhost:folded $x) (pyhost:folded $y)))
      ((py:Tuple $es (py:Load) $l $c $el $ec) (pyhost:fold-tuple (pyhost:folded-all $es)))
      ((py:Subscript $x $i (py:Load) $l $c $el $ec) (pyhost:fold-subscript (pyhost:folded $x) (pyhost:folded $i)))
      ((py:Name "__debug__" (py:Load) $l $c $el $ec) (c:folded True))
      ($other (c:unfolded)))))
(= (pyhost:fold-result $r) (case $r (((m:val $v) (c:folded $v)) ($no (c:unfolded)))))
(= (pyhost:fold-unary $o $r)
   (case $r (((c:folded $x) (pyhost:fold-result (pyrt:unary $o $x))) ($no (c:unfolded)))))
(= (pyhost:fold-binop $o $rx $ry)
   (case (fold2 $rx $ry)
     (((fold2 (c:folded $x) (c:folded $y))
       (if (pyhost:fold-within $o $x $y) (pyhost:fold-result (pyrt:binop $o $x $y)) (c:unfolded)))
      ($no (c:unfolded)))))
(= (pyhost:fold-subscript $rx $ri)
   (case (fold2 $rx $ri)
     (((fold2 (c:folded $x) (c:folded $i)) (pyhost:fold-result (pyrt:getitem $x $i)))
      ($no (c:unfolded)))))
(= (pyhost:fold-tuple $r)
   (case $r (((c:folded $vs) (c:folded (cons-atom py:tuple $vs))) ($no (c:unfolded)))))
; the values of a list of expressions, (c:folded (V ...)), when all fold
(= (pyhost:folded-all $es)
   (case $es
     (((LNil) (c:folded ()))
      ((LCons $x $rest)
       (case (pyhost:folded $x)
         (((c:folded $v)
           (case (pyhost:folded-all $rest) (((c:folded $vs) (c:folded (cons-atom $v $vs))) ($no1 (c:unfolded)))))
          ($no2 (c:unfolded))))))))
; the optimizer's sizes (safe_multiply, safe_power, safe_lshift, safe_mod):
; an int product, power or left shift of at most 128 bits; a repeated tuple of
; at most 256 items, 1024 counting the items of its nested tuples; a repeated
; text of at most 4096 characters; never a text % values.  For positive
; integers, a > floor(m / w) exactly when a * w > m.
(= (pyhost:fold-within $o $x $y)
   (case $o
     (((py:Mult) (pyhost:mult-within $x $y))
      ((py:Pow) (pyhost:pow-within $x $y))
      ((py:LShift) (pyhost:lshift-within $x $y))
      ((py:Mod) (if (pyrt:is-str $x) false (not (pyhost:is-bytes $x))))
      ($other true))))
(= (pyhost:is-bytes $x) (case $x (((py:Bytes $s) true) ($no false))))
(= (pyhost:mult-within $x $y)
   (if (pyrt:intish $x)
       (if (pyrt:intish $y)
           (pyhost:int-mult-within (pyrt:int-of $x) (pyrt:int-of $y))
           (pyhost:repeat-within (pyrt:int-of $x) $y))
       (if (pyrt:intish $y) (pyhost:repeat-within (pyrt:int-of $y) $x) true)))
(= (pyhost:int-mult-within $a $b)
   (if (== $a 0) true
     (if (== $b 0) true (<= (+ (integer:bit-length $a) (integer:bit-length $b)) 128))))
; N copies of a tuple or a text; any other operand is the operation's to judge
(= (pyhost:repeat-within $n $s)
   (if (pyrt:is-tuple $s)
       (let $size (- (size-atom $s) 1)
         (if (== $size 0) true
           (if (< $n 0) false
             (if (> (* $n $size) 256) false
               (if (== $n 0) true
                 (>= (pyhost:complexity $s (/ (- 1024 (% 1024 $n)) $n)) 0))))))
       (if (pyrt:is-str $s) (pyhost:text-repeat-within $n (str:codepoint-count $s))
         (case $s (((py:Bytes $b) (pyhost:text-repeat-within $n (str:codepoint-count $b))) ($no true))))))
(= (pyhost:text-repeat-within $n $size)
   (if (== $size 0) true (if (< $n 0) false (<= (* $n $size) 4096))))
; check_complexity: LIMIT less a tuple's items and its nested tuples' items,
; counted until it is negative
(= (pyhost:complexity $x $limit)
   (if (pyrt:is-tuple $x) (pyhost:complexity-items (cdr-atom $x) (- $limit (- (size-atom $x) 1))) $limit))
(= (pyhost:complexity-items $xs $limit)
   (if (< $limit 0) $limit
     (if (== $xs ()) $limit
       (pyhost:complexity-items (cdr-atom $xs) (pyhost:complexity (car-atom $xs) $limit)))))
(= (pyhost:pow-within $x $y)
   (if (if (pyrt:intish $x) (pyrt:intish $y) false)
       (let $a (pyrt:int-of $x)
         (let $w (pyrt:int-of $y)
           (if (if (== $a 0) true (<= $w 0)) true (<= (* (integer:bit-length $a) $w) 128))))
       true))
(= (pyhost:lshift-within $x $y)
   (if (if (pyrt:intish $x) (pyrt:intish $y) false)
       (let $a (pyrt:int-of $x)
         (let $w (pyrt:int-of $y)
           (if (if (== $a 0) true (== $w 0)) true
             (if (< $w 0) false
               (if (> $w 128) false (<= (+ (integer:bit-length $a) $w) 128))))))
       true))

; a set display as CPython compiles it: codegen's starunpack_helper builds a
; display of at most _PY_STACK_USE_GUIDELINE (30) items with BUILD_SET N, and
; flowgraph.c's optimize_lists_and_sets folds that into the set of its
; constants when it has at least three items that all fold to constants,
; (c:constants VALUES); any other display is built item by item, (c:display),
; a larger one with BUILD_SET 0 and SET_ADD, which is never folded.
; (pyhost:to-lcons is the scope host's.)
(= (pyhost:set-display $es)
   (let $n (pyhost:lcount $es 0)
     (if (if (> $n 2) (<= $n 30) false)
         (case (pyhost:folded-all $es) (((c:folded $vs) (c:constants (pyhost:to-lcons $vs))) ($no (c:display))))
         (c:display))))
(= (pyhost:lcount $xs $n) (case $xs (((LNil) $n) ((LCons $x $rest) (pyhost:lcount $rest (+ $n 1))))))
; an operand the next instruction iterates (GET_ITER: a for loop's iterable,
; a comprehension's iterable after the first) or tests for membership in
; (CONTAINS_OP: the last operand of in and not in): a set display of at most
; 30 items that all fold to constants is then the frozenset constant itself
; (optimize_lists_and_sets with GET_ITER or CONTAINS_OP next: LOAD_CONST, no
; new set), (c:folded-set VALUES); any other operand is itself.  An async for
; reads its iterable with GET_AITER, which is never folded; a comprehension's
; first iterable is followed by the saving of its names, and a generator
; expression's by its call (3.15 calls iter() inside the generator).
(= (pyhost:iter-operand $async $e)
   (if (== $async (py:False)) (pyhost:folded-set-operand $e) $e))
(= (pyhost:contains-operand $o $e)
   (case $o (((py:In) (pyhost:folded-set-operand $e)) ((py:NotIn) (pyhost:folded-set-operand $e)) ($other $e))))
(= (pyhost:folded-set-operand $e)
   (case $e
     (((py:Set $es $l $c $el $ec)
       (if (<= (pyhost:lcount $es 0) 30)
           (case (pyhost:folded-all $es) (((c:folded $vs) (c:folded-set (pyhost:to-lcons $vs))) ($no $e)))
           $e))
      ($other $e))))

; ---------------------------------------------------------------- compile warnings
; The SyntaxWarnings a compilation emits, (c:syntax-warning MESSAGE LINE COL
; ELINE ECOL) each, in the order CPython emits them: the AST preprocessor's
; (ast_preprocess.c: a return, break or continue leaving a finally block, PEP
; 765), then the code generator's (codegen.c: an is with a literal, an
; assertion of a tuple, a literal called or subscripted), which the core holds
; where the generator meets them, up to its first error.  &py-compile-
; warnings holds the last compilation's, for whoever made it to show.
!(bind! &py-compile-warnings (new-state ()))

; ast_preprocess.c's control-flow contexts: (cf FINALLY FUNCDEF LOOP) on a
; stack, pushed for a finally block, a function's body and a loop's body (not
; its else); a return warns when the top is a finally block's, a break or
; continue when it is a finally block's and not a loop's
(= (pyhost:finally-warnings $mod)
   (case $mod
     (((py:Module $body $ti) (pyhost:rev-onto (pyhost:fw-stmts $body () ()) ()))
      ($other ()))))
(= (pyhost:fw-stmts $ss $ctx $acc)
   (case $ss
     (((LNil) $acc)
      ((LCons $s $rest) (pyhost:fw-stmts $rest $ctx (pyhost:fw-stmt $s $ctx $acc))))))
(= (pyhost:fw-stmt $s $ctx $acc)
   (case $s
     (((py:Return $v $l $c $el $ec) (pyhost:fw-exit "return" (pyhost:fw-in-finally $ctx) $l $c $el $ec $acc))
      ((py:Break $l $c $el $ec) (pyhost:fw-exit "break" (pyhost:fw-leaves-finally $ctx) $l $c $el $ec $acc))
      ((py:Continue $l $c $el $ec) (pyhost:fw-exit "continue" (pyhost:fw-leaves-finally $ctx) $l $c $el $ec $acc))
      ((py:FunctionDef $nm $a $body $ds $r $tc $tps $l $c $el $ec) (pyhost:fw-stmts $body (cons-atom (cf false true false) $ctx) $acc))
      ((py:AsyncFunctionDef $nm $a $body $ds $r $tc $tps $l $c $el $ec) (pyhost:fw-stmts $body (cons-atom (cf false true false) $ctx) $acc))
      ((py:ClassDef $nm $bs $ks $body $ds $tps $l $c $el $ec) (pyhost:fw-stmts $body $ctx $acc))
      ((py:For $t $it $body $orelse $tc $l $c $el $ec) (pyhost:fw-loop $body $orelse $ctx $acc))
      ((py:AsyncFor $t $it $body $orelse $tc $l $c $el $ec) (pyhost:fw-loop $body $orelse $ctx $acc))
      ((py:While $t $body $orelse $l $c $el $ec) (pyhost:fw-loop $body $orelse $ctx $acc))
      ((py:If $t $body $orelse $l $c $el $ec) (pyhost:fw-stmts $orelse $ctx (pyhost:fw-stmts $body $ctx $acc)))
      ((py:With $its $body $tc $l $c $el $ec) (pyhost:fw-stmts $body $ctx $acc))
      ((py:AsyncWith $its $body $tc $l $c $el $ec) (pyhost:fw-stmts $body $ctx $acc))
      ((py:Try $body $hs $orelse $final $l $c $el $ec) (pyhost:fw-try $body $hs $orelse $final $ctx $acc))
      ((py:TryStar $body $hs $orelse $final $l $c $el $ec) (pyhost:fw-try $body $hs $orelse $final $ctx $acc))
      ((py:Match $subj $cases $l $c $el $ec) (pyhost:fw-cases $cases $ctx $acc))
      ($other $acc))))
(= (pyhost:fw-loop $body $orelse $ctx $acc)
   (pyhost:fw-stmts $orelse $ctx (pyhost:fw-stmts $body (cons-atom (cf false false true) $ctx) $acc)))
(= (pyhost:fw-try $body $hs $orelse $final $ctx $acc)
   (pyhost:fw-stmts $final (cons-atom (cf true false false) $ctx)
     (pyhost:fw-stmts $orelse $ctx (pyhost:fw-handlers $hs $ctx (pyhost:fw-stmts $body $ctx $acc)))))
(= (pyhost:fw-handlers $hs $ctx $acc)
   (case $hs
     (((LNil) $acc)
      ((LCons (py:ExceptHandler $t $nm $body $l $c $el $ec) $rest) (pyhost:fw-handlers $rest $ctx (pyhost:fw-stmts $body $ctx $acc))))))
(= (pyhost:fw-cases $cs $ctx $acc)
   (case $cs
     (((LNil) $acc)
      ((LCons (py:match_case $p $g $body) $rest) (pyhost:fw-cases $rest $ctx (pyhost:fw-stmts $body $ctx $acc))))))
(= (pyhost:fw-in-finally $ctx)
   (if (== $ctx ()) false (let (cf $f $d $lp) (car-atom $ctx) (if $f (not $d) false))))
(= (pyhost:fw-leaves-finally $ctx)
   (if (== $ctx ()) false (let (cf $f $d $lp) (car-atom $ctx) (if $f (not $lp) false))))
(= (pyhost:fw-exit $kw $warns $l $c $el $ec $acc)
   (if $warns
       (let $m (str:concat (str:concat "'" $kw) "' in a 'finally' block") (cons-atom (c:syntax-warning $m $l $c $el $ec) $acc))
       $acc))

; the type CPython infers for an expression (codegen.c infer_type): its name,
; or () when it infers none
(= (pyhost:inferred-type $e)
   (case $e
     (((py:Tuple $es $cx $l $c $el $ec) "tuple")
      ((py:List $es $cx $l $c $el $ec) "list")
      ((py:ListComp $x $gs $l $c $el $ec) "list")
      ((py:Dict $ks $vs $l $c $el $ec) "dict")
      ((py:DictComp $k $v $gs $l $c $el $ec) "dict")
      ((py:Set $es $l $c $el $ec) "set")
      ((py:SetComp $x $gs $l $c $el $ec) "set")
      ((py:GeneratorExp $x $gs $l $c $el $ec) "generator")
      ((py:Lambda $a $b $l $c $el $ec) "function")
      ((py:TemplateStr $vs $l $c $el $ec) "string.templatelib.Template")
      ((py:Interpolation $v $s $k $f $l $c $el $ec) "string.templatelib.Template")
      ((py:JoinedStr $vs $l $c $el $ec) "str")
      ((py:FormattedValue $v $k $f $l $c $el $ec) "str")
      ((py:Constant $v $kind $l $c $el $ec) (pyhost:constant-type-name $v))
      ($other ()))))
(= (pyhost:constant-type-name $v)
   (case $v
     (((py:None) "NoneType") ((py:True) "bool") ((py:False) "bool") ((py:Ellipsis) "ellipsis")
      ((py:Complex $r $i) "complex") ((py:Bytes $b) "bytes")
      ($o (if (pyrt:is-str $o) "str" (if (pyrt:is-int $o) "int" (if (pyrt:is-float $o) "float" ())))))))
; check_is_arg: an operand is may hold (not a literal but the named singletons)
(= (pyhost:is-arg $e)
   (case $e
     (((py:Tuple $es $cx $l $c $el $ec) (not (pyhost:all-constants $es)))
      ((py:Constant $v $k $l $c $el $ec)
       (case $v (((py:None) true) ((py:True) true) ((py:False) true) ((py:Ellipsis) true) ($o false))))
      ($other true))))
(= (pyhost:all-constants $es)
   (case $es (((LNil) true) ((LCons (py:Constant $v $k $l $c $el $ec) $rest) (pyhost:all-constants $rest)) ($o false))))
; the code generator's warnings for a node, before it visits the node's parts,
; as a list of statements: codegen_check_compare, check_caller,
; check_subscripter then check_index (a subscript read), codegen_assert
(= (pyhost:code-warnings $node)
   (case $node
     (((py:Compare $a $os $cs $l $c $el $ec) (pyhost:compare-warning (pyhost:is-arg $a) $a $os $cs $l $c $el $ec))
      ((py:Call $f $as $ks $l $c $el $ec) (pyhost:caller-warning $f))
      ((py:Subscript $v $s $cx $l $c $el $ec) (pyhost:subscript-warnings $v $s))
      ((py:Assert $t $m $l $c $el $ec)
       (case $t
         (((py:Tuple (LCons $e $es) $cx $tl $tc $tel $tec)
           (LCons (c:syntax-warning "assertion is always true, perhaps remove parentheses?" $l $c $el $ec) (LNil)))
          ($o (LNil)))))
      ($other (LNil)))))
; the first is or is not with a literal operand, the literal named
(= (pyhost:compare-warning $leftok $left $os $cs $l $c $el $ec)
   (case $os
     (((LNil) (LNil))
      ((LCons $o $ors)
       (case $cs
         (((LCons $r $rcs)
           (let $rightok (pyhost:is-arg $r)
             (if (if (pyhost:is-identity $o) (if $rightok (not $leftok) true) false)
                 (let $m (pyhost:is-literal-message $o (pyhost:inferred-type (if $leftok $r $left)))
                   (LCons (c:syntax-warning $m $l $c $el $ec) (LNil)))
                 (pyhost:compare-warning $rightok $r $ors $rcs $l $c $el $ec))))))))))
(= (pyhost:is-identity $o) (case $o (((py:Is) true) ((py:IsNot) true) ($x false))))
(= (pyhost:is-literal-message $o $t)
   (case $o
     (((py:Is) (str:concat (str:concat "\"is\" with '" $t) "' literal. Did you mean \"==\"?"))
      ($x (str:concat (str:concat "\"is not\" with '" $t) "' literal. Did you mean \"!=\"?")))))
(= (pyhost:caller-warning $f)
   (if (pyhost:never-callable $f)
       (let $m (str:concat (str:concat "'" (pyhost:inferred-type $f)) "' object is not callable; perhaps you missed a comma?")
         (LCons (pyhost:warning-at $m $f) (LNil)))
       (LNil)))
(= (pyhost:never-callable $f)
   (case $f
     (((py:Constant $v $k $l $c $el $ec) true) ((py:Tuple $es $cx $l $c $el $ec) true)
      ((py:List $es $cx $l $c $el $ec) true) ((py:ListComp $x $gs $l $c $el $ec) true)
      ((py:Dict $ks $vs $l $c $el $ec) true) ((py:DictComp $k $v $gs $l $c $el $ec) true)
      ((py:Set $es $l $c $el $ec) true) ((py:SetComp $x $gs $l $c $el $ec) true)
      ((py:GeneratorExp $x $gs $l $c $el $ec) true) ((py:JoinedStr $vs $l $c $el $ec) true)
      ((py:TemplateStr $vs $l $c $el $ec) true) ((py:FormattedValue $v $k $fs $l $c $el $ec) true)
      ((py:Interpolation $v $s $k $fs $l $c $el $ec) true)
      ($o false))))
(= (pyhost:subscript-warnings $v $s)
   (if (pyhost:never-subscriptable $v)
       (let $m (str:concat (str:concat "'" (pyhost:inferred-type $v)) "' object is not subscriptable; perhaps you missed a comma?")
         (LCons (pyhost:warning-at $m $v) (pyhost:index-warning $v $s)))
       (pyhost:index-warning $v $s)))
(= (pyhost:never-subscriptable $v)
   (case $v
     (((py:Constant $x $k $l $c $el $ec)
       (case $x
         (((py:None) true) ((py:Ellipsis) true) ((py:True) true) ((py:False) true) ((py:Complex $r $i) true)
          ($o (if (pyrt:is-int $o) true (pyrt:is-float $o))))))
      ((py:Set $es $l $c $el $ec) true) ((py:SetComp $x $gs $l $c $el $ec) true)
      ((py:GeneratorExp $x $gs $l $c $el $ec) true) ((py:TemplateStr $vs $l $c $el $ec) true)
      ((py:Interpolation $x $s $k $f $l $c $el $ec) true) ((py:Lambda $a $b $l $c $el $ec) true)
      ($o false))))
; check_index: a sequence literal indexed by a value of a type CPython
; infers, neither an int's nor a slice
(= (pyhost:index-warning $v $s)
   (let $it (pyhost:inferred-type $s)
     (if (if (== $it ()) true (if (== $it "int") true (== $it "bool")))
         (LNil)
         (if (pyhost:sequence-literal $v)
             (let $m (str:concat (str:concat (str:concat (pyhost:inferred-type $v) " indices must be integers or slices, not ") $it)
                                 "; perhaps you missed a comma?")
               (LCons (pyhost:warning-at $m $v) (LNil)))
             (LNil)))))
(= (pyhost:sequence-literal $v)
   (case $v
     (((py:Constant $x $k $l $c $el $ec)
       (case $x (((py:Bytes $b) true) ($o (pyrt:is-str $o)))))
      ((py:Tuple $es $cx $l $c $el $ec) true) ((py:List $es $cx $l $c $el $ec) true)
      ((py:ListComp $x $gs $l $c $el $ec) true) ((py:JoinedStr $vs $l $c $el $ec) true)
      ((py:FormattedValue $x $k $f $l $c $el $ec) true)
      ($o false))))
; a warning at an expression's place: its last four fields
(= (pyhost:warning-at $m $e)
   (let ($l $c $el $ec) (pyhost:last4 (cdr-atom $e)) (c:syntax-warning $m $l $c $el $ec)))

; the warnings a core holds before its first error, in its order, and the core
; without them
(= (pyhost:core-warnings $t)
   (pyhost:warnings-before-error (pyhost:rev-onto (pyhost:diagnostics $t ()) ())))
(= (pyhost:diagnostics $t $acc)
   (if (== (get-metatype $t) Expression)
       (if (== (size-atom $t) 0) $acc
         (let $h (car-atom $t)
           (if (== $h c:syntax-warning) (cons-atom $t $acc)
             (if (== $h c:syntax-error) (cons-atom $t $acc)
               (pyhost:diagnostics-in $t $acc)))))
       $acc))
(= (pyhost:diagnostics-in $ts $acc)
   (if (== $ts ()) $acc (pyhost:diagnostics-in (cdr-atom $ts) (pyhost:diagnostics (car-atom $ts) $acc))))
(= (pyhost:warnings-before-error $ds)
   (if (== $ds ()) ()
     (let $d (car-atom $ds)
       (if (== (car-atom $d) c:syntax-error) ()
         (let $rest (pyhost:warnings-before-error (cdr-atom $ds)) (cons-atom $d $rest))))))
(= (pyhost:without-warnings $t)
   (if (== (get-metatype $t) Expression)
       (case $t
         (((LCons (c:syntax-warning $m $l $c $el $ec) $rest) (pyhost:without-warnings $rest))
          ($e (if (== (size-atom $e) 0) $e (pyhost:without-warnings-each $e)))))
       $t))
(= (pyhost:without-warnings-each $ts)
   (if (== $ts ()) ()
     (let $h (pyhost:without-warnings (car-atom $ts))
       (let $r (pyhost:without-warnings-each (cdr-atom $ts)) (cons-atom $h $r)))))

; ---------------------------------------------------------------- interned strings
; CPython's interned strings (one object per string, unicodeobject.c
; intern_common), as far as they decide whether a frozenset constant is built
; again: those of two or more ASCII letters, digits and underscores
; (should_intern_string; a one-character or empty string is a singleton that
; is its own interned object).  Interned are the strings CPython holds once
; started (interned_strings.metta), every name a compilation reads (the
; parser interns each identifier, before any code object is made), and such
; strings among the constants of the compilations before (interned with
; their code objects).  A compilation's own constants are one object each
; with the strings its code objects intern, so they decide nothing for it.
; A compilation's tree and its core wait as (py:pending-names AST) and
; (py:pending-constants CODE) until a question needs them.
!(bind! &py-interned (new-space))
!(import! &py-interned interned_strings)
(= (pyhost:interns $s) (if (>= (str:codepoint-count $s) 2) (str:is-all-in word $s) false))
(= (pyhost:interned $s)
   (let $f (pyhost:intern-pending)
     (if (== (collapse (match &py-interned (py:interned $s) true)) ()) false true)))
(= (pyhost:intern $s)
   (if (pyhost:interns $s)
       (if (== (collapse (match &py-interned (py:interned $s) true)) ()) (add-atom &py-interned (py:interned $s)) ())
       ()))
(= (pyhost:intern-pending)
   (let $names (collapse (match &py-interned (py:pending-names $a) $a))
     (let $consts (collapse (match &py-interned (py:pending-constants $c) $c))
       (let $u1 (pyhost:intern-each-tree $names)
         (pyhost:intern-each-core $consts)))))
(= (pyhost:intern-each-tree $as)
   (if (== $as ()) ()
     (let $a (car-atom $as)
       (let $r (remove-atom &py-interned (py:pending-names $a))
         (let $u (pyhost:intern-names $a) (pyhost:intern-each-tree (cdr-atom $as)))))))
(= (pyhost:intern-each-core $cs)
   (if (== $cs ()) ()
     (let $c (car-atom $cs)
       (let $r (remove-atom &py-interned (py:pending-constants $c))
         (let $u (pyhost:intern-constants $c) (pyhost:intern-each-core (cdr-atom $cs)))))))
; the names a module's tree holds: its strings but a constant's (its value
; and kind) and an interpolation's text, a dotted name by its parts
(= (pyhost:intern-names $t)
   (if (== (get-metatype $t) Expression)
       (if (== (size-atom $t) 0) ()
         (let $h (car-atom $t)
           (if (== $h py:Constant) ()
             (if (== $h py:Interpolation)
                 (let $u (pyhost:intern-names (index-atom $t 1)) (pyhost:intern-names-in (cdr-atom (cdr-atom (cdr-atom $t)))))
                 (pyhost:intern-names-in (cdr-atom $t))))))
       (if (pyrt:is-str $t)
           (if (pyrt:is-int (str:find $t ".")) (pyhost:intern-names-in (pyhost:dotted-parts $t)) (pyhost:intern $t))
           ())))
(= (pyhost:intern-names-in $ts)
   (if (== $ts ()) () (let $u (pyhost:intern-names (car-atom $ts)) (pyhost:intern-names-in (cdr-atom $ts)))))
(= (pyhost:dotted-parts $s) (str:split "." $s))
; the strings among a core's constants
(= (pyhost:intern-constants $t)
   (if (== (get-metatype $t) Expression)
       (if (== (size-atom $t) 0) ()
         (if (== (car-atom $t) c:const)
             (let $v (index-atom $t 1) (if (pyrt:is-str $v) (pyhost:intern $v) ()))
             (pyhost:intern-constants-in $t)))
       (if (pyrt:is-str $t) (pyhost:intern $t) ())))
(= (pyhost:intern-constants-in $ts)
   (if (== $ts ()) () (let $u (pyhost:intern-constants (car-atom $ts)) (pyhost:intern-constants-in (cdr-atom $ts)))))

; ---------------------------------------------------------------- the constant cache
; CPython keeps one object per constant key in a compilation (compile.c
; const_cache_insert, from flowgraph.c add_const).  The frozensets of folded
; set displays are registered as each code unit is optimized, after the units
; nested in it, which the compiler compiles where it meets them: so in the
; post-order of the units, and within a unit in its displays' order.  A
; display whose key (the set of its constants, each by its type and value,
; _PyCode_ConstantKey) equals an earlier registered one's is given that one's
; constants, and so its set (pyrt:const-set).  A set with a member interned
; as another object when its code object is made is built again
; (intern_constants), (c:const-set-rebuilt VALUES).
(= (pyhost:merge-const-sets $code)
   (let $regs (pyhost:const-sets-of-unit $code)
     (let $table (pyhost:const-set-table $regs ())
       (if (if (pyhost:const-sets-merged $regs $table) true (pyhost:any-rebuilt $table))
           (pyhost:canon-const-sets $code $table)
           $code))))
; the constant sets a code unit registers, in order: its nested units', then its own
(= (pyhost:const-sets-of-unit $code)
   (let (cs $nested $own) (pyhost:cs-walk-in (cdr-atom $code) (cs () ()))
     (pyhost:rev-onto $nested (pyhost:rev-onto $own ()))))
; the walk keeps (cs NESTED OWN), each reversed
(= (pyhost:cs-walk $t $acc)
   (if (== (get-metatype $t) Expression)
       (if (== (size-atom $t) 0) $acc
         (let $h (car-atom $t)
           (if (== $h c:const-set)
               (let (cs $n $o) $acc (let $vs (index-atom $t 1) (cs $n (cons-atom $vs $o))))
               (if (== $h c:code)
                   (let (cs $n $o) $acc (let $n2 (pyhost:rev-onto (pyhost:const-sets-of-unit $t) $n) (cs $n2 $o)))
                   (pyhost:cs-walk-in $t $acc)))))
       $acc))
(= (pyhost:cs-walk-in $ts $acc)
   (if (== $ts ()) $acc (pyhost:cs-walk-in (cdr-atom $ts) (pyhost:cs-walk (car-atom $ts) $acc))))
(= (pyhost:rev-onto $xs $tail)
   (if (== $xs ()) $tail (pyhost:rev-onto (cdr-atom $xs) (cons-atom (car-atom $xs) $tail))))
; the first registered constants of each key and whether their set is built
; again, as (ck KEYS VALUES REBUILT) entries
(= (pyhost:const-set-table $regs $table)
   (if (== $regs ()) $table
     (let $vs (car-atom $regs)
       (let $ks (pyhost:const-keys $vs)
         (pyhost:const-set-table (cdr-atom $regs)
           (if (== (pyhost:canon-by-keys $ks $table) ())
               (let $rb (pyhost:rebuilds $vs) (cons-atom (ck $ks $vs $rb) $table))
               $table))))))
; whether a set of these constants is built again: a member is a string
; interned as another object when its code object is made
(= (pyhost:rebuilds $vs)
   (case $vs
     (((LNil) false)
      ((LCons $v $rest)
       (if (if (pyrt:is-str $v) (if (pyhost:interns $v) (pyhost:interned $v) false) false)
           true
           (pyhost:rebuilds $rest))))))
(= (pyhost:any-rebuilt $table)
   (if (== $table ()) false
     (let (ck $k $v $rb) (car-atom $table) (if $rb true (pyhost:any-rebuilt (cdr-atom $table))))))
; a display's key: the written forms of its set's members, the constants
; less those equal to an earlier one (1.0 after 1 is not a member)
(= (pyhost:const-keys $vs) (pyhost:const-keys-onto $vs () ()))
(= (pyhost:const-keys-onto $vs $seen $ks)
   (case $vs
     (((LNil) $ks)
      ((LCons $v $rest)
       (case (pyrt:set-key $v)
         (((m:val $sk)
           (if (pyhost:among $sk $seen)
               (pyhost:const-keys-onto $rest $seen $ks)
               (let $k (repr $v) (pyhost:const-keys-onto $rest (cons-atom $sk $seen) (cons-atom $k $ks)))))))))))
(= (pyhost:among $x $xs) (if (== $xs ()) false (if (== (car-atom $xs) $x) true (pyhost:among $x (cdr-atom $xs)))))
(= (pyhost:same-keys $a $b) (if (== (size-atom $a) (size-atom $b)) (pyhost:all-among $a $b) false))
(= (pyhost:all-among $a $b) (if (== $a ()) true (if (pyhost:among (car-atom $a) $b) (pyhost:all-among (cdr-atom $a) $b) false)))
; the table's entry for a key, or (); its constants, or ()
(= (pyhost:canon-entry $ks $table)
   (if (== $table ()) ()
     (let (ck $k $v $rb) (car-atom $table)
       (if (pyhost:same-keys $ks $k) (car-atom $table) (pyhost:canon-entry $ks (cdr-atom $table))))))
(= (pyhost:canon-by-keys $ks $table)
   (let $e (pyhost:canon-entry $ks $table) (if (== $e ()) () (index-atom $e 2))))
; whether a display is given another's constants
(= (pyhost:const-sets-merged $regs $table)
   (if (== $regs ()) false
     (let $vs (car-atom $regs)
       (if (== (pyhost:canon-by-keys (pyhost:const-keys $vs) $table) $vs)
           (pyhost:const-sets-merged (cdr-atom $regs) $table)
           true))))
; the code with each display's constants its key's first registered ones,
; the set marked when it is built again
(= (pyhost:canon-const-sets $t $table)
   (if (== (get-metatype $t) Expression)
       (if (== (size-atom $t) 0) $t
         (if (== (car-atom $t) c:const-set)
             (let (ck $k $vs $rb) (pyhost:canon-entry (pyhost:const-keys (index-atom $t 1)) $table)
               (if $rb (c:const-set-rebuilt $vs) (c:const-set $vs)))
             (pyhost:canon-each $t $table)))
       $t))
(= (pyhost:canon-each $ts $table)
   (if (== $ts ()) ()
     (let $h (pyhost:canon-const-sets (car-atom $ts) $table)
       (let $r (pyhost:canon-each (cdr-atom $ts) $table) (cons-atom $h $r)))))

; ---------------------------------------------------------------- unpacking
; a target list's unpacking: (c:exact N) as UNPACK_SEQUENCE takes it, or,
; with a starred target, (c:star BEFORE AFTER) as UNPACK_EX takes it
(= (pyhost:unpack-shape $es)
   (let $n (pyhost:slot-count $es)
     (let $i (pyhost:starred-index $es 0)
       (if (== $i -1) (c:exact $n)
         (if (> (pyhost:starred-count $es) 1) (c:many-stars)
           (if (>= $i 256) (c:too-many-stars) (c:star $i (- (- $n $i) 1))))))))
(= (pyhost:starred-count $es)
   (case $es
     (((LNil) 0)
      ((LCons (py:Starred $v $cx $l $c $el $ec) $rest) (+ 1 (pyhost:starred-count $rest)))
      ((LCons $e $rest) (pyhost:starred-count $rest)))))
(= (pyhost:starred-index $es $i)
   (case $es
     (((LNil) -1)
      ((LCons (py:Starred $v $cx $l $c $el $ec) $rest) $i)
      ((LCons $e $rest) (pyhost:starred-index $rest (+ $i 1))))))

; ---------------------------------------------------------------- match
; a sequence pattern as compiler_pattern_sequence compiles it:
; (c:seq-shape LENCHECK EXTRACT).  LENCHECK: (c:len (py:Eq) N) without a star,
; (c:len (py:GtE) N-1) with one among N > 1 patterns, else (c:len-any).
; EXTRACT: (c:only-wildcards); (c:subscripts PLAN) when the star is a
; wildcard, PLAN (c:front I PATTERN) or (c:back K PATTERN) (the item at
; len - K) for each pattern not a wildcard; else (c:unpack-by SHAPE PATTERNS)
(= (pyhost:sequence-shape $ps)
   (let $size (pyhost:slot-count $ps)
     (let $star (pyhost:match-star-index $ps 0)
       (let $lc (if (== $star -1) (c:len (py:Eq) $size) (if (> $size 1) (c:len (py:GtE) (- $size 1)) (c:len-any)))
         (c:seq-shape $lc (pyhost:sequence-extract $ps $size $star))))))
(= (pyhost:match-star-index $ps $i)
   (case $ps
     (((LNil) -1)
      ((LCons (py:MatchStar $nm $l $c $el $ec) $rest) $i)
      ((LCons $p $rest) (pyhost:match-star-index $rest (+ $i 1))))))
(= (pyhost:wildcard $p) (case $p (((py:MatchAs (py:None) (py:None) $l $c $el $ec) true) ($o false))))
(= (pyhost:star-wildcard $p) (case $p (((py:MatchStar (py:None) $l $c $el $ec) true) ($o false))))
(= (pyhost:all-wildcards $ps)
   (case $ps
     (((LNil) true)
      ((LCons $p $rest) (if (if (pyhost:wildcard $p) true (pyhost:star-wildcard $p)) (pyhost:all-wildcards $rest) false)))))
(= (pyhost:lnth $xs $i) (case $xs (((LCons $x $rest) (if (== $i 0) $x (pyhost:lnth $rest (- $i 1)))))))
(= (pyhost:sequence-extract $ps $size $star)
   (if (pyhost:all-wildcards $ps) (c:only-wildcards)
     (if (if (== $star -1) false (pyhost:star-wildcard (pyhost:lnth $ps $star)))
         (c:subscripts (pyhost:subscript-plan $ps 0 $size $star))
         (c:unpack-by (if (== $star -1) (c:exact $size) (c:star $star (- (- $size $star) 1))) $ps))))
(= (pyhost:subscript-plan $ps $i $size $star)
   (case $ps
     (((LNil) (LNil))
      ((LCons $p $rest)
       (let $r (pyhost:subscript-plan $rest (+ $i 1) $size $star)
         (if (pyhost:wildcard $p) $r
           (if (== $i $star) $r
             (if (< $i $star) (LCons (c:front $i $p) $r) (LCons (c:back (- $size $i) $p) $r)))))))))
; a mapping pattern as compiler_pattern_mapping compiles it: (c:map-empty)
; for {} alone, else (c:map LENCHECK), the length checked when it has keys
(= (pyhost:mapping-shape $ks $rest)
   (let $size (pyhost:slot-count $ks)
     (if (if (== $size 0) (== $rest (py:None)) false) (c:map-empty)
       (c:map (if (> $size 0) (c:len (py:GtE) $size) (c:len-any))))))
; a class pattern's keyword attribute names, as the tuple MATCH_CLASS takes
(= (pyhost:class-names $ka) (cons-atom py:tuple (pyhost:to-flat $ka)))
; an or-pattern's captures (compiler_pattern_or): the first alternative's
; names, each given a temporary from N on: (c:control STORES N')
(= (pyhost:or-control $stores $n)
   (case $stores
     (((LNil) (c:control (LNil) $n))
      ((LCons (c:store $nm $a) $rest)
       (case (pyhost:or-control $rest (+ $n 1))
         (((c:control $ctl $n2) (c:control (LCons (c:store $nm (c:tmp $n)) $ctl) $n2))))))))
; an alternative's captures moved into those temporaries, in the first
; alternative's order; (c:or-diff) when its names are not the same
(= (pyhost:or-assigns $ctl $stores)
   (case (if (== (pyhost:slot-count $ctl) (pyhost:slot-count $stores)) (pyhost:or-assigns-each $ctl $stores) (c:or-diff))
     (((c:or-diff) (LCons (c:syntax-error "alternative patterns bind different names" 1 0 1 0) (LNil)))
      ($asg $asg))))
(= (pyhost:or-assigns-each $ctl $stores)
   (case $ctl
     (((LNil) (LNil))
      ((LCons (c:store $nm $t) $rest)
       (let $a (pyhost:store-of $nm $stores)
         (if (== $a (c:none)) (c:or-diff)
           (case (pyhost:or-assigns-each $rest $stores)
             (((c:or-diff) (c:or-diff))
              ($r (LCons (c:assign $t (c:atom $a)) $r))))))))))
(= (pyhost:store-of $nm $stores)
   (case $stores
     (((LNil) (c:none))
      ((LCons (c:store $m $a) $rest) (if (== $m $nm) $a (pyhost:store-of $nm $rest))))))

; ---------------------------------------------------------------- compile errors
; the first compile error of a case's pattern, in the order compile.c meets
; them (compiler_pattern and its helpers): an irrefutable pattern where none
; is allowed (a case neither last nor guarded; an or-pattern's alternative
; but the last), a name captured twice or __debug__, alternatives binding
; different names, two starred names in a sequence, a class pattern's keyword
; attribute twice, a mapping pattern's constant key twice (by value).
; (c:no-error) or (c:syntax-error MESSAGE LINE COL ELINE ECOL)
(= (pyhost:pattern-error $p $guard $last)
   (case (pyhost:pat-check $p (if (== $last (py:True)) true (if (== $guard (py:None)) false true)) (LNil))
     (((c:ok $stores) (c:no-error))
      ($err $err))))
; (c:ok STORES) with the names captured so far, or the error
(= (pyhost:pat-check $p $allow $stores)
   (case $p
     (((py:MatchStar $nm $l $c $el $ec) (pyhost:pat-store $nm $stores $l $c $el $ec))
      ((py:MatchAs $sub $nm $l $c $el $ec)
       (if (== $sub (py:None))
           (if $allow (pyhost:pat-store $nm $stores $l $c $el $ec)
             (c:syntax-error (if (== $nm (py:None)) "wildcard makes remaining patterns unreachable"
                               (str:concat (str:concat "name capture " (pyrt:quoted $nm)) " makes remaining patterns unreachable"))
                             $l $c $el $ec))
           (case (pyhost:pat-check $sub $allow $stores)
             (((c:ok $s2) (pyhost:pat-store $nm $s2 $l $c $el $ec))
              ($err $err)))))
      ((py:MatchSequence $ps $l $c $el $ec)
       (if (> (pyhost:match-star-count $ps) 1)
           (c:syntax-error "multiple starred names in sequence pattern" $l $c $el $ec)
           (pyhost:pats-check $ps $stores)))
      ((py:MatchMapping $ks $ps $rest $l $c $el $ec)
       (case (pyhost:dup-key $ks ())
         (((c:dup $v) (c:syntax-error (str:concat (str:concat "mapping pattern checks duplicate key (" (pyhost:const-repr $v)) ")") $l $c $el $ec))
          ($none (case (pyhost:pats-check $ps $stores)
                   (((c:ok $s2) (pyhost:pat-store $rest $s2 $l $c $el $ec))
                    ($err $err)))))))
      ((py:MatchClass $cls $ps $ka $kp $l $c $el $ec)
       (case (pyhost:kwd-attr-error $ka $kp)
         (((c:no-error) (pyhost:pats-check (pyhost:append-list $ps $kp) $stores))
          ($err $err))))
      ((py:MatchOr $alts $l $c $el $ec) (pyhost:or-check $alts $allow $stores $l $c $el $ec))
      ($other (c:ok $stores)))))
; sub-patterns, each allowed to be irrefutable (compiler_pattern_subpattern)
(= (pyhost:pats-check $ps $stores)
   (case $ps
     (((LNil) (c:ok $stores))
      ((LCons $p $rest)
       (case (pyhost:pat-check $p true $stores)
         (((c:ok $s2) (pyhost:pats-check $rest $s2))
          ($err $err)))))))
; a capture (pattern_helper_store_name): nothing for a wildcard
(= (pyhost:pat-store $nm $stores $l $c $el $ec)
   (if (== $nm (py:None)) (c:ok $stores)
     (if (== $nm "__debug__") (c:syntax-error "cannot assign to __debug__" $l $c $el $ec)
       (if (pyhost:in-list $nm $stores)
           (c:syntax-error (str:concat (str:concat "multiple assignments to name " (pyrt:quoted $nm)) " in pattern") $l $c $el $ec)
           (c:ok (pyhost:append-list $stores (LCons $nm (LNil))))))))
; an or-pattern (compiler_pattern_or): its alternatives, each with names of
; its own, only the last allowed to be irrefutable; all must bind the first's
; names; those names then join the captures so far
(= (pyhost:or-check $alts $allow $stores $l $c $el $ec)
   (case (pyhost:or-alts-check $alts $allow (c:none) (pyhost:slot-count $alts) 0)
     (((c:ok $control) (pyhost:or-merge $control $stores $l $c $el $ec))
      ((c:or-diff) (c:syntax-error "alternative patterns bind different names" $l $c $el $ec))
      ($err $err))))
(= (pyhost:or-alts-check $alts $allow $control $size $i)
   (case $alts
     (((LNil) (c:ok $control))
      ((LCons $p $rest)
       (case (pyhost:pat-check $p (if (== $i (- $size 1)) $allow false) (LNil))
         (((c:ok $st)
           (if (== $control (c:none))
               (pyhost:or-alts-check $rest $allow $st $size (+ $i 1))
               (if (pyhost:same-names $control $st)
                   (pyhost:or-alts-check $rest $allow $control $size (+ $i 1))
                   (c:or-diff))))
          ($err $err)))))))
(= (pyhost:same-names $a $b)
   (if (== (pyhost:slot-count $a) (pyhost:slot-count $b)) (pyhost:all-in-list $a $b) false))
(= (pyhost:all-in-list $a $b)
   (case $a (((LNil) true) ((LCons $x $rest) (if (pyhost:in-list $x $b) (pyhost:all-in-list $rest $b) false)))))
(= (pyhost:or-merge $control $stores $l $c $el $ec)
   (case $control
     (((LNil) (c:ok $stores))
      ((LCons $nm $rest)
       (if (pyhost:in-list $nm $stores)
           (c:syntax-error (str:concat (str:concat "multiple assignments to name " (pyrt:quoted $nm)) " in pattern") $l $c $el $ec)
           (pyhost:or-merge $rest (pyhost:append-list $stores (LCons $nm (LNil))) $l $c $el $ec))))))
(= (pyhost:match-star-count $ps)
   (case $ps
     (((LNil) 0)
      ((LCons (py:MatchStar $nm $l $c $el $ec) $rest) (+ 1 (pyhost:match-star-count $rest)))
      ((LCons $p $rest) (pyhost:match-star-count $rest)))))
; the first constant key a mapping pattern repeats, by value as a set sees
; keys: (c:dup VALUE), else (c:none)
(= (pyhost:dup-key $ks $seen)
   (case $ks
     (((LNil) (c:none))
      ((LCons $k $rest)
       (case (pyhost:folded $k)
         (((c:folded $v)
           (case (pyrt:key $v)
             (((m:val $ck) (if (is-member $ck $seen) (c:dup $v) (pyhost:dup-key $rest (cons-atom $ck $seen))))
              ($e (pyhost:dup-key $rest $seen)))))
          ($u (pyhost:dup-key $rest $seen))))))))
(= (pyhost:const-repr $v) (case (pyrt:repr-of $v) (((m:val $s) $s) ($e "?"))))
; a class pattern's keyword attributes (validate_kwd_attrs): __debug__, or one
; named twice (the error at the second one's pattern)
(= (pyhost:kwd-attr-error $ka $kp)
   (case $ka
     (((LNil) (c:no-error))
      ((LCons $a $rest)
       (case $kp
         (((LCons $p $prest)
           (if (== $a "__debug__") (pyhost:pattern-loc-error "cannot assign to __debug__" $p)
             (case (pyhost:repeat-of $a $rest $prest)
               (((c:none) (pyhost:kwd-attr-error $rest $prest))
                ($q (pyhost:pattern-loc-error (str:concat "attribute name repeated in class pattern: " $a) $q))))))))))))
(= (pyhost:repeat-of $a $ka $kp)
   (case $ka
     (((LNil) (c:none))
      ((LCons $b $rest)
       (case $kp (((LCons $p $prest) (if (== $a $b) $p (pyhost:repeat-of $a $rest $prest)))))))))
; an error at a pattern's place: its last four fields
(= (pyhost:pattern-loc-error $msg $p)
   (let ($l $c $el $ec) (pyhost:last4 (cdr-atom $p)) (c:syntax-error $msg $l $c $el $ec)))
(= (pyhost:last4 $xs) (if (== (size-atom $xs) 4) $xs (pyhost:last4 (cdr-atom $xs))))

; the first (c:syntax-error ...) of a core, in its order: () when none
(= (pyhost:first-error $t)
   (if (== (get-metatype $t) Expression)
       (if (== (size-atom $t) 0) ()
         (if (== (car-atom $t) c:syntax-error) $t (pyhost:first-error-in $t)))
       ()))
(= (pyhost:first-error-in $ts)
   (if (== $ts ()) ()
     (let $e (pyhost:first-error (car-atom $ts))
       (if (== $e ()) (pyhost:first-error-in (cdr-atom $ts)) $e))))

; ---------------------------------------------------------------- async comprehensions
; whether a comprehension is asynchronous (PEP 530): an async for among its
; generators, or an await in what its own scope evaluates (its element and
; value, its conditions, its iterables but the first), outside nested
; functions, lambdas, classes and generator expressions
; a position's line
(= (pyhost:pos-line (st:pos $l $c)) $l)
; where a node stands: (c:span LINE COL END-LINE END-COL), its last four
; fields; (c:no-span) for a node without them
(= (pyhost:node-span $e)
   (if (== (get-metatype $e) Expression)
       (if (>= (size-atom $e) 5)
           (let $n (size-atom $e)
             (let $l (index-atom $e (- $n 4))
               (if (pyrt:is-int $l)
                   (c:span $l (index-atom $e (- $n 3)) (index-atom $e (- $n 2)) (index-atom $e (- $n 1)))
                   (c:no-span))))
           (c:no-span))
       (c:no-span)))
; a comprehension's element: (c:star VALUE) when unpacked (*VALUE, PEP 798),
; else (c:plain ELEMENT)
(= (pyhost:comp-elt $e) (case $e (((py:Starred $v $cx $l $c $el $ec) (c:star $v)) ($other (c:plain $other)))))
; a template string's strings (codegen_template_str): each constant part, and
; an empty string before an interpolation that begins the template or follows
; another, and at the end after one
(= (pyhost:template-strings $vs) (cons-atom py:tuple (pyhost:t-strings $vs true)))
(= (pyhost:t-strings $vs $after-interpolation)
   (case $vs
     (((LNil) (if $after-interpolation (cons-atom "" ()) ()))
      ((LCons (py:Constant $v $k $l $c $el $ec) $rest) (cons-atom (pyhost:core-value $v) (pyhost:t-strings $rest false)))
      ((LCons $i $rest) (if $after-interpolation (cons-atom "" (pyhost:t-strings $rest true)) (pyhost:t-strings $rest true))))))
(= (pyhost:comp-async $gs $e $v)
   (if (pyhost:any-async-gen $gs) (py:True)
     (if (pyhost:has-await $e) (py:True)
       (if (pyhost:has-await $v) (py:True)
         (if (pyhost:gens-await $gs true) (py:True) (py:False))))))
(= (pyhost:any-async-gen $gs)
   (case $gs
     (((LNil) false)
      ((LCons (py:comprehension $tg $it $ifs $ia) $rest) (if (== $ia 1) true (pyhost:any-async-gen $rest))))))
(= (pyhost:gens-await $gs $first)
   (case $gs
     (((LNil) false)
      ((LCons (py:comprehension $tg $it $ifs $ia) $rest)
       (if (if $first false (pyhost:has-await $it)) true
         (if (pyhost:has-await $ifs) true (pyhost:gens-await $rest false)))))))
(= (pyhost:has-await $x)
   (if (== (get-metatype $x) Expression)
       (if (== (size-atom $x) 0) false
         (let $h (car-atom $x)
           (if (== $h py:Await) true
             (if (is-member $h (py:Lambda py:GeneratorExp py:FunctionDef py:AsyncFunctionDef py:ClassDef)) false
               (pyhost:any-has-await (cdr-atom $x))))))
       false))
(= (pyhost:any-has-await $xs)
   (if (== $xs ()) false (if (pyhost:has-await (car-atom $xs)) true (pyhost:any-has-await (cdr-atom $xs)))))

; ---------------------------------------------------------------- type parameters (PEP 695)

; a type variable's bound: none, constraints (a tuple) or a bound
(= (pyhost:bound-kind $b)
   (case $b (((py:None) (c:no-bound)) ((py:Tuple $es $cx $l $c $el $ec) (c:constraints)) ($other (c:bound)))))
; the child table of KIND at POS (a type variable's bound and its default
; are blocks each standing where its expression does)
(= (pyhost:child-table-nth $tabs $kind $pos (c:first)) (pyhost:child-table $tabs $kind $pos))
(= (pyhost:generic-name $nm) (str:concat (str:concat "<generic parameters of " $nm) ">"))
; a generic function's type parameter scope takes .defaults when the function
; has positional defaults and .kwdefaults when it has keyword-only ones
(= (pyhost:tp-arguments (py:arguments $po $pa $va $ko $kd $kw $ds))
   (let $p1 (if (== $ds (LNil)) (LNil) (LCons (py:arg ".defaults" (py:None) (py:None) 0 0 0 0) (LNil)))
     (let $p2 (if (== (pyhost:kw-default-names $ko $kd) (LNil)) $p1
                (pyhost:lsnoc $p1 (py:arg ".kwdefaults" (py:None) (py:None) 0 0 0 0)))
       (py:arguments (LNil) $p2 (py:None) (LNil) (LNil) (py:None) (LNil)))))
(= (pyhost:lsnoc $l $x) (case $l (((LNil) (LCons $x (LNil))) ((LCons $y $rest) (LCons $y (pyhost:lsnoc $rest $x))))))
(= (pyhost:count-indexes $l) (pyhost:indexes-from $l 0))
(= (pyhost:indexes-from $l $i) (case $l (((LNil) (LNil)) ((LCons $x $rest) (LCons $i (pyhost:indexes-from $rest (+ $i 1)))))))
(= (pyhost:kw-default-names $ko $kd)
   (case $ko
     (((LNil) (LNil))
      ((LCons (py:arg $an $aa $at $al $ac $ael $aec) $rest)
       (case $kd
         (((LCons $d $ds) (if (== $d (py:None)) (pyhost:kw-default-names $rest $ds) (LCons $an (pyhost:kw-default-names $rest $ds))))))))))
(= (pyhost:star-default $d) (case $d (((py:Starred $v $cx $l $c $el $ec) (c:unpack-one $v)) ($other $d))))
(= (pyhost:tp-default-error $nm)
   (str:concat (str:concat "non-default type parameter '" $nm) "' follows default type parameter"))
