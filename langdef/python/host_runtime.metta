; Python's runtime on CeTTa's PeTTa lane, what Machine.module asks the host:
; the heap, frames, the value of an atom, one core operation, and the
; primitives behind them.  Values are CeTTa's: ints (bigints), floats,
; strings, True and False; None is (py:None).  A dict is (py:dict ID), its
; entries (h:entry ID KEY VALUE) in the heap space; a frame is
; (m:frame LOCALS GLOBALS NAMESPACE CODE), LOCALS the slots' values.
!(import! &self str)
!(import! &self system)
!(import! &self fs)
!(import! &self binary)
!(import! &self float)
!(import! &self integer)
!(import! &self host_objects)
!(import! &self builtin_types)
!(import! &self host_slots)

!(bind! &py-heap (new-space))
!(bind! &py-ids (new-state 0))
; the exceptions being handled, innermost first (CPython's exc_info stack)
!(bind! &py-handled (new-state ()))

(= (pyrt:new-id) (let $n (get-state &py-ids) (let $s (change-state! &py-ids (+ $n 1)) $n)))

; ---------------------------------------------------------------- frames

(= (pyrt:unbound-n $n) (if (== $n 0) () (cons-atom (m:unbound) (pyrt:unbound-n (- $n 1)))))
; the main module's frame: its globals the namespace of the __main__ module
; the prelude makes (sys.modules["__main__"], its file the source being run)
(= (pyrt:module-frame (c:code $nm $q $a $ns $cs $fs $fl $body $cofile))
   (case (pyrt:prelude-sync "_main_namespace" (cons-atom (pyrt:main-path) (cons-atom (python:absolute-path (pyrt:main-path)) ())))
     (((m:val $g) (m:frame (pyrt:unbound-n $ns) $g $g (c:code $nm $q $a $ns $cs $fs $fl $body $cofile))))))
(= (pyrt:main-path)
   (let $ps (collapse (match &self (pipeline:current-source $p) $p))
     (if (== $ps ()) "" (car-atom $ps))))
; a module's code compiled from its file by the stages before the run, and a
; module's code run with its namespace as its globals
; ("ok", CODE, WARNINGS) or ("error", WHY, WARNINGS), with the compilation's
; SyntaxWarnings for the prelude to show (_show_compile_warnings)
(= (pyrt:compile-file $path)
   (let $ast (python:ast-of-file $path)
     (case $ast
       (((Error $where $why) (m:val (py:tuple "error" (pyrt:text-of-why $why) (py:tuple))))
        ($a (let $r (python:core-of-ast $a $path)
              (let $ws (pyrt:compile-warnings)
                (case $r
                  (((Error $w $y) (m:val (py:tuple "error" (pyrt:text-of-why $y) $ws)))
                   ($code (m:val (py:tuple "ok" $code $ws))))))))))))
; the last compilation's SyntaxWarnings as a tuple of (MESSAGE, LINE, COL,
; END LINE, END COL)
(= (pyrt:compile-warnings) (cons-atom py:tuple (pyrt:warning-tuples (get-state &py-compile-warnings))))
(= (pyrt:warning-tuples $ws)
   (if (== $ws ()) ()
     (let (c:syntax-warning $m $l $c $el $ec) (car-atom $ws)
       (let $rest (pyrt:warning-tuples (cdr-atom $ws)) (cons-atom (py:tuple $m $l $c $el $ec) $rest)))))
(= (pyrt:text-of-why $why) (if (pyrt:is-str $why) $why (repr $why)))
; the str a bytes view's bytes are as UTF-8, or the byte offset where they
; are not: ("ok", STR) or ("error", OFFSET)
(= (pyrt:utf8-of-view $v)
   (let $raw (str:from-bytes (str:codepoint-values $v))
     (case (str:codepoint-count $raw)
       (((Error $call (StrMalformedUtf8V1 $at)) (py:tuple "error" $at))
        ($n (py:tuple "ok" $raw))))))
; an fs library result for the prelude: ("ok", TEXT or None) or ("error", WHY)
(= (pyrt:fs-outcome $r)
   (case $r
     (((Error $call $why) (py:tuple "error" $why))
      ($ok (py:tuple "ok" (if (pyrt:is-str $ok) $ok (py:None)))))))
; compile(): the stages before the run on a text, read as a file (exec) or
; as eval_input (eval): ("ok", CODE), or the refusal for the prelude to raise:
; ("read", WHY) the reader's, ("error", MESSAGE, LINE, COLUMN, END LINE, END
; COLUMN) a stage's (columns 0-based); each with the compilation's
; SyntaxWarnings last, for the prelude to show first
(= (pyrt:compile-source $text $file $mode)
   (let $ast (if (== $mode "eval") (python:ast-eval-of-text $text) (python:ast-of-text $text))
     (case $ast
       (((Error $where $why) (m:val (pyrt:with-warnings (pyrt:compile-failure $where $why) (py:tuple))))
        ($a (let $r (python:core-of-ast $a $file)
              (let $ws (pyrt:compile-warnings)
                (case $r
                  (((Error $w $y) (m:val (pyrt:with-warnings (pyrt:compile-failure $w $y) $ws)))
                   ($code (m:val (py:tuple "ok" (pyrt:code-object $code $file) $ws))))))))))))
; compile(..., PyCF_ONLY_AST) and ast.parse: the ast stage's data for a
; text, as values the held _ast makes nodes of (the same refusals as
; compile-source)
(= (pyrt:parse-source $text $mode)
   (let $ast (if (== $mode "eval") (python:ast-eval-of-text $text) (python:ast-of-text $text))
     (case $ast
       (((Error $where $why) (m:val (pyrt:with-warnings (pyrt:compile-failure $where $why) (py:tuple))))
        ($a (m:val (py:tuple "ok" (pyrt:ast-encode $a) (pyrt:compile-warnings))))))))
; a node (py:NAME field... attribute...) as the tuple ("NAME", ...); a list
; as a list; a constant as the value the core stage makes of it
(= (pyrt:ast-encode $a)
   (case $a
     (((LCons $x $rest) (pyrt:list-new (pyrt:ast-encode-items $a)))
      ((LNil) (pyrt:list-new ()))
      ($other
       (if (== (get-metatype $a) Expression)
           (if (pyrt:ast-value-head (car-atom $a)) (pyhost:core-value $a)
               (cons-atom py:tuple (cons-atom (pyrt:ast-node-name (car-atom $a)) (pyrt:ast-encode-args (cdr-atom $a)))))
           (pyhost:core-value $a))))))
(= (pyrt:ast-encode-items $l)
   (case $l (((LCons $x $rest) (cons-atom (pyrt:ast-encode $x) (pyrt:ast-encode-items $rest))) ($nil ()))))
(= (pyrt:ast-encode-args $xs)
   (if (== $xs ()) () (cons-atom (pyrt:ast-encode (car-atom $xs)) (pyrt:ast-encode-args (cdr-atom $xs)))))
(= (pyrt:ast-value-head $h) (pyrt:in-flat $h (py:True py:False py:None py:Ellipsis py:Bytes py:Complex)))
(= (pyrt:ast-node-name $h) (let $s (str:symbol->string $h) (str:byte-slice $s 3 (str:byte-length $s))))
; compile() of a node: its data (from _ast._to_encoding) back to the ast
; stage's atoms (("$v", X) is the value X), then the stages after it
(= (pyrt:compile-ast $enc $file)
   (let $r (python:core-of-ast (pyrt:ast-decode $enc) $file)
     (let $ws (pyrt:compile-warnings)
       (case $r
         (((Error $w $y) (m:val (pyrt:with-warnings (pyrt:compile-failure $w $y) $ws)))
          ($code (m:val (py:tuple "ok" (pyrt:code-object $code $file) $ws))))))))
(= (pyrt:ast-decode $v)
   (if (pyrt:is-tuple $v)
       (if (== (cdr-atom $v) ()) $v
         (let $first (index-atom $v 1)
           (if (pyrt:is-str $first)
               (if (== $first "$v") (index-atom $v 2)
                   (cons-atom (str:string->symbol (str:concat "py:" $first)) (pyrt:ast-decode-args (cdr-atom (cdr-atom $v)))))
               $v)))
       (case $v
         (((py:list $id) (pyrt:ast-decode-list (pyrt:list-items $v)))
          ($other (if (== $v True) (py:True) (if (== $v False) (py:False) $v)))))))
(= (pyrt:ast-decode-args $xs)
   (if (== $xs ()) () (cons-atom (pyrt:ast-decode (car-atom $xs)) (pyrt:ast-decode-args (cdr-atom $xs)))))
(= (pyrt:ast-decode-list $xs)
   (if (== $xs ()) (LNil) (LCons (pyrt:ast-decode (car-atom $xs)) (pyrt:ast-decode-list (cdr-atom $xs)))))
; a compile() refusal with the warnings before it last
(= (pyrt:with-warnings $t $ws) (cons-atom py:tuple (pyrt:snoc (cdr-atom $t) $ws)))
(= (pyrt:snoc $xs $x) (if (== $xs ()) (cons-atom $x ()) (let $r (pyrt:snoc (cdr-atom $xs) $x) (cons-atom (car-atom $xs) $r))))
(= (pyrt:compile-failure $where $why)
   (case $why
     (((python:syntax-error $msg $l $c $el $ec) (py:tuple "error" $msg $l $c $el $ec))
      ($other (if (== $where (python:read)) (py:tuple "read" $why) (py:tuple "error" (pyrt:text-of-why $why) 1 0 1 1))))))
; a code object (compile()'s): its core code, its name, the file it came from
(= (pyrt:code-object (c:code $nm $q $a $ns $cs $fs $fl $body $cofile) $file)
   (let $o (pyrt:new-object (py:type code))
     (let $u (pyrt:set-field $o core (c:code $nm $q $a $ns $cs $fs $fl $body $cofile))
       (let $v (pyrt:set-field $o filename $file)
         (let $w (pyrt:set-field $o name $nm) $o)))))
; exec() and eval() of a code object: its body run with GLOBALS, and LOCALS
; the namespace its names are read from and stored in
(= (pyrt:run-code $co $g $l)
   (let (c:code $nm $q $a $ns $cs $fs $fl $body $cofile) (pyrt:field $co core)
     (pyrt:run-to-value $body (m:frame (pyrt:unbound-n $ns) $g $l (c:code $nm $q $a $ns $cs $fs $fl $body $cofile)))))
(= (pyrt:exec-module (c:code $nm $q $a $ns $cs $fs $fl $body $cofile) $d)
   (pyrt:run-to-value $body (m:frame (pyrt:unbound-n $ns) $d $d (c:code $nm $q $a $ns $cs $fs $fl $body $cofile))))
(= (pyrt:tuple-set $l $i $v)
   (if (== $i 0) (cons-atom $v (cdr-atom $l))
     (cons-atom (car-atom $l) (pyrt:tuple-set (cdr-atom $l) (- $i 1) $v))))
(= (pyrt:append $a $b) (if (== $a ()) $b (cons-atom (car-atom $a) (pyrt:append (cdr-atom $a) $b))))
(= (pyrt:nth-name $names $i)
   (case $names (((LCons $n $rest) (if (== $i 0) $n (pyrt:nth-name $rest (- $i 1)))) ($none "?"))))

(= (pyrt:store (c:local $i) $v (m:frame $l $g $ns $c))
   (m:frame (pyrt:tuple-set $l $i $v) $g $ns $c))
(= (pyrt:store (c:tmp $i) $v (m:frame $l $g $ns $c))
   (m:frame (pyrt:tuple-set $l $i $v) $g $ns $c))
(= (pyrt:store (c:cell $i) $v (m:frame $l $g $ns $c))
   (let $s (pyrt:cell-set (index-atom $l $i) $v) (m:frame $l $g $ns $c)))
(= (pyrt:store (c:dict-or-cell $i $n) $v (m:frame $l $g $ns $c))
   (let $s (pyrt:cell-set (index-atom $l $i) $v) (m:frame $l $g $ns $c)))
; STORE_NAME: an exact dict takes the name; any other namespace (a class
; body's __prepare__ mapping) is given it by PyObject_SetItem, an operation
; the machine runs (_store_name: ns[name] = value)
(= (pyrt:store (c:name $n) $v (m:frame $l $g $ns $c))
   (case $ns
     (((py:dict $di) (let $s (pyrt:dict-set $ns $n $v) (m:frame $l $g $ns $c)))
      ($other (m:via (pyrt:prelude-call-op "_store_name" (LCons (c:const $ns) (LCons (c:const $n) (LCons (c:const $v) (LNil)))))
                     (m:frame $l $g $ns $c))))))
; an operation calling a prelude function on constant arguments
(= (pyrt:prelude-call-op $name $args) (c:call (c:const (pyrt:prelude $name)) $args (LNil)))
(= (pyrt:store (c:global $n) $v (m:frame $l $g $ns $c))
   (let $s (pyrt:dict-set $g $n $v) (m:frame $l $g $ns $c)))
(= (pyrt:store (c:discard) $v (m:frame $l $g $ns $c))
   (m:frame $l $g $ns $c))

; ---------------------------------------------------------------- names

(= (pyrt:exc $type $msg) (py:exc $type $msg))
(= (pyrt:quoted $x) (str:concat (str:concat "'" $x) "'"))

(= (pyrt:atom (c:const $v) (m:frame $l $g $ns $c))
   (m:val $v))
(= (pyrt:atom (c:const-set $vs) (m:frame $l $g $ns $c))
   (m:val (pyrt:const-set $vs)))
(= (pyrt:atom (c:common (c:cc-not-implemented-error)) (m:frame $l $g $ns $c))
   (m:val (py:type NotImplementedError)))
(= (pyrt:atom (c:const-set-rebuilt $vs) (m:frame $l $g $ns $c))
   (m:val (pyrt:const-set-as $vs true)))
(= (pyrt:atom (c:tmp $i) (m:frame $l $g $ns $c))
   (m:val (index-atom $l $i)))
(= (pyrt:atom (c:local $i) (m:frame $l $g $ns $c))
   (pyrt:local-value (index-atom $l $i) $i $c))
(= (pyrt:atom (c:cell $i) (m:frame $l $g $ns $c))
   (pyrt:cell-value (index-atom $l $i) $i $c))
(= (pyrt:atom (c:cellref $i) (m:frame $l $g $ns $c))
   (m:val (index-atom $l $i)))
(= (pyrt:atom (c:dict-or-cell $i $n) (m:frame $l $g $ns $c))
   (case (pyrt:mapping-lookup $ns $n)
     (((m:val (py:null)) (pyrt:cell-value (index-atom $l $i) $i $c))
      ((m:val $v) (pyrt:read-value $v $n $ns))
      ($e $e))))
; a class body's namespace (LOAD_LOCALS)
(= (pyrt:atom (c:namespace) (m:frame $l $g $ns $c)) (m:val $ns))
; LOAD_FROM_DICT_OR_GLOBALS: a scope that sees a class reads a name from the
; class's namespace (its __classdict__ cell), else from globals and builtins
(= (pyrt:atom (c:dict-or-global $i $n) (m:frame $l $g $ns $c))
   (case (pyrt:cell-value (index-atom $l $i) $i $c)
     (((m:val $d) (case (pyrt:mapping-lookup $d $n)
                    (((m:val (py:null)) (pyrt:global-value $n $g))
                     ((m:val $v) (pyrt:read-value $v $n $d))
                     ($r $r))))
      ($e $e))))
; PyMapping_GetOptionalItem: (m:val (py:null)) when the key is missing
(= (pyrt:mapping-lookup $d $n)
   (case $d
     (((py:dict $id) (let $v (pyrt:dict-get $d $n) (m:val (if (== $v (m:missing)) (py:null) $v))))
      ($other (pyrt:prelude-sync "_mapping_get_optional" (cons-atom $d (cons-atom $n ())))))))
(= (pyrt:atom (c:name $n) (m:frame $l $g $ns $c))
   (pyrt:name-value $n $ns $g))
(= (pyrt:atom (c:global $n) (m:frame $l $g $ns $c))
   (pyrt:global-value $n $g))
(= (pyrt:local-value $v $i (c:code $nm $q (c:args $npo $np $nk $st $ds $names) $ns $cs $fs $fl $body $cofile))
   (if (== $v (m:unbound))
       (m:err (pyrt:exc "UnboundLocalError"
                (str:concat (str:concat "cannot access local variable " (pyrt:quoted (pyrt:nth-name $names $i)))
                            " where it is not associated with a value")))
       (m:val $v)))
; a cell's content; unbound, CPython's words for a free variable or a local
(= (pyrt:cell-value (py:cell $id) $i (c:code $nm $q (c:args $npo $np $nk $st $ds $names) $ns $cs $fs $fl $body $cofile))
   (let $v (pyrt:heap-one (h:cell $id $x) $x)
     (if (== $v (m:unbound))
         (if (pyrt:in-lcons $i $fs)
             (m:err (pyrt:name-error (pyrt:nth-name $names $i) (str:concat (str:concat "cannot access free variable " (pyrt:quoted (pyrt:nth-name $names $i)))
                                                      " where it is not associated with a value in enclosing scope")))
             (m:err (pyrt:exc "UnboundLocalError" (str:concat (str:concat "cannot access local variable " (pyrt:quoted (pyrt:nth-name $names $i)))
                                                             " where it is not associated with a value"))))
         (m:val $v))))
(= (pyrt:in-lcons $x $l) (case $l (((LNil) false) ((LCons $y $rest) (if (== $x $y) true (pyrt:in-lcons $x $rest))))))
(= (pyrt:cell-new $v) (let $id (pyrt:new-id) (let $a (add-atom &py-heap (h:cell $id $v)) (py:cell $id))))
; a cell's contents (cell_get_contents): its value, else ValueError
(= (pyrt:cell-contents (py:cell $id))
   (let $v (pyrt:heap-one (h:cell $id $x) $x)
     (if (== $v (m:unbound)) (m:err (pyrt:exc "ValueError" "Cell is empty")) (m:val $v))))
(= (pyrt:cell-set (py:cell $id) $v)
   (let $old (pyrt:heap-one (h:cell $id $x) $x) (pyrt:heap-replace (h:cell $id $old) (h:cell $id $v))))

; LOAD_NAME: the namespace by PyMapping_GetOptionalItem (an exact dict
; directly, any other mapping through its __getitem__), then globals, then
; builtins
(= (pyrt:name-value $n $ns $g)
   (case $ns
     (((py:dict $di)
       (let $v (pyrt:dict-get $ns $n)
         (if (== $v (m:missing))
             (if (== $ns $g) (pyrt:builtin-value $n) (pyrt:global-value $n $g))
             (pyrt:read-value $v $n $ns))))
      ($other
       (case (pyrt:mapping-lookup $ns $n)
         (((m:val (py:null)) (pyrt:global-value $n $g))
          ((m:val $mv) (pyrt:read-value $mv $n $ns))
          ($me $me)))))))
(= (pyrt:global-value $n $g)
   (let $v (pyrt:dict-get $g $n)
     (if (== $v (m:missing)) (pyrt:builtin-value $n) (pyrt:read-value $v $n $g))))
; a value read by name from NAMESPACE: a lazy import's placeholder is
; resolved and put in its place (_PyLazyImport_Reify), the import run to its
; end as the read's own work
(= (pyrt:read-value $v $n $d)
   (case $v
     (((py:obj (lazy $k)) (pyrt:prelude-sync "_lazy_reify" (cons-atom $v (cons-atom $n (cons-atom $d ())))))
      ($o (m:val $o)))))
(= (pyrt:builtin-value $n)
   (let $b (pyrt:builtin $n)
     (if (== $b (m:missing))
         (m:err (pyrt:name-error $n (str:concat (str:concat "name " (pyrt:quoted $n)) " is not defined")))
         (m:val $b))))
; a NameError CPython raises about a name (format_exc_check_arg): the name kept
; as its name attribute, for the traceback's suggestion
(= (pyrt:name-error $n $msg)
   (let $e (pyrt:exc-new (py:type NameError) (cons-atom $msg ())) (let $u (pyrt:set-field $e name $n) $e)))
(= (pyrt:builtin $n) (pyrt:builtin-of $n))

; ---------------------------------------------------------------- types

(= (pyrt:is-int $x)
   (if (== (get-metatype $x) Grounded)
       (if (== (get-type $x) Number) (!= (binary:portable-kind $x) Float) false)
       false))
(= (pyrt:is-bool $x) (if (== (get-metatype $x) Grounded) (== (get-type $x) Bool) false))
(= (pyrt:is-str $x) (if (== (get-metatype $x) Grounded) (== (get-type $x) String) false))
; an int's value, or a bool's as an int (bool is a subclass of int)
(= (pyrt:intish $x) (if (pyrt:is-int $x) true (pyrt:is-bool $x)))
(= (pyrt:int-of $x) (if (pyrt:is-bool $x) (if $x 1 0) $x))
; a value's type: (py:type NAME) for the builtin types (builtin_types.metta,
; generated from CPython)
(= (pyrt:type-of $x)
   (if (== $x (py:None)) (py:type NoneType)
     (if (pyrt:is-bool $x) (py:type bool)
       (if (pyrt:is-int $x) (py:type int)
         (if (pyrt:is-str $x) (py:type str)
           (if (== (get-metatype $x) Grounded) (py:type float)
             (if (pyrt:is-tuple $x) (py:type tuple)
               (case $x
                 (((py:Bytes $b) (py:type bytes))
                  ((py:func $fid) (py:type function))
                  ((py:builtin $b) (py:type builtin_function_or_method))
                  ((py:method $o $n) (py:type builtin_function_or_method))
                  ((py:bmeth $bt $bn) (pyrt:bmeth-type $bt $bn))
                  ((py:bmeth-bound $bt $bn $bs) (pyrt:bmeth-bound-type $bt $bn))
                  ((py:list $i) (py:type list)) ((py:dict $i) (py:type dict)) ((py:set $i) (py:type set)) ((py:frozenset $i) (py:type frozenset))
                  ((py:range $a $b $c) (py:type range)) ((py:slice $a $b $c) (py:type slice))
                  ((py:iter $i) (pyrt:iter-type $i))
                  ((py:view keys $d) (py:type dict_keys)) ((py:view values $d) (py:type dict_values))
                  ((py:view items $d) (py:type dict_items))
                  ((py:cell $i) (py:type cell))
                  ((py:gen $i) (py:type generator)) ((py:coro $i) (py:type coroutine)) ((py:agen $i) (py:type async_generator))
                  ((py:type $t) (if (pyrt:builtin-class $t) (py:type type) (pyrt:field $x meta)))
                  ((py:obj $id) (pyrt:field $x class))
                  ((py:bound $i $f $s) (py:type method))
                  ((py:getset $n) (py:type getset_descriptor))
                  ((py:NotImplemented) (py:type NotImplementedType))
                  ((py:Ellipsis) (py:type ellipsis))
                  ((py:exc $t $m) (py:type (str:string->symbol $t)))
                  ($o (py:type object)))))))))))
; a value's type's tp_name, the name CPython's messages give it
(= (pyrt:type-name $x) (pyrt:tp-name-of (pyrt:type-of $x)))
; a type's tp_name: a builtin type of a module other than builtins is
; MODULE.NAME (CPython's __module__ and __name__ of such a type are its
; tp_name split at the last dot), any other type its name
(= (pyrt:tp-name-of (py:type $t))
   (if (pyrt:builtin-class $t)
       (let (bt $name $m $bases $mro $sub $hd) (pyrt:btype $t) (if (== $m "builtins") $name (str:concat (str:concat $m ".") $name)))
       (pyrt:field (py:type $t) name)))
; a class: (py:type ID); a builtin class's ID is its name (a symbol), its
; facts in builtin_types.metta; a program's class's ID is a number, its facts
; fields in the heap: meta, name, qualname, bases, mro, dict
(= (pyrt:builtin-class $t) (== (get-metatype $t) Symbol))
(= (pyrt:type-name-of (py:type $t))
   (if (pyrt:builtin-class $t) (let (bt $name $m $bases $mro $sub $hd) (pyrt:btype $t) $name) (pyrt:field (py:type $t) name)))
(= (pyrt:qualname-of (py:type $t))
   (if (pyrt:builtin-class $t) (let (bt $name $m $bases $mro $sub $hd) (pyrt:btype $t) $name) (pyrt:field (py:type $t) qualname)))
(= (pyrt:mro-of (py:type $t))
   (if (pyrt:builtin-class $t) (let (bt $name $m $bases $mro $sub $hd) (pyrt:btype $t) $mro) (cdr-atom (pyrt:field (py:type $t) mro))))
(= (pyrt:bases-of (py:type $t))
   (if (pyrt:builtin-class $t) (let (bt $name $m $bases $mro $sub $hd) (pyrt:btype $t) $bases) (cdr-atom (pyrt:field (py:type $t) bases))))
(= (pyrt:module-of-type (py:type $t))
   (if (pyrt:builtin-class $t) (let (bt $name $m $bases $mro $sub $hd) (pyrt:btype $t) $m)
     (let $v (pyrt:dict-get (pyrt:field (py:type $t) dict) "__module__") (if (pyrt:is-str $v) $v "builtins"))))
(= (pyrt:subclassable (py:type $t))
   (if (pyrt:builtin-class $t) (let (bt $name $m $bases $mro $sub $hd) (pyrt:btype $t) $sub) True))
(= (pyrt:class-has-dict (py:type $t))
   (if (pyrt:builtin-class $t) (let (bt $name $m $bases $mro $sub $hd) (pyrt:btype $t) $hd) (pyrt:field-or (py:type $t) has_dict True)))
; a class's instance layout (typeobject.c): its base (tp_base: best_base of
; its bases, for a builtin type its own), its solid base (the most derived
; class whose instances hold more than its base's), whether its instances
; have items of their own (tp_itemsize) and take weak references; a
; builtin type's from the generated table, a program's class's as type.__new__
; recorded them
(= (pyrt:type-base (py:type $t))
   (let $b (if (pyrt:builtin-class $t) (m:missing) (pyrt:field (py:type $t) base))
     (if (== $b (m:missing))
         (let $bs (pyrt:bases-of (py:type $t)) (if (== (size-atom $bs) 0) (py:None) (car-atom $bs)))
         $b)))
(= (pyrt:type-solid (py:type $t))
   (if (pyrt:builtin-class $t) (pyrt:blayout-part $t 0)
     (let $s (pyrt:field (py:type $t) solid)
       (if (== $s (m:missing)) (pyrt:type-solid (pyrt:type-base (py:type $t))) $s))))
(= (pyrt:class-varsize (py:type $t))
   (if (pyrt:builtin-class $t) (pyrt:blayout-part $t 1) (pyrt:field-or (py:type $t) varsize False)))
(= (pyrt:class-has-weakref (py:type $t))
   (if (pyrt:builtin-class $t) (pyrt:blayout-part $t 2) (pyrt:field-or (py:type $t) has_weakref True)))
(= (pyrt:blayout-part $t $i)
   (let $rows (collapse (match &self (pyrt:blayout $t $s $v $w) ($s $v $w)))
     (if (== (size-atom $rows) 0) (if (== $i 0) (py:type object) False) (index-atom (car-atom $rows) $i))))
; a class's namespace: a builtin class's is made in the heap when first asked
(= (pyrt:type-dict (py:type $t))
   (if (pyrt:builtin-class $t)
       (let $d (pyrt:heap-one (h:tdict $t $x) $x)
         (if (== $d (m:missing)) (let $n (pyrt:dict-new) (let $a (add-atom &py-heap (h:tdict $t $n)) $n)) $d))
       (pyrt:field (py:type $t) dict)))
; _PyType_Lookup: the first class in the order whose namespace has the name,
; through a cache of (tl NAME TYPE FOUND) in &py-tcache, FOUND (tl:found VALUE)
; or (tl:absent).  An entry is always what the walk would find now: a class's
; order is fixed when the class is made, and every write to a class's
; namespace forgets the entries of the name written (Smalltalk-80's method
; cache, flushed by selector).  The cache is not program state: a run starts
; with it empty.
!(bind! &py-tcache (new-space))
(= (pyrt:mro-lookup $tp $name)
   (case (collapse (match &py-tcache (tl $name $tp $f) $f))
     ((((tl:found $v)) $v)
      (((tl:absent)) (m:missing))
      ($none (pyrt:mro-lookup-keep $tp $name (pyrt:lookup-in $name (pyrt:mro-of $tp)))))))
(= (pyrt:mro-lookup-keep $tp $name $v)
   (let $f (if (== $v (m:missing)) (tl:absent) (tl:found $v))
     (let $a (add-atom &py-tcache (tl $name $tp $f)) $v)))
; NAME written in some class's namespace: its entries forgotten
(= (pyrt:mro-forget $name)
   (pyrt:tcache-remove (collapse (match &py-tcache (tl $name $t $f) (tl $name $t $f)))))
(= (pyrt:tcache-remove $es)
   (if (== $es ()) ()
     (let ($e $rest) (decons-atom $es)
       (let $r (remove-atom &py-tcache $e) (pyrt:tcache-remove $rest)))))
; the only writes to a class's namespace: NAME set to V, or deleted (true when
; it was there)
(= (pyrt:type-dict-set $tp $name $v)
   (let $u (pyrt:dict-set (pyrt:type-dict $tp) $name $v) (pyrt:mro-forget $name)))
(= (pyrt:type-dict-delete $tp $name)
   (let $r (pyrt:dict-delete (pyrt:type-dict $tp) $name) (let $u (pyrt:mro-forget $name) $r)))
; PyIter_Check: a native iterator or a generator, or an object whose type has
; __next__
(= (pyrt:is-iterator $x)
   (case $x
     (((py:iter $i) true) ((py:gen $i) true)
      ((py:obj $i) (!= (pyrt:mro-lookup (pyrt:type-of $x) "__next__") (m:missing)))
      ($o false))))
(= (pyrt:lookup-in $name $mro)
   (if (== (size-atom $mro) 0) (m:missing)
     (let ($c $rest) (decons-atom $mro)
       (let $v (pyrt:dict-get (pyrt:type-dict $c) $name)
         (if (== $v (m:missing)) (pyrt:lookup-in $name $rest) $v)))))
; an iterator's type, by what it iterates (CPython's names; the iterators
; over tuples, strings, dict views and sets get theirs with the object model)
(= (pyrt:iter-type $id)
   (case (pyrt:heap-one (h:it $id $s) $s)
     (((it:list $l $k) (py:type list_iterator)) ((it:range $a $b $c) (py:type range_iterator))
      ((it:map $f $is) (py:type map)) ((it:filter $f $i) (py:type filter)) ((it:seqproto $o $k) (py:type iterator))
      ((it:enumerate $i $n) (py:type enumerate)) ((it:zip $is) (py:type zip))
      ((it:bytes $p $q) (py:type bytes_iterator)) ((it:bytearray $p2 $q2) (py:type bytearray_iterator))
      ($other (py:type tuple_iterator)))))
; a type's repr: <class 'NAME'>, the module first unless it is builtins
(= (pyrt:type-repr $t)
   (let $m (pyrt:module-of-type $t)
     (str:concat (str:concat "<class '" (if (== $m "builtins") (pyrt:qualname-of $t) (str:concat (str:concat $m ".") (pyrt:qualname-of $t)))) "'>")))
; isinstance(x, classinfo) and issubclass(c, classinfo): a class, or a tuple
; of them (nested), in the method resolution order; the first match wins,
; before a later item could be refused
; object_recursive_isinstance: the instance's own type at once; a class whose
; type is exactly type by its MRO; a tuple or a union item by item; else the
; class's __instancecheck__ (looked up on its type), its answer's truth
(= (pyrt:isinstance $x $t)
   (if (== (pyrt:type-of $x) $t) (m:val True)
     (case $t
       (((py:type $n)
         (if (== (pyrt:type-of $t) (py:type type))
             (m:val (pyrt:in-mro $t (pyrt:mro-of (pyrt:type-of $x))))
             (pyrt:check-hook $t "__instancecheck__" $x
               (m:val (pyrt:in-mro $t (pyrt:mro-of (pyrt:type-of $x)))))))
        ($other
         (if (pyrt:is-tuple $t) (pyrt:isinstance-any $x (cdr-atom $t))
           (if (== (pyrt:type-of $t) (py:type Union)) (pyrt:isinstance-any $x (cdr-atom (pyrt:field $t args)))
             (pyrt:check-hook $t "__instancecheck__" $x
               (pyrt:class-check (pyrt:type-of $x) $t "isinstance() arg 2 must be a type, a tuple of types, or a union")))))))))
(= (pyrt:isinstance-any $x $ts)
   (if (== (size-atom $ts) 0) (m:val False)
     (let ($t $rest) (decons-atom $ts)
       (case (pyrt:isinstance $x $t)
         (((m:val True) (m:val True)) ((m:val False) (pyrt:isinstance-any $x $rest)) ($e $e))))))
; object_issubclass: a class whose type is exactly type by its MRO; a tuple
; or a union item by item; else the class's __subclasscheck__, its answer's
; truth
(= (pyrt:issubclass $c $t)
   (case $c
     (((py:type $n) (pyrt:subclass-check $c $t))
      ($other (m:err (pyrt:exc "TypeError" "issubclass() arg 1 must be a class"))))))
(= (pyrt:subclass-check $c $t)
   (case $t
     (((py:type $n)
       (if (== (pyrt:type-of $t) (py:type type))
           (m:val (pyrt:in-mro $t (pyrt:mro-of $c)))
           (pyrt:check-hook $t "__subclasscheck__" $c (m:val (pyrt:in-mro $t (pyrt:mro-of $c))))))
      ($other
       (if (pyrt:is-tuple $t) (pyrt:subclass-check-any $c (cdr-atom $t))
         (if (== (pyrt:type-of $t) (py:type Union)) (pyrt:subclass-check-any $c (cdr-atom (pyrt:field $t args)))
           (pyrt:check-hook $t "__subclasscheck__" $c
             (pyrt:class-check $c $t "issubclass() arg 2 must be a class, a tuple of classes, or a union"))))))))
(= (pyrt:subclass-check-any $c $ts)
   (if (== (size-atom $ts) 0) (m:val False)
     (let ($t $rest) (decons-atom $ts)
       (case (pyrt:subclass-check $c $t)
         (((m:val True) (m:val True)) ((m:val False) (pyrt:subclass-check-any $c $rest)) ($e $e))))))
; a class's own __instancecheck__ or __subclasscheck__ (type's own is the
; default answer), run to its end; else DEFAULT
(= (pyrt:check-hook $t $name $x $default)
   (case (pyrt:prelude-sync "_check_hook" (cons-atom $t (cons-atom $name (cons-atom $x ()))))
     (((m:val (py:null)) $default)
      ($r $r))))
(= (pyrt:class-check $c $t $msg)
   (case $t
     (((py:type $n) (m:val (pyrt:in-mro $t (pyrt:mro-of $c))))
      ((py:obj $i) (pyrt:class-check-object $c $t $msg))
      ($other (if (pyrt:is-tuple $t) (pyrt:class-check-any $c (cdr-atom $t) $msg) (m:err (pyrt:exc "TypeError" $msg)))))))
; object_recursive_isinstance: a union stands for its members; a
; parameterized generic refuses (ga_instancecheck, ga_subclasscheck)
(= (pyrt:class-check-object $c $t $msg)
   (let $k (pyrt:type-of $t)
     (if (== $k (py:type Union)) (pyrt:class-check-any $c (cdr-atom (pyrt:field $t args)) $msg)
       (if (pyrt:in-mro (py:type GenericAlias) (pyrt:mro-of $k))
           (m:err (pyrt:exc "TypeError" (if (str:starts-with $msg "isinstance")
                                            "isinstance() argument 2 cannot be a parameterized generic"
                                            "issubclass() argument 2 cannot be a parameterized generic")))
           (m:err (pyrt:exc "TypeError" $msg))))))
(= (pyrt:class-check-any $c $ts $msg)
   (if (== (size-atom $ts) 0) (m:val False)
     (let ($t $rest) (decons-atom $ts)
       (case (pyrt:class-check $c $t $msg)
         (((m:val True) (m:val True)) ((m:val False) (pyrt:class-check-any $c $rest $msg)) ($e $e))))))
(= (pyrt:in-mro $t $mro)
   (if (== (size-atom $mro) 0) False (let ($m $rest) (decons-atom $mro) (if (== $m $t) True (pyrt:in-mro $t $rest)))))
; a type's own attributes: its names, method resolution order and bases
(= (pyrt:type-attribute $t $name)
   (case $name
     (("__name__" (m:val (pyrt:type-name-of $t)))
      ("__qualname__" (m:val (pyrt:type-name-of $t)))
      ("__module__" (m:val (pyrt:module-of-type $t)))
      ("__mro__" (m:val (cons-atom py:tuple (pyrt:mro-of $t))))
      ("__bases__" (m:val (cons-atom py:tuple (pyrt:bases-of $t))))
      ("__base__" (m:val (pyrt:type-base $t)))
      ($other (m:err (pyrt:exc "AttributeError" (str:concat (str:concat (str:concat "type object " (pyrt:quoted (pyrt:tp-name-of $t))) " has no attribute ") (pyrt:quoted $name))))))))
; sm_init and cm_init: functools_wraps, the callable's __module__, __name__,
; __qualname__ and __doc__ (those it has) set on the wrapper, in its __dict__
(= (pyrt:sm-wraps $o $f)
   (let $d (pyrt:object-dict $o)
     (let $u1 (pyrt:sm-copy $d $f "__module__")
       (let $u2 (pyrt:sm-copy $d $f "__name__")
         (let $u3 (pyrt:sm-copy $d $f "__qualname__")
           (let $u4 (pyrt:sm-copy $d $f "__doc__") $o))))))
(= (pyrt:sm-copy $d $f $name)
   (case (pyrt:getattr-now $f $name) (((m:val $v) (pyrt:dict-set $d $name $v)) ($e ()))))
; calling a builtin type: type(x) is x's type; any other runs its constructor
(= (pyrt:call-type $t $xs $ks)
   (if (== $t type)
       (if (if (== (size-atom $xs) 1) (== (size-atom $ks) 0) false) (m:val (pyrt:type-of (car-atom $xs)))
           (pyrt:call-class (py:type type) $xs $ks))
       ; these classes' instances are made by their __new__ (the prelude's), as
       ; type.__call__ makes them
       (if (pyrt:in-flat $t (object super property BaseExceptionGroup ExceptionGroup GenericAlias mappingproxy bytes bytearray module frozendict sentinel Template Interpolation TypeVar ParamSpec TypeVarTuple SimpleNamespace))
           (pyrt:call-class (py:type $t) $xs $ks)
           (if (pyrt:in-flat $t (staticmethod classmethod))
               (if (if (== (size-atom $xs) 1) (== (size-atom $ks) 0) false)
                   (m:val (let $o (pyrt:obj-new (py:type $t)) (let $u (pyrt:set-field $o func (car-atom $xs)) (pyrt:sm-wraps $o (car-atom $xs)))))
                   (m:err (pyrt:exc "TypeError" (str:concat (str:concat (str:symbol->string $t) " expected 1 argument, got ") (str:number->string (size-atom $xs))))))
               ; an OSError's arguments are read by OSError.__new__ (the prelude's)
               (if (pyrt:is-exc-class (py:type $t))
                   (if (pyrt:in-mro (py:type OSError) (pyrt:mro-of (py:type $t))) (pyrt:call-class (py:type $t) $xs $ks) (pyrt:make-exception $t $xs $ks))
                   (pyrt:call-builtin-kw $t $xs $ks))))))
; calling a class: its metaclass's __call__ (type.__call__ unless overridden)
(= (pyrt:call-class $cls $xs $ks)
   (let $call (pyrt:mro-lookup (pyrt:type-of $cls) "__call__")
     (if (== $call (m:missing)) (m:err (pyrt:exc "TypeError" (str:concat (pyrt:quoted (pyrt:type-name $cls)) " object is not callable")))
         (pyrt:call-kw $call (cons-atom $cls $xs) $ks))))
; calling an instance: its class's __call__
(= (pyrt:call-object $o $xs $ks)
   (let $call (pyrt:mro-lookup (pyrt:type-of $o) "__call__")
     (if (== $call (m:missing)) (m:err (pyrt:exc "TypeError" (str:concat (pyrt:quoted (pyrt:type-name $o)) " object is not callable")))
         (pyrt:call-kw $call (cons-atom $o $xs) $ks))))

; str(): what print shows; a container's is its repr
(= (pyrt:str $x) (if (pyrt:is-str $x) $x (if (pyrt:is-exc-instance $x) (pyrt:exc-str $x) (pyrt:repr $x))))
; str(x) and repr(x) of any value, as results: an object's through its class's
; __str__ or __repr__ (Python, run from the host), a container's through its
; items'
(= (pyrt:str-of $x)
   (if (pyrt:is-str $x) (m:val $x)
     (if (pyrt:needs-python $x) (pyrt:dunder-text $x "__str__") (pyrt:repr-of $x))))
(= (pyrt:repr-of $x)
   (if (pyrt:needs-python $x) (pyrt:dunder-text $x "__repr__")
     (if (pyrt:is-tuple $x)
         (if (pyrt:repr-enter $x) (m:val "(...)")
           (pyrt:repr-leave $x
             (case (pyrt:reprs-of (cdr-atom $x) ())
               (((m:val $rs) (m:val (if (== (size-atom $rs) 1) (str:concat (str:concat "(" (car-atom $rs)) ",)")
                                        (str:concat (str:concat "(" (str:join ", " $rs)) ")"))))
                ($e $e)))))
         (case $x
           (((py:list $id) (pyrt:guarded-repr $x "[...]" "[" (pyrt:list-items $x) "]"))
            ((py:dict $id) (if (pyrt:repr-enter $x) (m:val "{...}")
                             (pyrt:repr-leave $x (case (pyrt:pair-reprs-of (pyrt:dict-entries $x) ()) (((m:val $rs) (m:val (str:concat (str:concat "{" (str:join ", " $rs)) "}"))) ($e $e))))))
            ((py:set $id) (if (== (pyrt:set-len $x) 0) (m:val "set()") (pyrt:guarded-repr $x "set(...)" "{" (pyrt:set-items $x) "}")))
            ((py:frozenset $id) (if (== (pyrt:set-len $x) 0) (m:val "frozenset()") (pyrt:guarded-repr $x "frozenset(...)" "frozenset({" (pyrt:set-items $x) "})")))
            ((py:view keys $d) (pyrt:guarded-repr $x "..." "dict_keys([" (pyrt:dict-keys $d) "])"))
            ((py:view values $d) (pyrt:guarded-repr $x "..." "dict_values([" (pyrt:dict-values $d) "])"))
            ((py:view items $d) (pyrt:guarded-repr $x "..." "dict_items([" (pyrt:pairs-as-tuples (pyrt:dict-entries $d)) "])"))
            ((py:bound $id $f $s) (pyrt:bound-method-repr $f $s))
            ((py:slice $sa $sb $sc) (pyrt:reprs-within "slice(" ($sa $sb $sc) ")"))
            ($other (m:val (pyrt:repr $x))))))))
; method_repr: <bound method QUALNAME of REPR(self)>, the function's
; __qualname__, else its __name__, else ?
(= (pyrt:bound-method-repr $f $s)
   (let $q (case (pyrt:getattr-now $f "__qualname__")
             (((m:val $n) (if (pyrt:is-str $n) $n "?"))
              ($e (case (pyrt:getattr-now $f "__name__") (((m:val $n2) (if (pyrt:is-str $n2) $n2 "?")) ($e2 "?"))))))
     (case (pyrt:repr-of $s)
       (((m:val $r) (m:val (str:concat (str:concat (str:concat (str:concat "<bound method " $q) " of ") $r) ">")))
        ($e $e)))))
; Py_ReprEnter and Py_ReprLeave: the containers whose repr is being made,
; latest first.  A container met again inside its own repr shows as DOTS
; ([...], {...}, (...)), as CPython's list, dict, set, tuple and dict views
; show it; a program's __repr__ has no such guard.  A tuple is a value here,
; not an object: an equal tuple met inside its own repr counts as itself.
!(bind! &py-repr-busy (new-state ()))
(= (pyrt:repr-enter $x)
   (let $busy (get-state &py-repr-busy)
     (if (pyrt:repr-busy-has $busy $x) true
       (let $u (change-state! &py-repr-busy (cons-atom $x $busy)) false))))
(= (pyrt:repr-leave $x $r)
   (let $u (change-state! &py-repr-busy (pyrt:repr-busy-drop (get-state &py-repr-busy) $x)) $r))
(= (pyrt:repr-busy-has $busy $x)
   (if (== $busy ()) false (if (== (car-atom $busy) $x) true (pyrt:repr-busy-has (cdr-atom $busy) $x))))
(= (pyrt:repr-busy-drop $busy $x)
   (if (== $busy ()) ()
     (if (== (car-atom $busy) $x) (cdr-atom $busy) (cons-atom (car-atom $busy) (pyrt:repr-busy-drop (cdr-atom $busy) $x)))))
(= (pyrt:guarded-repr $x $dots $open $xs $close)
   (if (pyrt:repr-enter $x) (m:val $dots) (pyrt:repr-leave $x (pyrt:reprs-within $open $xs $close))))
; a container's repr: its items' reprs (each through its class's __repr__)
; between its brackets
(= (pyrt:reprs-within $open $xs $close)
   (case (pyrt:reprs-of $xs ()) (((m:val $rs) (m:val (str:concat (str:concat $open (str:join ", " $rs)) $close))) ($e $e))))
(= (pyrt:pair-reprs-of $ps $acc)
   (if (== (size-atom $ps) 0) (m:val (pyrt:rev-onto $acc ()))
     (let ((py:pair $k $v) $rest) (decons-atom $ps)
       (case (pyrt:repr-of $k)
         (((m:val $rk) (case (pyrt:repr-of $v)
                         (((m:val $rv) (pyrt:pair-reprs-of $rest (cons-atom (str:concat (str:concat $rk ": ") $rv) $acc)))
                          ($e $e))))
          ($e $e))))))
(= (pyrt:reprs-of $xs $acc)
   (if (== (size-atom $xs) 0) (m:val (pyrt:rev-onto $acc ()))
     (let ($x $rest) (decons-atom $xs)
       (case (pyrt:repr-of $x) (((m:val $r) (pyrt:reprs-of $rest (cons-atom $r $acc))) ($e $e))))))
; a str() or repr() result's text, for a message (a failing one shows as
; CPython shows it)
(= (pyrt:text-of $r) (case $r (((m:val $s) $s) ($e "<exception str() failed>"))))
; an object whose operations are its type's protocol, run in Python: a
; program's object, and every class (its operations are its metaclass's, as
; type | type and list[int] are type's)
(= (pyrt:needs-python $x)
   (case $x (((py:obj $id) true) ((py:type $t) true) ($o false))))
(= (pyrt:dunder-text $x $name)
   (let $f (pyrt:mro-lookup (pyrt:type-of $x) $name)
     (case (pyrt:call-sync $f (cons-atom $x ()) ())
       (((m:val $s) (if (pyrt:is-str $s) (m:val $s)
                        (m:err (pyrt:exc "TypeError" (str:concat (str:concat (str:concat (str:concat (pyrt:type-fqn (pyrt:type-of $x)) ".") $name) "() must return a str, not ") (pyrt:type-fqn (pyrt:type-of $s)))))))
        ($e $e)))))
; the scalars' and functions' text (repr's base cases)
(= (pyrt:scalar-str $x)
   (if (== $x (py:None)) "None"
     (if (pyrt:is-bool $x) (if $x "True" "False")
       (if (pyrt:is-int $x) (str:number->string $x)
         (if (pyrt:is-float $x) (pyrt:float-repr $x)
         (case $x
           (((py:func $fid) (str:concat (str:concat (str:concat (str:concat "<function " (pyrt:field $x qualname)) " at ") (pyrt:address $x)) ">"))
            ((py:builtin $b) (str:concat (str:concat "<built-in function " (str:symbol->string $b)) ">"))
            ((py:type $t) (pyrt:type-repr $x))
            ((py:method $o $n) (pyrt:bmeth-bound-repr (pyrt:kind-of $o) $n $o))
            ((py:NotImplemented) "NotImplemented")
            ((py:Ellipsis) "Ellipsis")
            ($o (repr $o)))))))))

(= (pyrt:truthy $x)
   (if (== $x (py:None)) false
     (if (pyrt:is-bool $x) $x
       (if (pyrt:is-int $x) (!= $x 0)
         (if (pyrt:is-float $x) (not (pyrt:float-zero $x))
         (if (pyrt:is-str $x) (!= $x "")
           (if (pyrt:is-tuple $x) (> (size-atom $x) 1)
             (case $x
               (((py:Bytes $b) (!= $b ""))
                ((py:list $i) (> (pyrt:list-len $x) 0))
                ((py:dict $i) (> (pyrt:dict-len $x) 0))
                ((py:set $i) (> (pyrt:set-len $x) 0))
                ((py:frozenset $i) (> (pyrt:set-len $x) 0))
                ((py:range $a $b $c) (> (pyrt:range-len $a $b $c) 0))
                ($o true))))))))))

; ---------------------------------------------------------------- operations

(= (pyrt:eval (c:atom $a) (m:frame $l $g $ns $c))
   (pyrt:atom $a (m:frame $l $g $ns $c)))
(= (pyrt:eval (c:binop $o $a $b) (m:frame $l $g $ns $c))
   (pyrt:bin $o (pyrt:atom $a (m:frame $l $g $ns $c)) (pyrt:atom $b (m:frame $l $g $ns $c))))
(= (pyrt:eval (c:inplace $o $a $b) (m:frame $l $g $ns $c))
   (pyrt:with2 (pyrt:atom $a (m:frame $l $g $ns $c)) (pyrt:atom $b (m:frame $l $g $ns $c)) (inplace $o)))
(= (pyrt:eval (c:compare $o $a $b) (m:frame $l $g $ns $c))
   (pyrt:cmp $o (pyrt:atom $a (m:frame $l $g $ns $c)) (pyrt:atom $b (m:frame $l $g $ns $c))))
(= (pyrt:eval (c:unary $o $a) (m:frame $l $g $ns $c))
   (pyrt:un $o (pyrt:atom $a (m:frame $l $g $ns $c))))
(= (pyrt:eval (c:call $fa $args $kws) (m:frame $l $g $ns $c))
   (case (pyrt:atom $fa (m:frame $l $g $ns $c)) (((m:val (py:type super)) (if (if (== $args (LNil)) (== $kws (LNil)) false) (pyrt:zero-arg-super (m:frame $l $g $ns $c)) (pyrt:call-on (m:val (py:type super)) (pyrt:atoms $args (m:frame $l $g $ns $c)) (pyrt:kw-atoms $kws (m:frame $l $g $ns $c))))) ((m:val (py:builtin $b)) (if (pyrt:reads-frame $b) (case (pyrt:atoms $args (m:frame $l $g $ns $c)) (((m:val $xs) (case (pyrt:kw-atoms $kws (m:frame $l $g $ns $c)) (((m:val $ks) (pyrt:frame-builtin $b $xs $ks (m:frame $l $g $ns $c))) ($e $e)))) ($e $e))) (pyrt:call-on (m:val (py:builtin $b)) (pyrt:atoms $args (m:frame $l $g $ns $c)) (pyrt:kw-atoms $kws (m:frame $l $g $ns $c))))) ($rf (pyrt:call-on $rf (pyrt:atoms $args (m:frame $l $g $ns $c)) (pyrt:kw-atoms $kws (m:frame $l $g $ns $c)))))))
; obj.name(args): the method looked up before the arguments are read
(= (pyrt:eval (c:call-method $oa $name $args $kws) (m:frame $l $g $ns $c))
   (case (pyrt:atom $oa (m:frame $l $g $ns $c)) (((m:val $o) (case (pyrt:getattr-now $o $name) (((m:val $m) (pyrt:call-on (m:val $m) (pyrt:atoms $args (m:frame $l $g $ns $c)) (pyrt:kw-atoms $kws (m:frame $l $g $ns $c)))) ($e $e)))) ($e $e))))
(= (pyrt:eval (c:function $code $dflts $kwd $clo $ann) (m:frame $l $g $ns $c))
   (case (pyrt:atoms $dflts (m:frame $l $g $ns $c)) (((m:val $ds) (case (pyrt:kw-atoms $kwd (m:frame $l $g $ns $c)) (((m:val $ks) (case (pyrt:annotate-atom $ann (m:frame $l $g $ns $c)) (((m:val $a) (m:val (pyrt:new-function $code $g $ds $ks (pyrt:closure-cells $clo $l) $a))) ($e $e)))) ($e $e)))) ($e $e))))
; a function's __annotate__ function (PEP 649), or None
(= (pyrt:annotate-atom $ann $f) (if (== $ann (c:no-annotate)) (m:val (py:None)) (pyrt:atom $ann $f)))
; the compiler's intrinsics (CALL_INTRINSIC_1 and _2, Python/intrinsics.c):
; the prelude's _intrinsic_* on the operands' values
(= (pyrt:eval (c:intrinsic $i $as) (m:frame $l $g $ns $c))
   (case (pyrt:atoms $as (m:frame $l $g $ns $c))
     (((m:val $vs) (pyrt:prelude-call (pyrt:intrinsic-function $i) $vs))
      ($e $e))))
(= (pyrt:intrinsic-function (c:i-typevar)) "_intrinsic_typevar")
(= (pyrt:intrinsic-function (c:i-typevar-bound)) "_intrinsic_typevar_with_bound")
(= (pyrt:intrinsic-function (c:i-typevar-constraints)) "_intrinsic_typevar_with_constraints")
(= (pyrt:intrinsic-function (c:i-paramspec)) "_intrinsic_paramspec")
(= (pyrt:intrinsic-function (c:i-typevartuple)) "_intrinsic_typevartuple")
(= (pyrt:intrinsic-function (c:i-set-default)) "_intrinsic_set_typeparam_default")
(= (pyrt:intrinsic-function (c:i-set-function-type-params)) "_intrinsic_set_function_type_params")
(= (pyrt:intrinsic-function (c:i-typealias)) "_intrinsic_typealias")
(= (pyrt:intrinsic-function (c:i-unpack-one)) "_intrinsic_unpack_one")
(= (pyrt:intrinsic-function (c:i-subscript-generic)) "_intrinsic_subscript_generic")
; yield from's iterator (GET_YIELD_FROM_ITER): a generator as it is, else iter()
(= (pyrt:eval (c:yf-iter $a) (m:frame $l $g $ns $c))
   (case (pyrt:atom $a (m:frame $l $g $ns $c))
     (((m:val $x) (case $x (((py:gen $id) (m:val $x))
                            ((py:coro $id) (m:err (pyrt:exc "TypeError" "cannot 'yield from' a coroutine object in a non-coroutine generator")))
                            ($o (pyrt:now (pyrt:iter $x))))))
      ($e $e))))
; GET_AWAITABLE (_PyCoro_GetAwaitableIter): a coroutine is its own iterator
; (an error when it is being awaited already); another object's type's
; __await__ gives it (the prelude's)
(= (pyrt:eval (c:await-iter $a $op) (m:frame $l $g $ns $c))
   (case (pyrt:atom $a (m:frame $l $g $ns $c)) (((m:val $x) (pyrt:await-iter $x $op)) ($e $e))))
(= (pyrt:await-iter $x $op)
   (case $x
     (((py:coro $id)
       (if (if (== (pyrt:field $x state) suspended) (case (pyrt:field $x where) (((c:yf $i $t) true) ($w false))) false)
           (m:err (pyrt:exc "RuntimeError" "coroutine is being awaited already"))
           (m:val $x)))
      ($other (pyrt:now (pyrt:prelude-call "_get_awaitable_iter" (cons-atom $x (cons-atom (pyrt:aw-op $op) ()))))))))
; GET_AWAITABLE's oparg: a plain await, __aenter__'s, __aexit__'s
(= (pyrt:aw-op (c:aw-plain)) 0)
(= (pyrt:aw-op (c:aw-enter)) 1)
(= (pyrt:aw-op (c:aw-exit)) 2)
; GET_AITER, GET_ANEXT and BEFORE_ASYNC_WITH: the prelude's
(= (pyrt:eval (c:aiter $a) (m:frame $l $g $ns $c))
   (pyrt:prelude-on "_get_aiter" (LCons $a (LNil)) (m:frame $l $g $ns $c)))
(= (pyrt:eval (c:anext $a) (m:frame $l $g $ns $c))
   (pyrt:prelude-on "_get_anext" (LCons $a (LNil)) (m:frame $l $g $ns $c)))
(= (pyrt:eval (c:before-async-with $a) (m:frame $l $g $ns $c))
   (pyrt:prelude-on "_before_async_with" (LCons $a (LNil)) (m:frame $l $g $ns $c)))
; IMPORT_NAME: the builtins' __import__(name, globals, None, fromlist, level);
; an import that is lazy, or that the runtime's policy may make lazy, is the
; prelude's (_PyEval_LazyImportName), given the frame's globals and locals
(= (pyrt:eval (c:import $na $fa $la $md) (m:frame $l $g $ns $c))
   (case (pyrt:atoms (LCons $na (LCons $fa (LCons $la (LNil)))) (m:frame $l $g $ns $c))
     (((m:val ($name $fromlist $level))
       (if (== $md (c:imp-eager))
           (pyrt:call-on (m:val (pyrt:builtin-of "__import__"))
                         (m:val (cons-atom $name (cons-atom $g (cons-atom (py:None) (cons-atom $fromlist (cons-atom $level ()))))))
                         (m:val ()))
           (pyrt:prelude-call "_lazy_import_name"
             (cons-atom $name (cons-atom $g (cons-atom $ns (cons-atom $fromlist (cons-atom $level
               (cons-atom (if (== $md (c:imp-lazy)) True False) ())))))))))
      ($e $e))))
; IMPORT_FROM: the module's attribute, else its submodule
(= (pyrt:eval (c:import-from $ma $na) (m:frame $l $g $ns $c))
   (case (pyrt:atoms (LCons $ma (LCons $na (LNil))) (m:frame $l $g $ns $c))
     (((m:val ($m $name)) (pyrt:prelude-call "_import_from" (cons-atom $m (cons-atom $name ())))) ($e $e))))
; IMPORT_STAR: the module's public names into the namespace
(= (pyrt:eval (c:import-star $ma) (m:frame $l $g $ns $c))
   (case (pyrt:atom $ma (m:frame $l $g $ns $c))
     (((m:val $m) (pyrt:prelude-call "_import_star" (cons-atom $m (cons-atom $ns ())))) ($e $e))))
; an f-string's conversion of a field's value (CONVERT_VALUE): str, repr, ascii
(= (pyrt:eval (c:convert (c:conv-str) $a) (m:frame $l $g $ns $c))
   (case (pyrt:atom $a (m:frame $l $g $ns $c)) (((m:val $x) (pyrt:str-of $x)) ($e $e))))
(= (pyrt:eval (c:convert (c:conv-repr) $a) (m:frame $l $g $ns $c))
   (case (pyrt:atom $a (m:frame $l $g $ns $c)) (((m:val $x) (pyrt:repr-of $x)) ($e $e))))
(= (pyrt:eval (c:convert (c:conv-ascii) $a) (m:frame $l $g $ns $c))
   (case (pyrt:atom $a (m:frame $l $g $ns $c)) (((m:val $x) (pyrt:prelude-call "ascii" (cons-atom $x ()))) ($e $e))))
; a field formatted (FORMAT_SIMPLE, FORMAT_WITH_SPEC: PyObject_Format): a str
; with no spec as it is, an int with none as str(), else __format__
(= (pyrt:eval (c:format $a $sa) (m:frame $l $g $ns $c))
   (case (pyrt:atom $a (m:frame $l $g $ns $c))
     (((m:val $x) (case (pyrt:atom $sa (m:frame $l $g $ns $c))
                    (((m:val $spec)
                      (if (if (== $spec "") (if (pyrt:is-str $x) true (if (pyrt:is-int $x) (not (pyrt:is-bool $x)) false)) false)
                          (if (pyrt:is-str $x) (m:val $x) (m:val (pyrt:str $x)))
                          (pyrt:prelude-call "_format" (cons-atom $x (cons-atom $spec ())))))
                     ($e $e))))
      ($e $e))))
; BUILD_STRING: the parts joined
(= (pyrt:eval (c:build-string $as) (m:frame $l $g $ns $c))
   (case (pyrt:atoms $as (m:frame $l $g $ns $c)) (((m:val $xs) (m:val (pyrt:concat-all $xs ""))) ($e $e))))
; BUILD_INTERPOLATION: a template string's interpolation of a value, the text
; of its expression, its conversion (None, 's', 'r' or 'a': the value itself
; unconverted) and its format spec
(= (pyrt:eval (c:build-interpolation $a $ea $k $sa) (m:frame $l $g $ns $c))
   (pyrt:prelude-on "_interpolation_build" (LCons $a (LCons $ea (LCons (c:const (pyrt:conversion-char $k)) (LCons $sa (LNil)))))
                    (m:frame $l $g $ns $c)))
(= (pyrt:conversion-char $k)
   (case $k (((c:conv-str) "s") ((c:conv-repr) "r") ((c:conv-ascii) "a") ($none (py:None)))))
; BUILD_TEMPLATE: a template of its strings and its interpolations, each a tuple
(= (pyrt:eval (c:build-template $sa $as) (m:frame $l $g $ns $c))
   (case (pyrt:atoms (LCons $sa $as) (m:frame $l $g $ns $c))
     (((m:val $xs)
       (let ($strings $is) (decons-atom $xs)
         (pyrt:prelude-call "_template_build" (cons-atom $strings (cons-atom (cons-atom py:tuple $is) ())))))
      ($e $e))))
; pow(base, exp, mod) of ints (long_pow): mod 0 and a base with no inverse
; for a negative exponent are errors
(= (pyrt:pow-mod $b $e $m)
   (if (== $m 0) (m:err (pyrt:exc "ValueError" "pow() 3rd argument cannot be 0"))
     (case (integer:pow-mod $b $e $m)
       (((Error $call $why) (m:err (pyrt:exc "ValueError" "base is not invertible for the given modulus")))
        ($r (m:val $r))))))
; the modules written in Python that the runtime holds compiled, found by
; their full names before any path (as CPython's frozen modules are): each
; (pyrt:lib-module NAME FILE KIND), FILE its file under lib/ as CPython's Lib
; lays it out (generated/lib_FILE_v1.metta defines (pyrt:lib-code FILE));
; KIND module, package (FILE its __init__), or builtin for a module standing
; for one CPython writes in C, run as builtin code (no frames shown)
(pyrt:lib-module "math" "math" builtin)
(pyrt:lib-module "__future__" "__future__" module)
(pyrt:lib-module "linecache" "linecache" module)
(pyrt:lib-module "warnings" "warnings" module)
(pyrt:lib-module "string" "string/__init__" package)
(pyrt:lib-module "string.templatelib" "string/templatelib" module)
(pyrt:lib-module "typing" "typing" module)
(pyrt:lib-module "itertools" "itertools" builtin)
(pyrt:lib-module "_thread" "_thread" builtin)
(pyrt:lib-module "_weakref" "_weakref" builtin)
(pyrt:lib-module "_ast" "_ast" builtin)
; a held module's code and whether it is a package, else None
(= (pyrt:lib-module-code $n)
   (let $rows (collapse (match &self (pyrt:lib-module $n $file $kind) (held $file $kind)))
     (if (== $rows ()) (py:None)
       (let (held $file $kind) (car-atom $rows)
         (py:tuple (if (== $kind builtin) (pyrt:refile (pyrt:lib-code $file) (c:builtin)) (pyrt:lib-code $file))
                   (if (== $kind package) True False))))))
; the same code given another file: (c:builtin) for the prelude and the
; modules standing for CPython's C ones
(= (pyrt:refile $t $file)
   (if (== (get-metatype $t) Expression)
       (if (== $t ()) $t
         (if (if (== (car-atom $t) c:code) (== (size-atom $t) 10) false)
             (let ($h $nm $q $a $nx $cs $fs $fl $body (c:src $old $first)) $t
               (c:code $nm $q $a $nx $cs $fs $fl (pyrt:refile $body $file) (c:src $file $first)))
             (pyrt:refile-each $t $file)))
       $t))
(= (pyrt:refile-each $t $file)
   (if (== $t ()) ()
     (let ($x $rest) (decons-atom $t) (cons-atom (pyrt:refile $x $file) (pyrt:refile-each $rest $file)))))

; C's libm by name, for the math module (lib/math.py)
(= (pyrt:libm1 "acos" $x) (float:acos $x))
(= (pyrt:libm1 "acosh" $x) (float:acosh $x))
(= (pyrt:libm1 "asin" $x) (float:asin $x))
(= (pyrt:libm1 "asinh" $x) (float:asinh $x))
(= (pyrt:libm1 "atan" $x) (float:atan $x))
(= (pyrt:libm1 "atanh" $x) (float:atanh $x))
(= (pyrt:libm1 "cbrt" $x) (float:cbrt $x))
(= (pyrt:libm1 "cos" $x) (float:cos $x))
(= (pyrt:libm1 "cosh" $x) (float:cosh $x))
(= (pyrt:libm1 "erf" $x) (float:erf $x))
(= (pyrt:libm1 "erfc" $x) (float:erfc $x))
(= (pyrt:libm1 "exp" $x) (float:exp $x))
(= (pyrt:libm1 "exp2" $x) (float:exp2 $x))
(= (pyrt:libm1 "expm1" $x) (float:expm1 $x))
(= (pyrt:libm1 "fabs" $x) (float:abs $x))
(= (pyrt:libm1 "floor" $x) (float:floor $x))
(= (pyrt:libm1 "ceil" $x) (float:ceil $x))
(= (pyrt:libm1 "log" $x) (float:log $x))
(= (pyrt:libm1 "log2" $x) (float:log2 $x))
(= (pyrt:libm1 "log10" $x) (float:log10 $x))
(= (pyrt:libm1 "log1p" $x) (float:log1p $x))
(= (pyrt:libm1 "sin" $x) (float:sin $x))
(= (pyrt:libm1 "sinh" $x) (float:sinh $x))
(= (pyrt:libm1 "sqrt" $x) (float:sqrt $x))
(= (pyrt:libm1 "tan" $x) (float:tan $x))
(= (pyrt:libm1 "tanh" $x) (float:tanh $x))
(= (pyrt:libm2 "atan2" $y $x) (float:atan2 $y $x))
(= (pyrt:libm2 "copysign" $x $y) (float:copysign $x $y))
(= (pyrt:libm2 "fmod" $x $y) (float:fmod $x $y))
(= (pyrt:libm2 "remainder" $x $y) (float:remainder $x $y))
(= (pyrt:libm2 "nextafter" $x $y) (float:nextafter $x $y))
(= (pyrt:libm2 "pow" $x $y) (case (float:pow $x $y) (((FloatPowed $v $how) $v))))
; a path's directory: up to its last slash ("" when it has none)
(= (pyrt:directory-of $path)
   (let $parts (str:split "/" $path)
     (if (== (size-atom $parts) 1) "" (str:join "/" (pyrt:take $parts (- (size-atom $parts) 1))))))
(= (pyrt:concat-all $xs $acc)
   (if (== $xs ()) $acc (let ($x $rest) (decons-atom $xs) (pyrt:concat-all $rest (str:concat $acc $x)))))
; a new empty cell (MAKE_CELL), for a comprehension's own cell
(= (pyrt:eval (c:new-cell) (m:frame $l $g $ns $c))
   (m:val (pyrt:cell-new (m:unbound))))
; SETUP_ANNOTATIONS: a module's or class body's namespace gets an
; __annotations__ dict unless it has one
(= (pyrt:eval (c:setup-annotations) (m:frame $l $g $ns $c))
   (if (== (pyrt:dict-get $ns "__annotations__") (m:missing)) (let $u (pyrt:dict-set $ns "__annotations__" (pyrt:dict-new)) (m:val (py:None))) (m:val (py:None))))
(= (pyrt:eval (c:tuple $args) (m:frame $l $g $ns $c))
   (case (pyrt:atoms $args (m:frame $l $g $ns $c)) (((m:val $xs) (m:val (cons-atom py:tuple $xs))) ($e $e))))
(= (pyrt:eval (c:list $args) (m:frame $l $g $ns $c))
   (case (pyrt:atoms $args (m:frame $l $g $ns $c)) (((m:val $xs) (m:val (pyrt:list-new $xs))) ($e $e))))
(= (pyrt:eval (c:set $args) (m:frame $l $g $ns $c))
   (case (pyrt:atoms $args (m:frame $l $g $ns $c)) (((m:val $xs) (pyrt:build-set $xs)) ($e $e))))
(= (pyrt:eval (c:dict $pairs) (m:frame $l $g $ns $c))
   (case (pyrt:pair-atoms $pairs (m:frame $l $g $ns $c)) (((m:val $ps) (pyrt:build-dict $ps)) ($e $e))))
(= (pyrt:eval (c:getitem $oa $ka) (m:frame $l $g $ns $c))
   (pyrt:with2 (pyrt:atom $oa (m:frame $l $g $ns $c)) (pyrt:atom $ka (m:frame $l $g $ns $c)) getitem))
(= (pyrt:eval (c:slice $a $b $st) (m:frame $l $g $ns $c))
   (pyrt:make-slice (pyrt:atom $a (m:frame $l $g $ns $c)) (pyrt:atom $b (m:frame $l $g $ns $c)) (pyrt:atom $st (m:frame $l $g $ns $c))))
(= (pyrt:eval (c:getattr $oa $name) (m:frame $l $g $ns $c))
   (case (pyrt:atom $oa (m:frame $l $g $ns $c)) (((m:val $x) (pyrt:getattr $x $name)) ($e $e))))
(= (pyrt:eval (c:iter $a) (m:frame $l $g $ns $c))
   (case (pyrt:atom $a (m:frame $l $g $ns $c)) (((m:val $x) (pyrt:iter $x)) ($e $e))))
(= (pyrt:eval (c:next $a) (m:frame $l $g $ns $c))
   (case (pyrt:atom $a (m:frame $l $g $ns $c)) (((m:val $x) (pyrt:next-of $x)) ($e $e))))
(= (pyrt:eval (c:exhausted $a) (m:frame $l $g $ns $c))
   (case (pyrt:atom $a (m:frame $l $g $ns $c)) (((m:val $x) (m:val (== $x (m:exhausted)))) ($e $e))))
(= (pyrt:eval (c:unpack $a $n) (m:frame $l $g $ns $c))
   (case (pyrt:atom $a (m:frame $l $g $ns $c)) (((m:val $x) (pyrt:unpack $x $n)) ($e $e))))
(= (pyrt:eval (c:unpack-ex $a $b $af) (m:frame $l $g $ns $c))
   (case (pyrt:atom $a (m:frame $l $g $ns $c)) (((m:val $x) (pyrt:unpack-ex $x $b $af)) ($e $e))))
; a match statement's checks (ceval's MATCH_SEQUENCE, MATCH_MAPPING, GET_LEN,
; MATCH_KEYS, MATCH_CLASS, and a mapping pattern's **rest): the prelude's
; protocol functions, but GET_LEN, which is len()
(= (pyrt:eval (c:match-sequence $a) (m:frame $l $g $ns $c))
   (pyrt:prelude-on "_match_sequence" (LCons $a (LNil)) (m:frame $l $g $ns $c)))
(= (pyrt:eval (c:match-mapping $a) (m:frame $l $g $ns $c))
   (pyrt:prelude-on "_match_mapping" (LCons $a (LNil)) (m:frame $l $g $ns $c)))
(= (pyrt:eval (c:match-keys $a $k) (m:frame $l $g $ns $c))
   (pyrt:prelude-on "_match_keys" (LCons $a (LCons $k (LNil))) (m:frame $l $g $ns $c)))
(= (pyrt:eval (c:match-class $a $k $n $names) (m:frame $l $g $ns $c))
   (pyrt:prelude-on "_match_class" (LCons $a (LCons $k (LCons $n (LCons $names (LNil))))) (m:frame $l $g $ns $c)))
(= (pyrt:eval (c:match-rest $a $k) (m:frame $l $g $ns $c))
   (pyrt:prelude-on "_match_rest" (LCons $a (LCons $k (LNil))) (m:frame $l $g $ns $c)))
(= (pyrt:eval (c:get-len $a) (m:frame $l $g $ns $c))
   (case (pyrt:atom $a (m:frame $l $g $ns $c)) (((m:val $x) (pyrt:len $x)) ($e $e))))
; except*'s CHECK_EG_MATCH and PREP_RERAISE_STAR: the prelude's
(= (pyrt:eval (c:eg-match $a $t) (m:frame $l $g $ns $c))
   (pyrt:prelude-on "_eg_match" (LCons $a (LCons $t (LNil))) (m:frame $l $g $ns $c)))
(= (pyrt:eval (c:prep-reraise-star $o $xs) (m:frame $l $g $ns $c))
   (pyrt:prelude-on "_prep_reraise_star" (LCons $o (LCons $xs (LNil))) (m:frame $l $g $ns $c)))
; PyErr_SetHandledException: the exception being handled is now A (an
; except* handler handles its match)
(= (pyrt:eval (c:set-handled $a) (m:frame $l $g $ns $c))
   (case (pyrt:atom $a (m:frame $l $g $ns $c))
     (((m:val $x)
       (let $s (get-state &py-handled)
         (let $u (change-state! &py-handled (cons-atom $x (if (== (size-atom $s) 0) () (cdr-atom $s)))) (m:val (py:None)))))
      ($e $e))))
(= (pyrt:prelude-on $name $args $frame)
   (case (pyrt:atoms $args $frame) (((m:val $xs) (pyrt:prelude-call $name $xs)) ($e $e))))
(= (pyrt:eval (c:list-append $ta $xa) (m:frame $l $g $ns $c))
   (pyrt:with2 (pyrt:atom $ta (m:frame $l $g $ns $c)) (pyrt:atom $xa (m:frame $l $g $ns $c)) list-append))
(= (pyrt:eval (c:list-extend $ta $xa) (m:frame $l $g $ns $c))
   (pyrt:with2 (pyrt:atom $ta (m:frame $l $g $ns $c)) (pyrt:atom $xa (m:frame $l $g $ns $c)) list-extend))
(= (pyrt:eval (c:list-tuple $ta) (m:frame $l $g $ns $c))
   (case (pyrt:atom $ta (m:frame $l $g $ns $c)) (((m:val $t) (m:val (cons-atom py:tuple (pyrt:list-items $t)))) ($e $e))))
(= (pyrt:eval (c:set-add $sa $xa) (m:frame $l $g $ns $c))
   (pyrt:with2 (pyrt:atom $sa (m:frame $l $g $ns $c)) (pyrt:atom $xa (m:frame $l $g $ns $c)) set-add))
(= (pyrt:eval (c:set-update $sa $xa) (m:frame $l $g $ns $c))
   (pyrt:with2 (pyrt:atom $sa (m:frame $l $g $ns $c)) (pyrt:atom $xa (m:frame $l $g $ns $c)) set-update))
(= (pyrt:eval (c:dict-update $da $xa) (m:frame $l $g $ns $c))
   (pyrt:with2 (pyrt:atom $da (m:frame $l $g $ns $c)) (pyrt:atom $xa (m:frame $l $g $ns $c)) dict-update))
(= (pyrt:eval (c:dict-merge $da $xa $fa) (m:frame $l $g $ns $c))
   (pyrt:dict-merge-on (pyrt:atom $da (m:frame $l $g $ns $c)) (pyrt:atom $xa (m:frame $l $g $ns $c)) (pyrt:atom $fa (m:frame $l $g $ns $c))))
(= (pyrt:eval (c:exc-of $a) (m:frame $l $g $ns $c))
   (case (pyrt:atom $a (m:frame $l $g $ns $c)) (((m:val $x) (pyrt:exc-of $x)) ($e $e))))
(= (pyrt:eval (c:cause-of $a) (m:frame $l $g $ns $c))
   (case (pyrt:atom $a (m:frame $l $g $ns $c)) (((m:val $x) (pyrt:cause-of $x)) ($e $e))))
(= (pyrt:eval (c:check-exc $ta $aa) (m:frame $l $g $ns $c))
   (pyrt:with2 (pyrt:atom $ta (m:frame $l $g $ns $c)) (pyrt:atom $aa (m:frame $l $g $ns $c)) check-exc))
(= (pyrt:eval (c:with-cause $ta $aa $tca $caa) (m:frame $l $g $ns $c))
   (case (pyrt:atoms (LCons $ta (LCons $aa (LCons $tca (LCons $caa (LNil))))) (m:frame $l $g $ns $c)) (((m:val $vs) (let ($t $a $tc $ca) $vs (pyrt:with-cause $t $a $tc $ca))) ($e $e))))
(= (pyrt:eval (c:exc-match $ta $ca) (m:frame $l $g $ns $c))
   (pyrt:with2 (pyrt:atom $ta (m:frame $l $g $ns $c)) (pyrt:atom $ca (m:frame $l $g $ns $c)) exc-match))
(= (pyrt:eval (c:assertion-error $args) (m:frame $l $g $ns $c))
   (case (pyrt:atoms $args (m:frame $l $g $ns $c)) (((m:val $xs) (m:val (pyrt:exc-new (py:type AssertionError) $xs))) ($e $e))))
(= (pyrt:eval (c:del-item $oa $ka) (m:frame $l $g $ns $c))
   (pyrt:with2 (pyrt:atom $oa (m:frame $l $g $ns $c)) (pyrt:atom $ka (m:frame $l $g $ns $c)) del-item))
(= (pyrt:eval (c:del-attr $oa $name) (m:frame $l $g $ns $c))
   (case (pyrt:atom $oa (m:frame $l $g $ns $c)) (((m:val $o) (pyrt:delattr $o $name)) ($e $e))))
(= (pyrt:eval (c:build-class) (m:frame $l $g $ns $c))
   (let $b (pyrt:prelude-export "__build_class__") (if (== $b (m:missing)) (m:err (pyrt:exc "NameError" "__build_class__ not found")) (m:val $b))))
(= (pyrt:eval (c:call-ex $fa $aa $ka) (m:frame $l $g $ns $c))
   (pyrt:call-ex-on (pyrt:atom $fa (m:frame $l $g $ns $c)) (pyrt:atom $aa (m:frame $l $g $ns $c)) (pyrt:ex-kwargs $ka (m:frame $l $g $ns $c))))
; STORE_SUBSCR and STORE_ATTR: the value is read before the object and the key
(= (pyrt:eval (c:set-item $oa $ka $va) (m:frame $l $g $ns $c))
   (case (pyrt:atoms (LCons $va (LCons $oa (LCons $ka (LNil)))) (m:frame $l $g $ns $c)) (((m:val ($v $o $k)) (pyrt:setitem $o $k $v)) ($e $e))))
(= (pyrt:eval (c:set-attr $oa $name $va) (m:frame $l $g $ns $c))
   (case (pyrt:atom $va (m:frame $l $g $ns $c)) (((m:val $v) (pyrt:set-attr-on (pyrt:atom $oa (m:frame $l $g $ns $c)) $name (m:val $v))) ($e $e))))
; the builtins that read their caller's frame: globals() its globals,
; locals() its namespace, or in a function a new dict of its bound locals
; and cells in slot order (PEP 667's snapshot), vars() as locals() and
; vars(x) x's __dict__, dir() the sorted names of locals() and dir(x)
; type(x).__dir__(x) sorted (the prelude's _dir)
(= (pyrt:reads-frame $b) (case $b ((globals true) (locals true) (vars true) (dir true) (exec true) (eval true) ($other false))))
; exec() and eval() (builtin_exec_impl, builtin_eval_impl): the prelude's,
; given the caller's globals and locals first
(= (pyrt:frame-builtin exec $xs $ks (m:frame $l $g $ns $c))
   (case (pyrt:frame-locals (m:frame $l $g $ns $c))
     (((m:val $loc) (pyrt:call-kw (pyrt:prelude "_exec") (cons-atom $g (cons-atom $loc $xs)) $ks))
      ($e $e))))
(= (pyrt:frame-builtin eval $xs $ks (m:frame $l $g $ns $c))
   (case (pyrt:frame-locals (m:frame $l $g $ns $c))
     (((m:val $loc) (pyrt:call-kw (pyrt:prelude "_eval") (cons-atom $g (cons-atom $loc $xs)) $ks))
      ($e $e))))
(= (pyrt:frame-builtin dir $xs $ks $f)
   (if (== $ks ())
       (case (size-atom $xs)
         ((0 (case (pyrt:frame-locals $f) (((m:val $d) (pyrt:prelude-call "_dir_locals" (cons-atom $d ()))) ($e $e))))
          (1 (pyrt:prelude-call "_dir" $xs))
          ($n (m:err (pyrt:exc "TypeError" (str:concat "dir expected at most 1 argument, got " (repr $n)))))))
       (m:err (pyrt:exc "TypeError" "dir() takes no keyword arguments"))))
(= (pyrt:frame-builtin globals $xs $ks (m:frame $l $g $ns $c))
   (if (pyrt:no-arguments $xs $ks) (m:val $g) (pyrt:takes-no-arguments "globals" $xs $ks)))
(= (pyrt:frame-builtin locals $xs $ks $f)
   (if (pyrt:no-arguments $xs $ks) (pyrt:frame-locals $f) (pyrt:takes-no-arguments "locals" $xs $ks)))
(= (pyrt:frame-builtin vars $xs $ks $f)
   (if (== $ks ())
       (case (size-atom $xs)
         ((0 (pyrt:frame-locals $f))
          (1 (pyrt:vars-of (car-atom $xs)))
          ($n (m:err (pyrt:exc "TypeError" (str:concat "vars expected at most 1 argument, got " (repr $n)))))))
       (m:err (pyrt:exc "TypeError" "vars() takes no keyword arguments"))))
(= (pyrt:no-arguments $xs $ks) (if (== $xs ()) (== $ks ()) false))
(= (pyrt:takes-no-arguments $name $xs $ks)
   (if (== $ks ())
       (m:err (pyrt:exc "TypeError" (str:concat (str:concat (str:concat $name "() takes no arguments (") (repr (size-atom $xs))) " given)")))
       (m:err (pyrt:exc "TypeError" (str:concat $name "() takes no keyword arguments")))))
(= (pyrt:frame-locals (m:frame $l $g $ns (c:code $nm $q (c:args $npo $np $nk $st $ds $names) $nsl $cells $frees $fl $body $cofile)))
   (case $fl
     (((c:function $d) (pyrt:build-dict (pyrt:bound-slots $names 0 $l $cells $frees)))
      ((c:generator $d) (pyrt:build-dict (pyrt:bound-slots $names 0 $l $cells $frees)))
      ((c:coroutine $d) (pyrt:build-dict (pyrt:bound-slots $names 0 $l $cells $frees)))
      ((c:async-generator $d) (pyrt:build-dict (pyrt:bound-slots $names 0 $l $cells $frees)))
      ($other (m:val $ns)))))
(= (pyrt:bound-slots $names $i $l $cells $frees)
   (case $names
     (((LNil) ())
      ((LCons $n $rest)
       (let $v (if (pyrt:in-lcons $i $cells) (pyrt:cell-content (index-atom $l $i))
                 (if (pyrt:in-lcons $i $frees) (pyrt:cell-content (index-atom $l $i)) (index-atom $l $i)))
         (if (== $v (m:unbound)) (pyrt:bound-slots $rest (+ $i 1) $l $cells $frees)
             (cons-atom (py:pair $n $v) (pyrt:bound-slots $rest (+ $i 1) $l $cells $frees))))))))
(= (pyrt:cell-content (py:cell $id)) (pyrt:heap-one (h:cell $id $x) $x))
(= (pyrt:vars-of $x)
   (case (pyrt:getattr-now $x "__dict__")
     (((m:val $d) (m:val $d))
      ((m:err $e) (if (pyrt:err-is $e AttributeError) (m:err (pyrt:exc "TypeError" "vars() argument must have __dict__ attribute")) (m:err $e))))))
; whether a raised error is of exception class T or a subclass of it
(= (pyrt:err-is $x $t)
   (case $x
     (((py:exc $n $m) (pyrt:in-mro (py:type $t) (pyrt:mro-of (py:type (str:string->symbol $n)))))
      ((py:exca $n $as) (pyrt:in-mro (py:type $t) (pyrt:mro-of (py:type (str:string->symbol $n)))))
      ($other (pyrt:exc-is $other $t)))))
(= (pyrt:closure-cells $clo $l)
   (case $clo (((LNil) ()) ((LCons (c:cellref $i) $rest) (cons-atom (index-atom $l $i) (pyrt:closure-cells $rest $l))))))
(= (pyrt:with2 $rx $ry $what)
   (case $rx (((m:val $x) (case $ry (((m:val $y) (pyrt:apply2 $what $x $y)) ($e $e)))) ($e $e))))
(= (pyrt:apply2 getitem $x $y) (pyrt:getitem $x $y))
; the instructions that build displays and a call's arguments, as CPython's:
; LIST_APPEND, LIST_EXTEND, SET_ADD, SET_UPDATE, DICT_UPDATE
; whether iterating may be tried (LIST_EXTEND's test before its own message):
; a builtin iterable, or an object whose type has __iter__ or __getitem__
(= (pyrt:may-iterate $x)
   (if (pyrt:needs-python $x)
       (let $t (pyrt:type-of $x)
         (if (!= (pyrt:mro-lookup $t "__iter__") (m:missing)) true (!= (pyrt:mro-lookup $t "__getitem__") (m:missing))))
       (pyrt:is-iterable $x)))
(= (pyrt:apply2 list-append $t $x) (let $a (pyrt:list-append $t $x) (m:val (py:None))))
(= (pyrt:apply2 list-extend $t $x)
   (if (pyrt:may-iterate $x)
       (case (pyrt:elements $x) (((m:val $xs) (let $a (pyrt:list-append-all $t $xs) (m:val (py:None)))) ($e $e)))
       (m:err (pyrt:exc "TypeError" (str:concat "Value after * must be an iterable, not " (pyrt:type-name $x))))))
(= (pyrt:apply2 set-add $t $x) (case (pyrt:set-key $x) (((m:val $k) (let $a (pyrt:set-add $t $k $x) (m:val (py:None)))) ($e $e))))
(= (pyrt:apply2 set-update $t $x)
   (case (pyrt:set-update $t $x) (((m:val $s) (m:val (py:None))) ($e $e))))
(= (pyrt:apply2 dict-update $t $x)
   (case $x
     (((py:dict $id) (case (pyrt:fill-dict $t (pyrt:dict-entries $x)) (((m:val $t2) (m:val (py:None))) ($e $e))))
      ((py:obj $id) (pyrt:prelude-sync "_dict_update_mapping" (cons-atom $t (cons-atom $x ()))))
      ($other (m:err (pyrt:exc "TypeError" (str:concat (pyrt:quoted (pyrt:type-name $x)) " object is not a mapping")))))))
(= (pyrt:list-append-all $t $xs)
   (if (== (size-atom $xs) 0) ()
     (let ($x $rest) (decons-atom $xs) (let $a (pyrt:list-append $t $x) (pyrt:list-append-all $t $rest)))))
; DICT_MERGE: a call's keywords from a mapping; a repeated one is an error
(= (pyrt:dict-merge-on $rd $rx $rf)
   (case $rf
     (((m:val $fn)
       (case $rd
         (((m:val $d)
           (case $rx
             (((m:val $x)
               (case $x
                 (((py:dict $id) (pyrt:merge-entries $d (pyrt:dict-entries $x) $fn))
                  ($other (m:err (pyrt:exc "TypeError" (str:concat "Value after ** must be a mapping, not " (pyrt:type-fqn (pyrt:type-of $x)))))))))
              ($e $e))))
          ($e $e))))
      ($e $e))))
(= (pyrt:merge-entries $d $ps $fn)
   (if (== (size-atom $ps) 0) (m:val (py:None))
     (let ((py:pair $k $v) $rest) (decons-atom $ps)
       (case (pyrt:dict-key $k)
         (((m:val $ck)
           (if (== (pyrt:dict-entry $d $ck) (m:missing))
               (let $a (pyrt:dict-put $d $ck $k $v) (pyrt:merge-entries $d $rest $fn))
               (m:err (pyrt:exc "TypeError" (str:concat (str:concat (str:concat (pyrt:function-str $fn) " got multiple values for keyword argument '") (pyrt:str $k)) "'")))))
          ($e $e))))))
; CALL_FUNCTION_EX: the positional arguments a tuple (any other iterable made
; one), the keywords a dict whose keys must all be strings
(= (pyrt:call-ex-on $rf $ra $rk)
   (case $rf
     (((m:val $fn)
       (case $ra
         (((m:val $a)
           (case (pyrt:ex-args $fn $a)
             (((m:val $xs) (case $rk (((m:val $ks) (pyrt:call-kw $fn $xs $ks)) ($e $e))))
              ($e $e))))
          ($e $e))))
      ($e $e))))
(= (pyrt:ex-args $fn $a)
   (if (pyrt:is-tuple $a) (m:val (cdr-atom $a))
     (if (pyrt:may-iterate $a) (pyrt:elements $a)
       (m:err (pyrt:exc "TypeError" (str:concat "Value after * must be an iterable, not " (pyrt:type-name $a)))))))
(= (pyrt:ex-kwargs $ka $f)
   (case $ka
     (((c:no-kw) (m:val ()))
      ($other (case (pyrt:atom $ka $f)
                (((m:val $d) (let $ps (pyrt:dict-entries $d)
                               (if (pyrt:all-str-keys $ps) (m:val $ps) (m:err (pyrt:exc "TypeError" "keywords must be strings")))))
                 ($e $e)))))))
(= (pyrt:all-str-keys $ps)
   (if (== (size-atom $ps) 0) true
     (let ((py:pair $k $v) $rest) (decons-atom $ps) (if (pyrt:is-str $k) (pyrt:all-str-keys $rest) false))))
; a callable's name in CPython's messages (_PyObject_FunctionStr): its
; qualified name and "()", after its module unless that is builtins
(= (pyrt:function-str $fn)
   (case $fn
     (((py:func $fid)
       (let $m (pyrt:field $fn module) (let $q (pyrt:field $fn qualname)
         (if (pyrt:is-str $m)
             (if (== $m "builtins") (str:concat $q "()") (str:concat (str:concat (str:concat $m ".") $q) "()"))
             (str:concat $q "()")))))
      ((py:builtin $b) (str:concat (str:symbol->string $b) "()"))
      ((py:method $o $n) (str:concat (str:concat (str:concat (pyrt:qualname-of (pyrt:type-of $o)) ".") $n) "()"))
      ($other (pyrt:str $other)))))
; x[a:b:c] (BUILD_SLICE): the slice of the three values as they are (what
; they mean is the subscripted object's to judge)
(= (pyrt:make-slice $ra $rb $rs)
   (case (r3 $ra $rb $rs)
     (((r3 (m:val $a) (m:val $b) (m:val $s)) (m:val (py:slice $a $b $s)))
      ($other (pyrt:first-error (r3 $ra $rb $rs))))))
(= (pyrt:first-error $rs) (let ($r $rest) (decons-atom (cdr-atom $rs)) (case $r (((m:val $x) (pyrt:first-error (cons-atom r3 $rest))) ($e $e)))))
(= (pyrt:pair-atoms $pairs $f)
   (case $pairs
     (((LNil) (m:val ()))
      ((LCons (c:pair $ka $va) $rest)
       (case (pyrt:atom $ka $f)
         (((m:val $k) (case (pyrt:atom $va $f)
                        (((m:val $v) (case (pyrt:pair-atoms $rest $f) (((m:val $ps) (m:val (cons-atom (py:pair $k $v) $ps))) ($e $e))))
                         ($e $e))))
          ($e $e)))))))
; keyword arguments: (NAME VALUE) pairs
(= (pyrt:kw-atoms $kws $f)
   (case $kws
     (((LNil) (m:val ()))
      ((LCons (c:kw $name $va) $rest)
       (case (pyrt:atom $va $f)
         (((m:val $v) (case (pyrt:kw-atoms $rest $f) (((m:val $ks) (m:val (cons-atom (py:pair $name $v) $ks))) ($e $e))))
          ($e $e)))))))

(= (pyrt:atoms $args $f)
   (case $args
     (((LNil) (m:val ()))
      ((LCons $a $rest)
       (case (pyrt:atom $a $f)
         (((m:val $x) (case (pyrt:atoms $rest $f) (((m:val $xs) (m:val (cons-atom $x $xs))) ($e $e))))
          ($e $e)))))))

(= (pyrt:bin $o $rx $ry)
   (case $rx
     (((m:val $x) (case $ry (((m:val $y) (pyrt:binop $o $x $y)) ($e $e))))
      ($e $e))))
(= (pyrt:cmp $o $rx $ry)
   (case $rx
     (((m:val $x) (case $ry (((m:val $y) (pyrt:compare $o $x $y)) ($e $e))))
      ($e $e))))
(= (pyrt:un $o $rx) (case $rx (((m:val $x) (pyrt:unary $o $x)) ($e $e))))

(= (pyrt:op-text (py:Add)) "+")
(= (pyrt:op-text (py:Sub)) "-")
(= (pyrt:op-text (py:Mult)) "*")
(= (pyrt:op-text (py:FloorDiv)) "//")
(= (pyrt:op-text (py:Mod)) "%")
(= (pyrt:op-text (py:Pow)) "** or pow()")
(= (pyrt:op-text (py:Div)) "/")
(= (pyrt:op-text (py:MatMult)) "@")
(= (pyrt:op-text (py:LShift)) "<<")
(= (pyrt:op-text (py:RShift)) ">>")
(= (pyrt:op-text (py:BitAnd)) "&")
(= (pyrt:op-text (py:BitOr)) "|")
(= (pyrt:op-text (py:BitXor)) "^")
; a builtin operation declines operands outside its domain (NotImplemented,
; as a number slot answers)
(= (pyrt:declined) (m:val (py:NotImplemented)))
(= (pyrt:unsupported-op $sym $x $y)
   (m:err (pyrt:exc "TypeError"
            (str:concat (str:concat (str:concat (str:concat "unsupported operand type(s) for " $sym) ": ")
                                    (str:concat (pyrt:quoted (pyrt:type-name $x)) " and "))
                        (pyrt:quoted (pyrt:type-name $y))))))
(= (pyrt:zero-division $why) (m:err (pyrt:exc "ZeroDivisionError" $why)))

; x OP y: a program's object through the prelude's binary_op1 (its dunder
; methods, reflected, a subclass first); builtin values here
(= (pyrt:binop $o $x $y)
   (if (pyrt:either-python-or-set $x $y)
       (let (dn $name $rname $iname) (pyrt:op-dunders $o)
         (pyrt:prelude-call "_binary_op" (cons-atom $x (cons-atom $y (cons-atom $name (cons-atom $rname (cons-atom (pyrt:op-text $o) ())))))))
       (pyrt:binop-plain $o $x $y (pyrt:op-text $o))))
; builtin values (PyNumber_Add and its kin): the operation; when it declines,
; a sequence's concatenation (the left's, for +) or repetition (the left's,
; else the right's, for *), else the TypeError naming SYM
(= (pyrt:binop-plain $o $x $y $sym)
   (case (pyrt:binop-builtin $o $x $y)
     (((m:val (py:NotImplemented)) (pyrt:sequence-fallback $o $x $y $sym false))
      ($r $r))))
; the same for x OP= y (PyNumber_InPlaceMultiply asks the right operand to
; repeat only when the left has no sequence methods at all)
(= (pyrt:inplace-plain $o $x $y)
   (case (pyrt:binop-builtin $o $x $y)
     (((m:val (py:NotImplemented)) (pyrt:sequence-fallback $o $x $y (pyrt:inplace-text $o) true))
      ($r $r))))
(= (pyrt:sequence-fallback $o $x $y $sym $inplace)
   (case $o
     (((py:Add) (if (pyrt:is-builtin-seq $x) (m:err (pyrt:exc "TypeError" (pyrt:concat-refused (pyrt:seq-kind $x) $y))) (pyrt:unsupported-op $sym $x $y)))
      ((py:Mult) (if (pyrt:is-builtin-seq $x) (pyrt:sequence-repeat $x $y)
                   (if (if (pyrt:is-builtin-seq $y) (if $inplace (not (pyrt:has-sequence-methods $x)) true) false)
                       (pyrt:sequence-repeat $y $x)
                       (pyrt:unsupported-op $sym $x $y))))
      ($other (pyrt:unsupported-op $sym $x $y)))))
; a builtin value whose type has sequence methods (tp_as_sequence)
(= (pyrt:has-sequence-methods $x)
   (if (pyrt:is-builtin-seq $x) true
     (case $x (((py:dict $i) true) ((py:set $i) true) ((py:frozenset $i) true) ((py:range $a $b $c) true) ((py:view $k $d) true) ($o false)))))
(= (pyrt:is-builtin-seq $x) (!= (pyrt:seq-kind $x) none))
(= (pyrt:seq-kind $x)
   (if (pyrt:is-str $x) str
     (if (pyrt:is-tuple $x) tuple
       (case $x (((py:list $i) list) ((py:Bytes $v) bytes) ($o none))))))
; sequence_repeat: N's index the count, else the TypeError
(= (pyrt:sequence-repeat $s $n)
   (case (pyrt:index-int $n)
     (((m:val $k) (pyrt:binop-builtin (py:Mult) $s $k))
      ((m:missing) (m:err (pyrt:exc "TypeError" (str:concat (str:concat "can't multiply sequence by non-int of type " (pyrt:quoted (pyrt:type-name $n))) ""))))
      ($e $e))))
; a builtin sequence's slot wrapper for + or * (sq_concat, sq_repeat): not a
; number slot, so the operators reach it last (PyNumber_Add); a template's +
; is its sq_concat too
(= (pyrt:seq-slot $f) (case $f (((py:bmeth $st $sn) (pyrt:in-flat $st (list tuple str bytes bytearray Template))) ($o false))))
; an in-place operator's text: += for +, **= for ** (or pow())
(= (pyrt:inplace-text $o) (case $o (((py:Pow) "**=") ($other (str:concat (pyrt:op-text $o) "=")))))
(= (pyrt:either-python $x $y) (if (pyrt:needs-python $x) true (pyrt:needs-python $y)))
; a set's or frozenset's operators are its type's methods, which the prelude
; gives it (setobject.c's number and comparison slots)
(= (pyrt:either-python-or-set $x $y)
   (if (pyrt:either-python $x $y) true (if (pyrt:is-anyset $x) true (pyrt:is-anyset $y))))
(= (pyrt:op-dunders $o)
   (case $o
     (((py:Add) (dn "__add__" "__radd__" "__iadd__")) ((py:Sub) (dn "__sub__" "__rsub__" "__isub__"))
      ((py:Mult) (dn "__mul__" "__rmul__" "__imul__")) ((py:Div) (dn "__truediv__" "__rtruediv__" "__itruediv__"))
      ((py:FloorDiv) (dn "__floordiv__" "__rfloordiv__" "__ifloordiv__")) ((py:Mod) (dn "__mod__" "__rmod__" "__imod__"))
      ((py:Pow) (dn "__pow__" "__rpow__" "__ipow__")) ((py:MatMult) (dn "__matmul__" "__rmatmul__" "__imatmul__"))
      ((py:LShift) (dn "__lshift__" "__rlshift__" "__ilshift__")) ((py:RShift) (dn "__rshift__" "__rrshift__" "__irshift__"))
      ((py:BitAnd) (dn "__and__" "__rand__" "__iand__")) ((py:BitOr) (dn "__or__" "__ror__" "__ior__"))
      ((py:BitXor) (dn "__xor__" "__rxor__" "__ixor__")))))
; x OP= y: the in-place method first, then the binary operation; a list's +=
; and *= change the list
(= (pyrt:apply2 (inplace $o) $x $y)
   (if (pyrt:either-python-or-set $x $y)
       (let (dn $name $rname $iname) (pyrt:op-dunders $o)
         (pyrt:prelude-call "_inplace_op" (cons-atom $x (cons-atom $y (cons-atom $iname (cons-atom $name (cons-atom $rname (cons-atom (pyrt:op-text $o) ()))))))))
       (case (iop $o $x)
         (((iop (py:Add) (py:list $id)) (case (pyrt:elements $y) (((m:val $es) (let $u (pyrt:list-append-all $x $es) (m:val $x))) ($e $e))))
          ((iop (py:Mult) (py:list $id)) (if (pyrt:intish $y) (let $r (pyrt:list-reset $x (pyrt:repeat-items (pyrt:list-items $x) (pyrt:int-of $y))) (m:val $x)) (pyrt:inplace-plain $o $x $y)))
          ((iop (py:BitOr) (py:dict $id)) (case (pyrt:dict-fill-from $x $y) (((m:val $u) (m:val $x)) ($e $e))))
          ($other (pyrt:inplace-plain $o $x $y))))))
; an operation's result, a prelude call run to its value from the host
(= (pyrt:now $r)
   (case $r
     (((m:call (c:code $nm $q $a $ns $cs $fs $fl $body $cofile) $frame) (pyrt:run-to-value $body $frame))
      ($other $other))))
(= (pyrt:binop-builtin $o $x $y)
   (if (if (pyrt:is-float $x) (pyrt:is-number $y) (if (pyrt:is-float $y) (pyrt:intish $x) false))
       (if (pyrt:float-op-defined $o) (pyrt:float-binop $o $x $y) (pyrt:declined))
       (pyrt:binop-nonfloat $o $x $y)))
(= (pyrt:binop-nonfloat $o $x $y)
   (if (pyrt:intish $x)
       (if (pyrt:intish $y)
           (if (if (pyrt:is-bool $x) (if (pyrt:is-bool $y) (pyrt:is-bool-bitop $o) false) false)
               (pyrt:bool-bitop $o $x $y)
               (if (pyrt:int-op-defined $o) (pyrt:int-binop $o (pyrt:int-of $x) (pyrt:int-of $y)) (pyrt:declined)))
           (pyrt:mixed-binop $o $x $y))
       (if (pyrt:is-str $x) (pyrt:str-binop $o $x $y) (pyrt:seq-binop $o $x $y))))
(= (pyrt:seq-binop $o $x $y)
   (case (b3 $o $x $y)
     (((b3 (py:Add) (py:list $i) (py:list $j)) (m:val (pyrt:list-new (pyrt:append (pyrt:list-items $x) (pyrt:list-items $y)))))
      ((b3 (py:Mult) (py:list $i) $n) (if (pyrt:intish $n) (m:val (pyrt:list-new (pyrt:repeat-items (pyrt:list-items $x) (pyrt:int-of $n)))) (pyrt:declined)))
      ((b3 (py:BitOr) (py:dict $i) (py:dict $j)) (pyrt:dict-union $x $y))
      ((b3 (py:Add) (py:Bytes $p) (py:Bytes $q)) (m:val (py:Bytes (str:concat $p $q))))
      ((b3 (py:Add) (py:Bytes $p) $q)
       (let $w (pyrt:bytes-view-of $q)
         (if (== $w (m:missing)) (m:err (pyrt:exc "TypeError" (str:concat (str:concat "can't concat " (pyrt:type-name $q)) " to bytes")))
           (m:val (py:Bytes (str:concat $p $w))))))
      ((b3 (py:Mult) (py:Bytes $p) $n) (if (pyrt:intish $n) (m:val (py:Bytes (pyrt:repeat $p (pyrt:int-of $n)))) (pyrt:declined)))
      ; bytes % args: printf-style formatting (_PyBytes_FormatEx, the prelude's)
      ((b3 (py:Mod) (py:Bytes $p) $q) (pyrt:prelude-call "_bytes_mod" (cons-atom $x (cons-atom $y ()))))
      ($other
       (if (pyrt:is-tuple $x)
           (if (pyrt:is-tuple $y)
               (case $o (((py:Add) (m:val (cons-atom py:tuple (pyrt:append (cdr-atom $x) (cdr-atom $y))))) ($p (pyrt:declined))))
               (case $o (((py:Mult) (if (pyrt:intish $y) (m:val (cons-atom py:tuple (pyrt:repeat-items (cdr-atom $x) (pyrt:int-of $y)))) (pyrt:declined))) ($p (pyrt:declined)))))
           (pyrt:declined))))))
(= (pyrt:repeat-items $xs $n) (if (> $n 0) (pyrt:append $xs (pyrt:repeat-items $xs (- $n 1))) ()))
; dict | dict (dict_or, PEP 584): a new dict of the left's entries, then the
; right's
(= (pyrt:dict-union $x $y)
   (let $d (pyrt:dict-new)
     (case (pyrt:fill-dict $d (pyrt:dict-entries $x))
       (((m:val $d1) (pyrt:fill-dict $d (pyrt:dict-entries $y)))
        ($e $e)))))
(= (pyrt:int-binop $o $x $y)
   (case $o
     (((py:Add) (m:val (+ $x $y)))
      ((py:Sub) (m:val (- $x $y)))
      ((py:Mult) (m:val (* $x $y)))
      ((py:Div) (if (== $y 0) (pyrt:zero-division "division by zero")
                  (case (float:from-ratio $x $y)
                    (((FloatOverflow) (m:err (pyrt:exc "OverflowError" "integer division result too large for a float"))) ($f (m:val $f))))))
      ((py:FloorDiv) (if (== $y 0) (pyrt:zero-division "division by zero") (m:val (/ (- $x (% $x $y)) $y))))
      ((py:Mod) (if (== $y 0) (pyrt:zero-division "division by zero") (m:val (% $x $y))))
      ((py:Pow) (if (>= $y 0) (m:val (pyrt:int-pow $x $y 1)) (pyrt:float-binop $o $x $y)))
      ; bits (lib/integer: two's complement without end, as Python's int)
      ((py:LShift) (if (< $y 0) (m:err (pyrt:exc "ValueError" "negative shift count")) (m:val (integer:shift-left $x $y))))
      ((py:RShift) (if (< $y 0) (m:err (pyrt:exc "ValueError" "negative shift count")) (m:val (integer:shift-right $x $y))))
      ((py:BitAnd) (m:val (integer:and $x $y)))
      ((py:BitOr) (m:val (integer:or $x $y)))
      ((py:BitXor) (m:val (integer:xor $x $y))))))
; the operators a number takes: all but @, and the bitwise ones only for ints
(= (pyrt:float-op-defined $o)
   (case $o (((py:Add) true) ((py:Sub) true) ((py:Mult) true) ((py:Div) true) ((py:FloorDiv) true) ((py:Mod) true) ((py:Pow) true) ($other false))))
(= (pyrt:int-op-defined $o) (case $o (((py:MatMult) false) ($other true))))
; bool & | ^ bool is a bool
(= (pyrt:bool-bitop $o $x $y)
   (case $o
     (((py:BitAnd) (m:val (if $x $y False)))
      ((py:BitOr) (m:val (if $x True $y)))
      ((py:BitXor) (m:val (if $x (not $y) $y))))))
(= (pyrt:is-bool-bitop $o) (case $o (((py:BitAnd) true) ((py:BitOr) true) ((py:BitXor) true) ($other false))))
; x ** n for n >= 0, exactly, by squaring
(= (pyrt:int-pow $x $n $acc)
   (if (== $n 0) $acc
     (if (== (% $n 2) 1) (pyrt:int-pow (* $x $x) (/ (- $n 1) 2) (* $acc $x))
       (pyrt:int-pow (* $x $x) (/ $n 2) $acc))))
(= (pyrt:mixed-binop $o $x $y)
   (if (pyrt:is-str $y)
       (case $o (((py:Mult) (m:val (pyrt:repeat $y (pyrt:int-of $x)))) ($other (pyrt:declined))))
       (case (bm $o $y)
         (((bm (py:Mult) (py:Bytes $b)) (m:val (py:Bytes (pyrt:repeat $b (pyrt:int-of $x)))))
          ($other (pyrt:declined))))))
(= (pyrt:str-binop $o $x $y)
   (case $o
     (((py:Add) (if (pyrt:is-str $y) (m:val (str:concat $x $y))
                  (m:err (pyrt:exc "TypeError"
                           (str:concat (str:concat "can only concatenate str (not \"" (pyrt:type-name $y)) "\") to str")))))
      ((py:Mult) (if (pyrt:intish $y) (m:val (pyrt:repeat $x (pyrt:int-of $y))) (pyrt:declined)))
      ; str % args: printf-style formatting (PyUnicode_Format, in the prelude)
      ((py:Mod) (pyrt:prelude-call "_percent_format" (cons-atom $x (cons-atom $y ()))))
      ($other (pyrt:declined)))))
(= (pyrt:repeat $s $n) (if (> $n 0) (str:concat $s (pyrt:repeat $s (- $n 1))) ""))

; comparisons: == and != between any values, the orders between numbers or
; between strings
; x == y as CPython's PyObject_RichCompareBool(x, y, Py_EQ): a program's
; object is equal to itself without asking, else its rich comparison's truth;
; containers compare item by item (the left item first); (m:val B) or
; (m:err E), an exception in an __eq__ propagating
(= (pyrt:eq $x $y)
   (if (pyrt:either-python $x $y)
       (if (== $x $y) (m:val true) (pyrt:prelude-sync "_eq" (cons-atom $x (cons-atom $y ()))))
       (pyrt:eq-builtin $x $y)))
(= (pyrt:eq-builtin $x $y)
   (if (pyrt:is-float $x) (m:val (pyrt:float-eq $x $y))
     (if (pyrt:is-float $y) (m:val (pyrt:float-eq $y $x))
       (if (pyrt:intish $x) (m:val (if (pyrt:intish $y) (== (pyrt:int-of $x) (pyrt:int-of $y)) false))
         (if (pyrt:is-str $x) (m:val (if (pyrt:is-str $y) (== $x $y) false))
           (if (pyrt:is-tuple $x) (if (pyrt:is-tuple $y) (pyrt:all-eq (cdr-atom $x) (cdr-atom $y)) (m:val false))
             (case (eq2 $x $y)
               (((eq2 (py:list $i) (py:list $j)) (pyrt:all-eq (pyrt:list-items $x) (pyrt:list-items $y)))
                ((eq2 (py:dict $i) (py:dict $j)) (pyrt:dict-eq $x $y))
                ((eq2 (py:set $i) (py:set $j)) (m:val (pyrt:set-eq $x $y)))
                ((eq2 (py:set $i) (py:frozenset $j)) (m:val (pyrt:set-eq $x $y)))
                ((eq2 (py:frozenset $i) (py:set $j)) (m:val (pyrt:set-eq $x $y)))
                ((eq2 (py:frozenset $i) (py:frozenset $j)) (m:val (pyrt:set-eq $x $y)))
                ; slice_richcompare: as the tuples of their three values
                ((eq2 (py:slice $sa $sb $sc) (py:slice $ta $tb $tc)) (pyrt:all-eq ($sa $sb $sc) ($ta $tb $tc)))
                ($other (m:val (== $x $y)))))))))))
; equality where an exception cannot be passed on (a key's lookup by its
; program's __eq__): an exception counts as unequal
(= (pyrt:py-eq $x $y) (case (pyrt:eq $x $y) (((m:val $b) $b) ($e false))))
(= (pyrt:all-eq $xs $ys)
   (if (== (size-atom $xs) (size-atom $ys)) (pyrt:each-eq $xs $ys) (m:val false)))
(= (pyrt:each-eq $xs $ys)
   (if (== $xs ()) (m:val true)
     (case (pyrt:eq (car-atom $xs) (car-atom $ys))
       (((m:val $b) (if $b (pyrt:each-eq (cdr-atom $xs) (cdr-atom $ys)) (m:val false)))
        ($e $e)))))
(= (pyrt:dict-eq $x $y)
   (if (== (pyrt:dict-len $x) (pyrt:dict-len $y)) (pyrt:entries-in (pyrt:dict-entries $x) $y) (m:val false)))
(= (pyrt:entries-in $es $y)
   (if (== $es ()) (m:val true)
     (let ((py:pair $k $v) $rest) (decons-atom $es)
       (case (pyrt:dict-key $k)
         (((m:val $ck) (let $w (pyrt:dict-get $y $ck)
                         (if (== $w (m:missing)) (m:val false)
                           (case (pyrt:eq $v $w) (((m:val $b) (if $b (pyrt:entries-in $rest $y) (m:val false))) ($e $e))))))
          ($e $e))))))
(= (pyrt:cmp-text (py:Lt)) "<")
(= (pyrt:cmp-text (py:LtE)) "<=")
(= (pyrt:cmp-text (py:Gt)) ">")
(= (pyrt:cmp-text (py:GtE)) ">=")
; x CMP y: a program's object through the prelude's rich comparison (and its
; containment); builtin values here
(= (pyrt:compare $o $x $y)
   (case $o
     (((py:Is) (m:val (== $x $y)))
      ((py:IsNot) (m:val (not (== $x $y))))
      ((py:In) (if (pyrt:needs-python $y) (pyrt:prelude-call "_contains" (cons-atom $y (cons-atom $x ()))) (pyrt:compare-builtin $o $x $y)))
      ((py:NotIn) (if (pyrt:needs-python $y)
                      (case (pyrt:prelude-sync "_contains" (cons-atom $y (cons-atom $x ()))) (((m:val $b) (m:val (not $b))) ($e $e)))
                      (pyrt:compare-builtin $o $x $y)))
      ($other (if (if (pyrt:either-python $x $y) true (pyrt:set-order $o $x $y))
                  (pyrt:prelude-call "_richcompare" (cons-atom $x (cons-atom $y (cons-atom (pyrt:compare-dunder $o) ()))))
                  (pyrt:compare-builtin $o $x $y))))))
; a set's order comparisons (subset, superset) are its type's methods; its
; equality is the host's
(= (pyrt:set-order $o $x $y)
   (if (if (pyrt:is-anyset $x) true (pyrt:is-anyset $y))
       (case $o (((py:Lt) true) ((py:LtE) true) ((py:Gt) true) ((py:GtE) true) ($p false)))
       false))
(= (pyrt:compare-dunder $o)
   (case $o (((py:Eq) "__eq__") ((py:NotEq) "__ne__") ((py:Lt) "__lt__") ((py:LtE) "__le__") ((py:Gt) "__gt__") ((py:GtE) "__ge__"))))
(= (pyrt:compare-builtin $o $x $y)
   (case $o
     (((py:Eq) (pyrt:eq $x $y))
      ((py:NotEq) (case (pyrt:eq $x $y) (((m:val $b) (m:val (not $b))) ($e $e))))
      ((py:Is) (m:val (== $x $y)))
      ((py:IsNot) (m:val (not (== $x $y))))
      ((py:In) (pyrt:contains $y $x))
      ((py:NotIn) (case (pyrt:contains $y $x) (((m:val $b) (m:val (not $b))) ($e $e))))
      ($order (case (bb $x $y)
                (((bb (py:Bytes $p) (py:Bytes $q)) (pyrt:order $o $p $q))
                 ($other (pyrt:order $o $x $y))))))))
; x in a bytes value (bytes_contains): an int from 0 to 255 one of its bytes,
; a bytes value a part of it
(= (pyrt:bytes-contains $b $x)
   (case (pyrt:index-int $x)
     (((m:val $n)
       (if (if (>= $n 0) (<= $n 255) false)
           (m:val (>= (str:find-index (str:from-codepoints (cons-atom $n ())) $b) 0))
           (m:err (pyrt:exc "ValueError" "byte must be in range(0, 256)"))))
      ((m:missing)
       (let $w (pyrt:bytes-view-of $x)
         (if (== $w (m:missing))
             (m:err (pyrt:exc "TypeError" (str:concat "a bytes-like object is required, not " (pyrt:quoted (pyrt:type-name $x)))))
             (m:val (>= (str:find-index $w $b) 0)))))
      ($e $e))))
; x in c (CPython's PySequence_Contains): a substring of a string; a key of a
; dict, a set or a dict's keys; an int in a range, by arithmetic; else an
; item that is x or equals it, by iteration
(= (pyrt:contains $c $x)
   (if (pyrt:is-str $c)
       (if (pyrt:is-str $x) (m:val (pyrt:is-int (str:find $c $x)))
           (m:err (pyrt:exc "TypeError" (str:concat "'in <string>' requires string as left operand, not " (pyrt:type-name $x)))))
       (case $c
         (((py:Bytes $b) (pyrt:bytes-contains $b $x))
          ((py:dict $id) (case (pyrt:dict-key $x) (((m:val $k) (m:val (!= (pyrt:dict-entry $c $k) (m:missing)))) ($e $e))))
          ((py:set $id) (case (pyrt:set-member-key $x) (((m:val $k) (m:val (!= (pyrt:set-entry $c $k) (m:missing)))) ($e $e))))
          ((py:frozenset $id) (case (pyrt:set-member-key $x) (((m:val $k) (m:val (!= (pyrt:set-entry $c $k) (m:missing)))) ($e $e))))
          ((py:view keys $d) (pyrt:contains $d $x))
          ((py:range $a $b $st) (if (pyrt:intish $x) (m:val (pyrt:in-range (pyrt:int-of $x) $a $b $st)) (pyrt:contains-by-iteration $c $x)))
          ($other (pyrt:contains-by-iteration $c $x))))))
; by iteration (_PySequence_IterSearch, list_contains): each item compared
; with x, the item first, until one equals it; the rest is not asked for
(= (pyrt:contains-by-iteration $c $x)
   (if (pyrt:is-iterable $c)
       (case (pyrt:now (pyrt:iter $c)) (((m:val $it) (pyrt:search-in $it $x)) ($e $e)))
       (m:err (pyrt:exc "TypeError" (str:concat (str:concat "argument of type " (pyrt:quoted (pyrt:type-name $c))) " is not a container or iterable")))))
(= (pyrt:search-in $it $x)
   (case (pyrt:next-of $it)
     (((m:val (m:exhausted)) (m:val false))
      ((m:val $e) (case (pyrt:eq $e $x) (((m:val $b) (if $b (m:val true) (pyrt:search-in $it $x))) ($r $r))))
      ($r $r))))
(= (pyrt:in-range $x $a $b $st)
   (if (> $st 0)
       (if (if (>= $x $a) (< $x $b) false) (== (% (- $x $a) $st) 0) false)
       (if (if (<= $x $a) (> $x $b) false) (== (% (- $a $x) (- 0 $st)) 0) false)))
(= (pyrt:order $o $x $y)
   (if (if (pyrt:is-float $x) (pyrt:is-number $y) (if (pyrt:is-float $y) (pyrt:intish $x) false))
       (m:val (pyrt:float-order $o $x $y))
       (pyrt:order-nonfloat $o $x $y)))
(= (pyrt:order-nonfloat $o $x $y)
   (if (pyrt:intish $x)
       (if (pyrt:intish $y) (m:val (pyrt:order-holds $o (pyrt:int-of $x) (pyrt:int-of $y))) (pyrt:not-orderable $o $x $y))
       (if (pyrt:is-str $x)
           (if (pyrt:is-str $y) (m:val (pyrt:order-holds $o (str:compare $x $y) 0)) (pyrt:not-orderable $o $x $y))
           (if (pyrt:is-tuple $x)
               (if (pyrt:is-tuple $y) (pyrt:seq-order $o (cdr-atom $x) (cdr-atom $y)) (pyrt:not-orderable $o $x $y))
               (case (o2 $x $y)
                 (((o2 (py:list $i) (py:list $j)) (pyrt:seq-order $o (pyrt:list-items $x) (pyrt:list-items $y)))
                  ((o2 (py:slice $sa $sb $sc) (py:slice $ta $tb $tc)) (pyrt:seq-order $o ($sa $sb $sc) ($ta $tb $tc)))
                  ($other (pyrt:not-orderable $o $x $y))))))))
; sequences in order (list_richcompare, tuplerichcompare): the first place
; their items differ (by ==, an item equal to itself unasked) decides, by
; those items' own comparison, whose result is the answer; else the lengths
(= (pyrt:seq-order $o $xs $ys)
   (if (if (== $xs ()) true (== $ys ()))
       (m:val (pyrt:order-holds $o (size-atom $xs) (size-atom $ys)))
       (case (pyrt:eq (car-atom $xs) (car-atom $ys))
         (((m:val $b) (if $b (pyrt:seq-order $o (cdr-atom $xs) (cdr-atom $ys))
                          (pyrt:now (pyrt:compare $o (car-atom $xs) (car-atom $ys)))))
          ($e $e)))))
(= (pyrt:order-holds $o $a $b)
   (case $o (((py:Lt) (< $a $b)) ((py:LtE) (<= $a $b)) ((py:Gt) (> $a $b)) ((py:GtE) (>= $a $b)))))
(= (pyrt:not-orderable $o $x $y)
   (m:err (pyrt:exc "TypeError"
            (str:concat (str:concat (str:concat (pyrt:quoted (pyrt:cmp-text $o)) " not supported between instances of ")
                                    (str:concat (pyrt:quoted (pyrt:type-name $x)) " and "))
                        (pyrt:quoted (pyrt:type-name $y))))))

(= (pyrt:unary $o $x)
   (if (pyrt:needs-python $x)
       (case $o
         (((py:Not) (case (pyrt:truth-of $x) (((m:val $b) (m:val (not $b))) ($e $e))))
          ((py:USub) (pyrt:prelude-call "_unary_op" (cons-atom $x (cons-atom "__neg__" (cons-atom "unary -" ())))))
          ((py:UAdd) (pyrt:prelude-call "_unary_op" (cons-atom $x (cons-atom "__pos__" (cons-atom "unary +" ())))))
          ((py:Invert) (pyrt:prelude-call "_unary_op" (cons-atom $x (cons-atom "__invert__" (cons-atom "unary ~" ())))))))
       (pyrt:unary-builtin $o $x)))
(= (pyrt:unary-builtin $o $x)
   (case $o
     (((py:Not) (m:val (not (pyrt:truthy $x))))
      ((py:USub)
       (if (pyrt:is-float $x) (m:val (float:neg $x))
         (if (pyrt:intish $x) (m:val (- 0 (pyrt:int-of $x)))
           (m:err (pyrt:exc "TypeError" (str:concat "bad operand type for unary -: " (pyrt:quoted (pyrt:type-name $x))))))))
      ((py:UAdd)
       (if (pyrt:is-float $x) (m:val $x)
         (if (pyrt:intish $x) (m:val (pyrt:int-of $x))
           (m:err (pyrt:exc "TypeError" (str:concat "bad operand type for unary +: " (pyrt:quoted (pyrt:type-name $x))))))))
      ((py:Invert)
       (if (pyrt:is-bool $x)
           (m:warn (py:type DeprecationWarning) (pyrt:bool-invert-warning) (m:val (integer:not (pyrt:int-of $x))))
       (if (pyrt:intish $x) (m:val (integer:not (pyrt:int-of $x)))
         (m:err (pyrt:exc "TypeError" (str:concat "bad operand type for unary ~: " (pyrt:quoted (pyrt:type-name $x)))))))))))
(= (pyrt:bool-invert-warning)
   (str:concat "Bitwise inversion '~' on bool is deprecated and will be removed in Python 3.16. "
     (str:concat "This returns the bitwise inversion of the underlying int object and is usually not what you expect from negating a bool. "
                 "Use the 'not' operator for boolean negation or ~int(x) if you really want the bitwise inversion of the underlying int.")))

; ---------------------------------------------------------------- calls

(= (pyrt:call-on $rf $rxs $rks)
   (case $rf
     (((m:val $fn) (case $rxs (((m:val $xs) (case $rks (((m:val $ks) (pyrt:call-kw $fn $xs $ks)) ($e $e)))) ($e $e))))
      ($e $e))))
(= (pyrt:call $fn $xs) (pyrt:call-kw $fn $xs ()))
; PyCallable_Check: the forms a call takes; a program's object whose type
; has __call__
(= (pyrt:is-callable $x)
   (case $x
     (((py:builtin $b) true) ((py:method $o $name) true) ((py:func $fid) true) ((py:type $t) true)
      ((py:bmeth $bt $bn) true) ((py:bmeth-bound $bt $bn $bs) true)
      ((py:bound $id $f $s) true)
      ((py:obj $id) (!= (pyrt:mro-lookup (pyrt:type-of $x) "__call__") (m:missing)))
      ($other false))))
(= (pyrt:call-kw $fn $xs $ks)
   (case $fn
     (((py:builtin $b) (pyrt:call-builtin-kw $b $xs $ks))
      ((py:method $o $name) (pyrt:call-bound-kw $o $name $xs $ks))
      ((py:bmeth $bt $bn) (pyrt:call-bmeth $bt $bn $xs $ks))
      ((py:bmeth-bound $bt $bn $bs) (pyrt:call-bmeth $bt $bn (cons-atom $bs $xs) $ks))
      ((py:func $fid) (pyrt:call-function (pyrt:field $fn code) (pyrt:field $fn globals) (pyrt:field $fn defaults)
                                          (pyrt:field $fn kwdefaults) (pyrt:field $fn closure) $xs $ks))
      ((py:type $t) (if (pyrt:builtin-class $t) (pyrt:call-type $t $xs $ks) (pyrt:call-class $fn $xs $ks)))
      ((py:bound $id $f $s) (pyrt:call-kw $f (cons-atom $s $xs) $ks))
      ((py:obj $id) (pyrt:call-object $fn $xs $ks))
      ($other (m:err (pyrt:exc "TypeError" (str:concat (pyrt:quoted (pyrt:type-name $other)) " object is not callable")))))))
; a call from the host, to its value (CPython's PyObject_Call from C): a
; Python function's body runs on the machine to its return or its exception
(= (pyrt:call-sync $fn $xs $ks)
   (case (pyrt:call-kw $fn $xs $ks)
     (((m:call (c:code $nm $q $a $ns $cs $fs $fl $body $cofile) $frame) (pyrt:run-to-value $body $frame))
      ($other $other))))
(= (pyrt:run-to-value $body $frame)
   (case (collapse (gslt:entry:python-machine:rewrite:10 (quote (MExec $body $frame (k:host)))))
     ((((gslt:result:python-machine:rewrite:10 $r)) $r)
      ($none (m:err (pyrt:exc "SystemError" "a call from the host did not finish"))))))
(= (pyrt:join-str $xs)
   (if (== $xs ()) ""
     (let ($x $rest) (decons-atom $xs)
       (if (== $rest ()) (pyrt:str $x) (str:concat (str:concat (pyrt:str $x) " ") (pyrt:join-str $rest))))))
; a call of a Python function: CPython's argument binding, in its order
; (initialize_locals): the **kwargs dict; the positional arguments into their
; slots, the rest into *args; each keyword argument into the slot of the
; parameter it names, else into **kwargs; then too many positional arguments
; are an error, missing positional ones too, else the defaults fill the last
; positional parameters; then the keyword-only defaults, and what is still
; missing is an error.  Then the cells: a captured local's slot holds a cell,
; a free name's slot its closure's cell
(= (pyrt:call-function $code $g $dflts $kwd $clo $xs $ks)
   (pyrt:start-call (pyrt:bind-function $code $g $dflts $kwd $clo $xs $ks)))
; a call's frame made: a generator function's call makes its generator, its
; body not yet run; any other call runs its body
(= (pyrt:start-call $r)
   (case $r
     (((m:call (c:code $nm $q $a $ns $cs $fs (c:generator $d) $body $cofile) $frame)
       (m:val (pyrt:gen-new (c:code $nm $q $a $ns $cs $fs (c:generator $d) $body $cofile) $frame)))
      ((m:call (c:code $nm $q $a $ns $cs $fs (c:coroutine $d) $body $cofile) $frame)
       (m:val (pyrt:coro-new (c:code $nm $q $a $ns $cs $fs (c:coroutine $d) $body $cofile) $frame)))
      ((m:call (c:code $nm $q $a $ns $cs $fs (c:async-generator $d) $body $cofile) $frame)
       (m:val (pyrt:agen-new (c:code $nm $q $a $ns $cs $fs (c:async-generator $d) $body $cofile) $frame)))
      ($other $other))))
(= (pyrt:bind-function (c:code $nm $q (c:args $npo $np $nk $star $dstar $names) $ns $cells $frees $fl $body $cofile) $g $dflts $kwd $clo $xs $ks)
   (if (pyrt:exact-call (+ $npo $np) $nk $star $dstar $xs $ks)
       (m:call (c:code $nm $q (c:args $npo $np $nk $star $dstar $names) $ns $cells $frees $fl $body $cofile)
               (m:frame (pyrt:fill-frees (pyrt:make-cells (pyrt:append $xs (pyrt:unbound-n (- $ns (+ $npo $np)))) $cells) $frees $clo)
                        $g $g (c:code $nm $q (c:args $npo $np $nk $star $dstar $names) $ns $cells $frees $fl $body $cofile)))
       (pyrt:bind-call (c:code $nm $q (c:args $npo $np $nk $star $dstar $names) $ns $cells $frees $fl $body $cofile) $g $dflts $kwd $clo $xs $ks)))
; CPython's specialized call of a Python function (CALL_PY_EXACT_ARGS): no
; *args, **kwargs or keyword-only parameters, no keyword arguments, exactly
; as many positional arguments as parameters: the arguments are the first
; slots, then the cells are made as in any call
(= (pyrt:exact-call $npos $nk $star $dstar $xs $ks)
   (if (== $nk 0) (if (== $star (py:False)) (if (== $dstar (py:False))
     (if (== (size-atom $ks) 0) (== (size-atom $xs) $npos) false) false) false) false))
(= (pyrt:bind-call (c:code $nm $q (c:args $npo $np $nk $star $dstar $names) $ns $cells $frees $fl $body $cofile) $g $dflts $kwd $clo $xs $ks)
   (let $npos (+ $npo $np)
     (let $total (+ $npos $nk)
       (let $n (size-atom $xs)
         (let $kwslot (+ $total (if (== $star (py:True)) 1 0))
           (let $l0 (pyrt:unbound-n $ns)
             (let $l1 (if (== $dstar (py:True)) (pyrt:tuple-set $l0 $kwslot (pyrt:dict-new)) $l0)
               (let $l2 (pyrt:fill-positional $l1 $xs 0 $npos)
                 (let $l3 (if (== $star (py:True)) (pyrt:tuple-set $l2 $total (cons-atom py:tuple (pyrt:drop $xs (if (> $n $npos) $npos $n)))) $l2)
                   (case (pyrt:bind-keywords $l3 $ks $ks $names $npo $total $dstar $kwslot $q)
                     (((m:val $l4)
                       (if (if (> $n $npos) (== $star (py:False)) false)
                           (m:err (pyrt:too-many-positional $q $npos (size-atom $dflts) $n (pyrt:count-bound $l4 $npos $total)))
                           (let $mp (pyrt:missing-params $l4 $names 0 (- $npos (size-atom $dflts)))
                             (if (> (size-atom $mp) 0)
                                 (m:err (pyrt:missing-error $q $mp "positional"))
                                 (let $l5 (pyrt:fill-defaults $l4 $dflts (- $npos (size-atom $dflts)) 0)
                                   (let $l6 (pyrt:fill-kwdefaults $l5 $kwd $names $npos $total)
                                     (let $mk (pyrt:missing-params $l6 $names $npos $total)
                                       (if (> (size-atom $mk) 0)
                                           (m:err (pyrt:missing-error $q $mk "keyword-only"))
                                           (let $l7 (pyrt:make-cells $l6 $cells)
                                             (let $l8 (pyrt:fill-frees $l7 $frees $clo)
                                               (m:call (c:code $nm $q (c:args $npo $np $nk $star $dstar $names) $ns $cells $frees $fl $body $cofile)
                                                       (m:frame $l8 $g $g (c:code $nm $q (c:args $npo $np $nk $star $dstar $names) $ns $cells $frees $fl $body $cofile)))))))))))))
                      ($e $e))))))))))))
(= (pyrt:fill-positional $l $xs $i $npos)
   (if (if (== $xs ()) true (>= $i $npos)) $l
     (let ($x $rest) (decons-atom $xs) (pyrt:fill-positional (pyrt:tuple-set $l $i $x) $rest (+ $i 1) $npos))))
; a keyword argument names one of the parameters after the positional-only
; ones; any other goes into **kwargs, else it is an error: positional-only
; parameters passed by keyword (all of them, in the parameters' order), or an
; unexpected keyword, with CPython's suggestion of a near parameter name
(= (pyrt:bind-keywords $l $ks $all $names $npo $total $dstar $kwslot $q)
   (if (== (size-atom $ks) 0) (m:val $l)
     (let ((py:pair $name $v) $rest) (decons-atom $ks)
       (let $i (pyrt:param-index $names $name 0 $npo $total)
         (if (== $i -1)
             (if (== $dstar (py:True))
                 (case (pyrt:setitem (index-atom $l $kwslot) $name $v)
                   (((m:val $ok) (pyrt:bind-keywords $l $rest $all $names $npo $total $dstar $kwslot $q)) ($e $e)))
                 (let $conflicts (pyrt:posonly-conflicts $names 0 $npo $all)
                   (if (> (size-atom $conflicts) 0)
                       (m:err (pyrt:exc "TypeError" (str:concat (str:concat (str:concat $q "() got some positional-only arguments passed as keyword arguments: '")
                                                                            (str:join ", " $conflicts)) "'")))
                       (m:err (pyrt:exc "TypeError" (pyrt:unexpected-keyword $q $name (pyrt:names-between $names $npo $total)))))))
             (if (== (index-atom $l $i) (m:unbound))
                 (pyrt:bind-keywords (pyrt:tuple-set $l $i $v) $rest $all $names $npo $total $dstar $kwslot $q)
                 (m:err (pyrt:exc "TypeError" (str:concat (str:concat (str:concat $q "() got multiple values for argument '") $name) "'")))))))))
(= (pyrt:param-index $names $name $i $from $to)
   (if (>= $i $to) -1
     (case $names
       (((LCons $n $rest) (if (if (>= $i $from) (== $n $name) false) $i (pyrt:param-index $rest $name (+ $i 1) $from $to)))
        ($none -1)))))
(= (pyrt:posonly-conflicts $names $i $npo $all)
   (if (>= $i $npo) ()
     (pyrt:append (pyrt:keywords-named $all (pyrt:nth-name $names $i)) (pyrt:posonly-conflicts $names (+ $i 1) $npo $all))))
(= (pyrt:keywords-named $ks $p)
   (if (== (size-atom $ks) 0) ()
     (let ((py:pair $name $v) $rest) (decons-atom $ks)
       (if (== $name $p) (cons-atom $name (pyrt:keywords-named $rest $p)) (pyrt:keywords-named $rest $p)))))
(= (pyrt:names-between $names $from $to)
   (if (>= $from $to) () (cons-atom (pyrt:nth-name $names $from) (pyrt:names-between $names (+ $from 1) $to))))
(= (pyrt:count-bound $l $from $to)
   (if (>= $from $to) 0 (+ (if (== (index-atom $l $from) (m:unbound)) 0 1) (pyrt:count-bound $l (+ $from 1) $to))))
(= (pyrt:unexpected-keyword $q $name $candidates)
   (let $base (str:concat (str:concat (str:concat $q "() got an unexpected keyword argument '") $name) "'")
     (let $near (pyrt:suggestion $name $candidates)
       (if (== $near (m:missing)) $base (str:concat (str:concat (str:concat $base ". Did you mean '") $near) "'?")))))

; CPython's suggestion of a near name (_Py_CalculateSuggestions): the
; candidate at the least edit distance over UTF-8 bytes (a move costs 2, a
; change of ASCII case 1) within a third of the bytes involved, the first
; such one on a tie; none among 750 or more candidates
(= (pyrt:suggestion $name $candidates)
   (if (>= (size-atom $candidates) 750) (m:missing)
     (pyrt:suggest-from (str:byte-values $name) $name $candidates (m:missing) 0)))
(= (pyrt:suggest-from $nb $name $cs $best $bestd)
   (if (== (size-atom $cs) 0) $best
     (let ($c $rest) (decons-atom $cs)
       (if (== $c $name) (pyrt:suggest-from $nb $name $rest $best $bestd)
         (let $cb (str:byte-values $c)
           (let $max0 (pyrt:floor-div (* (+ (+ (size-atom $nb) (size-atom $cb)) 3) 2) 6)
             (let $max (if (== $best (m:missing)) $max0 (pyrt:min2 $max0 (- $bestd 1)))
               (let $d (pyrt:edit-distance $nb $cb $max)
                 (if (> $d $max) (pyrt:suggest-from $nb $name $rest $best $bestd)
                     (pyrt:suggest-from $nb $name $rest $c $d))))))))))
(= (pyrt:min2 $x $y) (if (< $x $y) $x $y))
; the distance, or MAX + 1 when it is more: the common prefix and suffix
; trimmed; more than 40 bytes left on either side is too far; one row of the
; table kept, and a row whose least entry is past MAX ends the search.  The
; distance is the same read backwards, so the suffix is trimmed as a prefix
(= (pyrt:edit-distance $a $b $max)
   (let (ed:two $a1 $b1) (pyrt:trim-common $a $b)
     (let (ed:two $ra $rb) (pyrt:trim-common (pyrt:rev-onto $a1 ()) (pyrt:rev-onto $b1 ()))
       (let $na (size-atom $ra)
         (let $nb (size-atom $rb)
           (if (if (== $na 0) true (== $nb 0)) (* (+ $na $nb) 2)
             (if (if (> $na 40) true (> $nb 40)) (+ $max 1)
               (if (< $nb $na) (pyrt:edit-rows-from $rb $ra $max) (pyrt:edit-rows-from $ra $rb $max)))))))))
(= (pyrt:trim-common $a $b)
   (if (if (> (size-atom $a) 0) (if (> (size-atom $b) 0) (== (car-atom $a) (car-atom $b)) false) false)
       (pyrt:trim-common (cdr-atom $a) (cdr-atom $b))
       (ed:two $a $b)))
(= (pyrt:rev-onto $es $acc)
   (if (== (size-atom $es) 0) $acc (let ($x $rest) (decons-atom $es) (pyrt:rev-onto $rest (cons-atom $x $acc)))))
; A the shorter: a length difference alone may already be too far
(= (pyrt:edit-rows-from $a $b $max)
   (if (> (* (- (size-atom $b) (size-atom $a)) 2) $max) (+ $max 1)
     (pyrt:edit-rows $a $b (pyrt:edit-first-row (size-atom $a) 1) 0 $max 0)))
(= (pyrt:edit-first-row $n $k) (if (> $k $n) () (cons-atom (* $k 2) (pyrt:edit-first-row $n (+ $k 1)))))
(= (pyrt:edit-rows $a $b $row $bi $max $last)
   (if (== (size-atom $b) 0) $last
     (let ($code $brest) (decons-atom $b)
       (let $next (pyrt:edit-row $a $row $code (* $bi 2) (* $bi 2))
         (if (> (pyrt:least $next) $max) (+ $max 1)
           (pyrt:edit-rows $a $brest $next (+ $bi 1) $max (pyrt:last-of $next)))))))
(= (pyrt:edit-row $a $row $code $diag $left)
   (if (== (size-atom $a) 0) ()
     (let ($x $arest) (decons-atom $a)
       (let ($up $rrest) (decons-atom $row)
         (let $here (pyrt:min2 (+ (pyrt:min2 $left $up) 2) (+ $diag (pyrt:byte-cost $code $x)))
           (cons-atom $here (pyrt:edit-row $arest $rrest $code $up $here)))))))
(= (pyrt:byte-cost $x $y)
   (if (== $x $y) 0
     (if (!= (% $x 32) (% $y 32)) 2
       (if (== (pyrt:ascii-lower $x) (pyrt:ascii-lower $y)) 1 2))))
(= (pyrt:ascii-lower $x) (if (if (>= $x 65) (<= $x 90) false) (+ $x 32) $x))
(= (pyrt:least $xs) (let ($x $rest) (decons-atom $xs) (if (== (size-atom $rest) 0) $x (pyrt:min2 $x (pyrt:least $rest)))))
(= (pyrt:last-of $xs) (index-atom $xs (- (size-atom $xs) 1)))
; the defaults belong to the last positional parameters, from slot FROM
(= (pyrt:fill-defaults $l $dflts $from $k)
   (if (== $dflts ()) $l
     (let ($d $rest) (decons-atom $dflts)
       (let $i (+ $from $k)
         (pyrt:fill-defaults (if (== (index-atom $l $i) (m:unbound)) (pyrt:tuple-set $l $i $d) $l) $rest $from (+ $k 1))))))
(= (pyrt:fill-kwdefaults $l $kwd $names $from $to)
   (if (>= $from $to) $l
     (let $name (pyrt:nth-name $names $from)
       (let $d (pyrt:kwdefault-get $kwd $name)
         (pyrt:fill-kwdefaults (if (== (index-atom $l $from) (m:unbound)) (pyrt:tuple-set $l $from $d) $l) $kwd $names (+ $from 1) $to)))))
(= (pyrt:missing-params $locals $names $i $to)
   (if (>= $i $to) ()
     (if (== (index-atom $locals $i) (m:unbound))
         (cons-atom (pyrt:nth-name $names $i) (pyrt:missing-params $locals $names (+ $i 1) $to))
         (pyrt:missing-params $locals $names (+ $i 1) $to))))
(= (pyrt:missing-error $q $missing $what)
   (pyrt:exc "TypeError"
     (str:concat (str:concat (str:concat $q "() missing ") (str:number->string (size-atom $missing)))
                 (str:concat (str:concat " required " $what)
                             (str:concat (pyrt:plural (size-atom $missing) " argument: " " arguments: ") (pyrt:name-list $missing))))))
; CPython's words for too many positional arguments, which count the
; keyword-only arguments given too
(= (pyrt:too-many-positional $q $npos $ndflt $n $kg)
   (let $sig (if (> $ndflt 0)
                 (str:concat (str:concat (str:concat "from " (str:number->string (- $npos $ndflt))) " to ") (str:number->string $npos))
                 (str:number->string $npos))
     (let $takes (if (if (> $ndflt 0) true (!= $npos 1)) " positional arguments but " " positional argument but ")
       (let $kwsig (if (> $kg 0)
                       (str:concat (str:concat (str:concat (pyrt:plural $n " positional argument" " positional arguments") " (and ")
                                               (str:number->string $kg))
                                   (pyrt:plural $kg " keyword-only argument)" " keyword-only arguments)"))
                       "")
         (pyrt:exc "TypeError"
           (str:concat (str:concat (str:concat (str:concat (str:concat $q "() takes ") $sig) $takes)
                                   (str:concat (str:number->string $n) $kwsig))
                       (if (if (== $n 1) (== $kg 0) false) " was given" " were given")))))))
; a cell for each captured local, holding the slot's value; the closure's
; cells in the free names' slots
(= (pyrt:make-cells $l $cells)
   (case $cells (((LNil) $l) ((LCons $i $rest) (pyrt:make-cells (pyrt:tuple-set $l $i (pyrt:cell-new (index-atom $l $i))) $rest)))))
(= (pyrt:fill-frees $l $frees $clo)
   (case $frees
     (((LNil) $l)
      ((LCons $i $rest) (let ($cell $more) (decons-atom $clo) (pyrt:fill-frees (pyrt:tuple-set $l $i $cell) $rest $more))))))
; CPython's words for lists of names
(= (pyrt:plural $n $one $many) (if (== $n 1) $one $many))
(= (pyrt:name-list $ns)
   (let $k (size-atom $ns)
     (if (== $k 1) (pyrt:quoted (car-atom $ns))
       (if (== $k 2) (str:concat (str:concat (pyrt:quoted (car-atom $ns)) " and ") (pyrt:quoted (index-atom $ns 1)))
         (pyrt:name-list-many $ns)))))
(= (pyrt:name-list-many $ns)
   (let ($n $rest) (decons-atom $ns)
     (if (== (size-atom $rest) 1) (str:concat "and " (pyrt:quoted $n))
       (str:concat (str:concat (pyrt:quoted $n) ", ") (pyrt:name-list-many-tail $rest)))))
(= (pyrt:name-list-many-tail $ns)
   (let ($n $rest) (decons-atom $ns)
     (if (== $rest ()) (str:concat "and " (pyrt:quoted $n))
       (str:concat (str:concat (pyrt:quoted $n) ", ") (pyrt:name-list-many-tail $rest)))))

; ---------------------------------------------------------------- floats

; Python's floats are IEEE 754 doubles, lib/float's: an overflow is an
; infinity; the errors are Python's own, checked before the arithmetic
(= (pyrt:is-float $x)
   (if (== (get-metatype $x) Grounded) (if (== (get-type $x) Number) (== (binary:portable-kind $x) Float) false) false))
(= (pyrt:is-number $x) (if (pyrt:intish $x) true (pyrt:is-float $x)))
; an int or a bool as a float: the nearest; too large is an OverflowError
(= (pyrt:as-float $x)
   (if (pyrt:is-float $x) (m:val $x)
     (case (float:from-int (pyrt:int-of $x))
       (((FloatOverflow) (m:err (pyrt:exc "OverflowError" "int too large to convert to float")))
        ($f (m:val $f))))))
(= (pyrt:float-zero $x) (== (float:compare $x 0.0) 0))
(= (pyrt:float-negative $x) (== (float:compare $x 0.0) -1))
(= (pyrt:float-binop $o $x $y)
   (case (pyrt:as-float $x)
     (((m:val $a) (case (pyrt:as-float $y) (((m:val $b) (pyrt:float-op $o $a $b)) ($e $e))))
      ($e $e))))
(= (pyrt:float-op $o $a $b)
   (case $o
     (((py:Add) (m:val (float:add $a $b)))
      ((py:Sub) (m:val (float:sub $a $b)))
      ((py:Mult) (m:val (float:mul $a $b)))
      ((py:Div) (if (pyrt:float-zero $b) (pyrt:zero-division "division by zero") (m:val (float:div $a $b))))
      ((py:FloorDiv) (if (pyrt:float-zero $b) (pyrt:zero-division "division by zero") (m:val (pyrt:float-floor-div $a $b))))
      ((py:Mod) (if (pyrt:float-zero $b) (pyrt:zero-division "division by zero") (m:val (pyrt:float-mod $a $b))))
      ((py:Pow) (pyrt:float-pow $a $b)))))
; CPython's float_rem: fmod, then the remainder takes the divisor's sign; a
; zero remainder is a zero of the divisor's sign
(= (pyrt:float-mod $a $b)
   (let $m (float:fmod $a $b)
     (if (pyrt:float-zero $m) (float:copysign 0.0 $b)
       (if (== (pyrt:float-negative $b) (pyrt:float-negative $m)) $m (float:add $m $b)))))
; CPython's _float_div_mod: (a - fmod(a, b)) / b, one less when the remainder
; changed sign, snapped to the nearest whole number; a zero quotient takes
; the true quotient's sign
(= (pyrt:float-floor-div $a $b)
   (let $m (float:fmod $a $b)
     (let $d0 (float:div (float:sub $a $m) $b)
       (let $d (if (pyrt:float-zero $m) $d0 (if (== (pyrt:float-negative $b) (pyrt:float-negative $m)) $d0 (float:sub $d0 1.0)))
         (if (pyrt:float-zero $d) (float:copysign 0.0 (float:div $a $b))
           (let $f (float:floor $d) (if (== (float:compare (float:sub $d $f) 0.5) 1) (float:add $f 1.0) $f)))))))
; CPython's float_pow: the special cases first, then C's pow; an overflow is
; an OverflowError, an underflow is not an error
(= (pyrt:float-pow $a $b)
   (if (pyrt:float-zero $b) (m:val 1.0)
     (if (float:is-nan $a) (m:val $a)
       (if (float:is-nan $b) (m:val (if (== (float:compare $a 1.0) 0) 1.0 $b))
         (if (float:is-inf $b)
             (let $m (float:abs $a)
               (if (== (float:compare $m 1.0) 0) (m:val 1.0)
                 (if (== (pyrt:float-negative $b) (== (float:compare $m 1.0) -1)) (m:val (float:abs $b)) (m:val 0.0))))
             (if (float:is-inf $a)
                 (if (pyrt:float-negative $b)
                     (m:val (if (pyrt:float-odd $b) (float:copysign 0.0 $a) 0.0))
                     (m:val (if (pyrt:float-odd $b) $a (float:abs $a))))
                 (if (pyrt:float-zero $a)
                     (if (pyrt:float-negative $b) (pyrt:zero-division "zero to a negative power")
                         (m:val (if (pyrt:float-odd $b) $a 0.0)))
                     (if (pyrt:float-negative $a)
                         (if (== (float:compare $b (float:floor $b)) 0)
                             (pyrt:float-pow-positive (float:neg $a) $b (pyrt:float-odd $b))
                             (pyrt:float-complex-pow $a $b))
                         (pyrt:float-pow-positive $a $b false)))))))))
(= (pyrt:float-pow-positive $a $b $negate)
   (if (== (float:compare $a 1.0) 0) (m:val (if $negate -1.0 1.0))
     (case (float:pow $a $b)
       (((FloatPowed $v ok) (m:val (if $negate (float:neg $v) $v)))
        ((FloatPowed $v range)
         (if (pyrt:float-zero $v) (m:val (if $negate (float:neg $v) $v))
             (m:err (pyrt:exc-args "OverflowError" (cons-atom 34 (cons-atom "Numerical result out of range" ()))))))
        ((FloatPowed $v domain) (m:err (pyrt:exc-args "ValueError" (cons-atom 33 (cons-atom "Numerical argument out of domain" ())))))))))
(= (pyrt:float-odd $x) (== (float:compare (float:fmod (float:abs $x) 2.0) 1.0) 0))
; == between a float and a number: exact (NaN equals nothing)
(= (pyrt:float-eq $x $y)
   (if (pyrt:is-float $y) (== (float:compare $x $y) 0)
     (if (pyrt:intish $y) (== (float:compare-int $x (pyrt:int-of $y)) 0) false)))
; an order between numbers, one of them a float: exact; any order with a NaN
; is false
(= (pyrt:float-order $o $x $y)
   (let $c (if (pyrt:is-float $x)
               (if (pyrt:is-float $y) (float:compare $x $y) (float:compare-int $x (pyrt:int-of $y)))
               (let $r (float:compare-int $y (pyrt:int-of $x)) (if (== $r unordered) unordered (- 0 $r))))
     (if (== $c unordered) False (pyrt:order-holds $o $c 0))))
; repr(x) of a float (CPython's 'r' format): the shortest digits that read
; back, a point placed when the decimal point position is above -4 and at
; most 16, else an exponent of at least two digits; a whole number ends ".0"
(= (pyrt:float-repr $x)
   (case (float:digits $x)
     (((FloatSpecial $name) $name)
      ((FloatDigits $sign $ds $decpt) (str:concat $sign (pyrt:place-digits $ds $decpt))))))
(= (pyrt:place-digits $ds $decpt)
   (let $n (str:byte-length $ds)
     (if (if (<= $decpt -4) true (> $decpt 16))
         (str:concat (str:concat (if (== $n 1) $ds (str:concat (str:concat (str:byte-slice $ds 0 1) ".") (str:byte-slice $ds 1 $n))) "e")
                     (pyrt:exponent-text (- $decpt 1)))
         (if (<= $decpt 0)
             (str:concat (str:concat "0." (pyrt:repeat "0" (- 0 $decpt))) $ds)
             (if (>= $decpt $n)
                 (str:concat (str:concat $ds (pyrt:repeat "0" (- $decpt $n))) ".0")
                 (str:concat (str:concat (str:byte-slice $ds 0 $decpt) ".") (str:byte-slice $ds $decpt $n)))))))
(= (pyrt:exponent-text $e)
   (let $a (if (< $e 0) (- 0 $e) $e)
     (str:concat (if (< $e 0) "-" "+") (if (< $a 10) (str:concat "0" (str:number->string $a)) (str:number->string $a)))))

; ---------------------------------------------------------------- objects with fields

; a heap object's fields: (h:f ID FIELD VALUE)
(= (pyrt:obj-id (py:obj $id)) $id)
(= (pyrt:obj-id (py:type $id)) $id)
(= (pyrt:obj-id (py:func $id)) $id)
(= (pyrt:obj-id (py:gen $id)) $id)
(= (pyrt:obj-id (py:coro $id)) $id)
(= (pyrt:obj-id (py:agen $id)) $id)
; a function: (py:func ID), its fields code, globals, defaults (a list),
; kwdefaults (name and value pairs), closure (cells), name, qualname and
; module (the globals' __name__ when made), doc (its code's first constant),
; annotate (its __annotate__ function, PEP 649; annotations, the dict it
; gives, once asked for), and a __dict__ made when first asked
(= (pyrt:new-function (c:code $nm $q $a $ns $cs $fs $fl $body $cofile) $g $ds $ks $cl $annotate)
   (let $f (py:func (pyrt:new-id))
     (let $u1 (pyrt:set-field $f code (c:code $nm $q $a $ns $cs $fs $fl $body $cofile))
       (let $u2 (pyrt:set-field $f globals $g)
         (let $u3 (pyrt:set-field $f defaults $ds)
           (let $u4 (pyrt:set-field $f kwdefaults $ks)
             (let $u5 (pyrt:set-field $f closure $cl)
               (let $u6 (pyrt:set-field $f name $nm)
                 (let $u7 (pyrt:set-field $f qualname $q)
                   (let $u8 (pyrt:set-field $f module (let $m (pyrt:dict-get $g "__name__") (if (== $m (m:missing)) (py:None) $m)))
                     (let $u9 (pyrt:set-field $f doc (case $fl (((c:function $d) $d) ((c:generator $d) $d) ((c:coroutine $d) $d) ((c:async-generator $d) $d) ($o (py:None)))))
                       (let $u10 (if (== $annotate (py:None)) () (pyrt:set-field $f annotate $annotate))
                         $f))))))))))))
(= (pyrt:field $o $name) (let $id (pyrt:obj-id $o) (pyrt:heap-one (h:f $id $name $v) $v)))
(= (pyrt:field-or $o $name $default) (let $v (pyrt:field $o $name) (if (== $v (m:missing)) $default $v)))
; a type's fully qualified name (PyType_GetFullyQualifiedName, the %T of
; CPython's messages): MODULE.QUALNAME, the qualname alone in builtins and
; __main__
(= (pyrt:type-fqn $t)
   (let $m (pyrt:module-of-type $t)
     (if (if (== $m "builtins") true (== $m "__main__")) (pyrt:qualname-of $t)
         (str:concat (str:concat $m ".") (pyrt:qualname-of $t)))))
; a value's field for the prelude: an object's from the heap; a range's or a
; slice's parts by name (their members start, stop and step)
(= (pyrt:value-field $o $name)
   (case $o
     (((py:range $ra $rb $rc) (pyrt:part-named $name $ra $rb $rc))
      ((py:slice $sa $sb $sc) (pyrt:part-named $name $sa $sb $sc))
      ($other (pyrt:field-or $o $name (py:None))))))
(= (pyrt:part-named $name $p $q $r) (case $name ((start $p) (stop $q) (step $r) ($n (py:None)))))
(= (pyrt:set-field $o $name $value)
   (let $id (pyrt:obj-id $o) (let $old (pyrt:heap-one (h:f $id $name $x) $x)
     (if (== $old (m:missing))
         (let $a (add-atom &py-heap (h:f $id $name $value)) ())
         (pyrt:heap-replace (h:f $id $name $old) (h:f $id $name $value))))))
(= (pyrt:clear-field $o $name)
   (let $id (pyrt:obj-id $o) (let $old (pyrt:heap-one (h:f $id $name $x) $x)
     (if (== $old (m:missing)) () (let $r (pyrt:heap-remove (h:f $id $name $old)) ())))))
(= (pyrt:obj-new $cls) (let $id (pyrt:new-id) (let $a (add-atom &py-heap (h:f $id class $cls)) (py:obj $id))))
; a lazy import's placeholder: its id is (lazy N), CPython's exact-type check
; (PyLazyImport_CheckExact) a match on the value's shape
(= (pyrt:lazy-new) (let $id (lazy (pyrt:new-id)) (let $a (add-atom &py-heap (h:f $id class (py:type lazy_import))) (py:obj $id))))
(= (pyrt:in-flat $x $xs)
   (if (== (size-atom $xs) 0) false (let ($y $rest) (decons-atom $xs) (if (== $x $y) true (pyrt:in-flat $x $rest)))))

; ---------------------------------------------------------------- exceptions

; the host's errors: (py:exc TYPE MESSAGE), whose args are the message, and
; (py:exca TYPE ARGS); raised, an error becomes an instance of its type
(= (pyrt:exc-args $type $args) (py:exca $type $args))
(= (pyrt:exc-instance $e)
   (case $e
     (((py:exc $t $m) (pyrt:exc-new (py:type (str:string->symbol $t)) (cons-atom $m ())))
      ((py:exca $t $as) (pyrt:exc-new (py:type (str:string->symbol $t)) $as))
      ($other $other))))
; an instance of an exception class, made with its arguments
(= (pyrt:exc-new $cls $args)
   (let $o (pyrt:obj-new $cls) (let $a (pyrt:set-field $o args (cons-atom py:tuple $args)) $o)))
(= (pyrt:is-exc-class $x) (case $x (((py:type $t) (pyrt:in-mro (py:type BaseException) (pyrt:mro-of $x))) ($o false))))
(= (pyrt:is-exc-instance $x) (case $x (((py:obj $id) (pyrt:is-exc-class (pyrt:field $x class))) ($o false))))
(= (pyrt:exc-is $x $t) (pyrt:in-mro (py:type $t) (pyrt:mro-of (pyrt:type-of $x))))
; calling an exception class: its args; NameError, AttributeError and
; ImportError take their keywords, any other takes none
(= (pyrt:make-exception $t $xs $ks)
   (if (== (size-atom $ks) 0) (m:val (pyrt:import-msg $t (pyrt:exc-new (py:type $t) $xs) $xs))
     (let $allowed (pyrt:exception-keywords $t)
       (if (== (size-atom $allowed) 0)
           (m:err (pyrt:exc "TypeError" (str:concat (str:symbol->string $t) "() takes no keyword arguments")))
           (let $o (pyrt:import-msg $t (pyrt:exc-new (py:type $t) $xs) $xs) (pyrt:exception-fields $o $t $ks $allowed))))))
; ImportError_init: one argument is also the error's msg
(= (pyrt:import-msg $t $o $xs)
   (if (pyrt:in-flat $t (ImportError ModuleNotFoundError))
       (if (== (size-atom $xs) 1) (let $u (pyrt:set-field $o msg (car-atom $xs)) $o) $o)
       $o))
(= (pyrt:exception-keywords $t)
   (case $t
     ((NameError ("name")) (AttributeError ("name" "obj")) (ImportError ("name" "path" "name_from"))
      (ModuleNotFoundError ("name" "path" "name_from")) ($other ()))))
(= (pyrt:exception-fields $o $t $ks $allowed)
   (if (== (size-atom $ks) 0) (m:val $o)
     (let ((py:pair $name $v) $rest) (decons-atom $ks)
       (if (pyrt:in-flat $name $allowed)
           (let $u (pyrt:set-field $o (str:string->symbol $name) $v) (pyrt:exception-fields $o $t $rest $allowed))
           (m:err (pyrt:exc "TypeError" (str:concat (str:concat (str:concat (str:symbol->string $t) "() got an unexpected keyword argument '") $name) "'")))))))

; raising: the instance; its context the exception being handled, unless that
; is the same object, after a cycle through the handled one's contexts is
; broken (CPython's _PyErr_SetObject)
(= (pyrt:raise-start $e $f $k) (pyrt:tb-here (pyrt:raise-instance $e) $f $k))
(= (pyrt:raise-instance $e)
   (let $x (pyrt:raised (pyrt:exc-instance $e))
     (if (== (pyrt:field-or $x propagating False) True)
         (let $u (pyrt:clear-field $x propagating) $x)
         (let $h (pyrt:handled-top)
           (let $u (if (== $h (m:none)) ()
                       (if (== $h $x) () (let $b (pyrt:break-context-cycle $h $x ()) (pyrt:set-field $x context $h))))
             $x)))))
; ---------------------------------------------------------------- tracebacks

; PyTraceBack_Here: an exception raised in a frame, or passing through it on
; its way out, gets an entry for the frame (a traceback object: the frame,
; the line it stands at, the entries it had after it); the prelude's frames
; and those of the modules standing for CPython's C ones get none
(= (pyrt:tb-here $x $f $k)
   (if (pyrt:frame-hidden $f) $x
     (let (c:tb-made $fo $old) (pyrt:tb-push $x $f $k) $x)))
(= (pyrt:tb-push $x $f $k)
   (let $fo (pyrt:frame-object $f (pyrt:k-span $k))
     (let $old (pyrt:field-or $x tb (py:None))
       (let $tb (pyrt:new-object (py:type traceback))
         (let $u1 (pyrt:set-field $tb frame $fo)
           (let $u2 (pyrt:set-field $tb lineno (pyrt:field $fo lineno))
             (let $u3 (pyrt:set-field $tb next $old)
               (let $u4 (pyrt:set-field $x tb $tb) (c:tb-made $fo $old)))))))))
; leaving CALLEE for its caller FRAME: the caller's entry, and the frame of the
; entry before it (the nearest frame CPython shows below the caller) given the
; caller as its f_back
(= (pyrt:traceback-here $e $callee $f $k)
   (let $x (pyrt:exc-instance $e)
     (if (pyrt:frame-hidden $f) $e
       (let (c:tb-made $fo $old) (pyrt:tb-push $x $f $k)
         (let $u (if (== $old (py:None)) () (pyrt:set-field (pyrt:field $old frame) back $fo)) $e)))))
(= (pyrt:frame-hidden (m:frame $l $g $ns (c:code $nm $q $a $n $cs $fs $fl $body (c:src $file $first)))) (== $file (c:builtin)))
; an exception raised again in FRAME (thrown into a generator, or a delegate's
; error at a yield from): FRAME's entry
(= (pyrt:tb-here-again $e $f $k) (pyrt:tb-here (pyrt:exc-instance $e) $f $k))
; where a frame stands: its innermost statement's mark on its continuation,
; up to where its activation began (a call, the host, a generator's delimiter)
(= (pyrt:k-span $k)
   (case $k
     (((k:at $sp $rest) $sp)
      ((k:stmts $sp $rest $k2) $sp)
      ((k:call $t $f $rest) (c:no-span))
      ((k:gen $g $rest) (c:no-span))
      ((k:host) (c:no-span))
      ((k:top) (c:no-span))
      ((k:hole) (c:no-span))
      ($frame (pyrt:k-span (pyrt:last-of $frame))))))
(= (pyrt:code-file (c:src $file $first)) $file)
; one code object for each code (PyCodeObject): a function's __code__ and its
; frames' f_code are the same object
!(bind! &py-codes (new-space))
(= (pyrt:code-object-of $c)
   (let $found (collapse (match &py-codes (pyco $c $o) $o))
     (if (== $found ())
         (let $o (pyrt:code-object $c (pyrt:code-file (pyrt:last-of $c)))
           (let $u (add-atom &py-codes (pyco $c $o)) $o))
         (car-atom $found))))
; what a code object tells (co_*): (QUALNAME FIRSTLINENO ARGCOUNT POSONLY KWONLY
; VARNAMES CELLVARS FREEVARS): its parameters and other locals by slot, its
; cells sorted (CPython's dictbytype), its free names in the closure's order
(= (pyrt:code-info (c:code $nm $q (c:args $npo $np $nk $st $ds $names) $nsl $cells $frees $fl $body (c:src $file $first)))
   (let $vars (pyrt:code-varnames $names 0 (+ (+ $npo $np) (+ $nk (+ (if (== $st (py:True)) 1 0) (if (== $ds (py:True)) 1 0)))) $cells $frees)
     (py:tuple $q $first (+ $npo $np) $npo $nk (cons-atom py:tuple $vars)
               (cons-atom py:tuple (sort-atom (pyrt:slot-names $names $cells)))
               (cons-atom py:tuple (pyrt:slot-names $names $frees)))))
(= (pyrt:code-varnames $names $i $nparams $cells $frees)
   (case $names
     (((LNil) ())
      ((LCons $n $rest)
       (let $more (pyrt:code-varnames $rest (+ $i 1) $nparams $cells $frees)
         (if (pyrt:in-lcons $i $frees) $more
           (if (if (< $i $nparams) false (pyrt:in-lcons $i $cells)) $more (cons-atom $n $more))))))))
(= (pyrt:slot-names $names $is)
   (case $is (((LNil) ()) ((LCons $i $rest) (cons-atom (pyrt:lnth-name $names $i) (pyrt:slot-names $names $rest))))))
(= (pyrt:lnth-name $names $i) (case $names (((LCons $n $rest) (if (== $i 0) $n (pyrt:lnth-name $rest (- $i 1)))))))
; a frame's builtins (f_builtins): its globals' __builtins__, a module's namespace
(= (pyrt:frame-builtins $g)
   (let $b (pyrt:dict-get $g "__builtins__")
     (case $b (((py:obj $id) (m:val (pyrt:object-dict $b))) ((m:missing) (m:val (pyrt:dict-new))) ($d (m:val $d))))))
; PyErr_WarnEx from the runtime's own operations (stack level 1): the warning
; attributed to the innermost frame CPython shows, at its line, through the
; warnings machinery (the prelude's _warn_at, _warnings.c's warn_explicit):
; (m:val None), or (m:err E) when a filter makes it an error
(= (pyrt:warn-at $cat $msg $f $k)
   (let $frs (pyrt:visible-frames $f $k)
     (if (== $frs ())
         (pyrt:prelude-sync "_warn_at" (cons-atom $cat (cons-atom $msg (cons-atom "<sys>" (cons-atom 0 (cons-atom (py:None) ()))))))
         (let ((c:fr (m:frame $l $g $ns $c) $sp) $rest) (decons-atom $frs)
           (pyrt:prelude-sync "_warn_at"
             (cons-atom $cat (cons-atom $msg (cons-atom (pyrt:code-file (pyrt:last-of $c))
               (cons-atom (case $sp (((c:span $sl $sc $sel $sec) $sl) ($none (pyrt:code-first-line (pyrt:last-of $c)))))
                 (cons-atom $g ()))))))))))
; sys._getframe(DEPTH): the frames this run of the machine is in, innermost
; first, those CPython shows (the prelude's are its C code), each with the
; frame it was called from as its f_back
(= (pyrt:frame-at $d $f $k)
   (let $frs (pyrt:visible-frames $f $k)
     (if (>= $d (size-atom $frs)) (m:err (pyrt:exc "ValueError" "call stack is not deep enough"))
       (m:val (pyrt:link-frames (pyrt:drop $frs $d))))))
(= (pyrt:visible-frames $f $k)
   (let $rest (pyrt:outer-frames $k)
     (if (pyrt:frame-hidden $f) $rest (cons-atom (c:fr $f (pyrt:k-span $k)) $rest))))
(= (pyrt:outer-frames $k)
   (case $k
     (((k:call $t $fc $k2) (pyrt:visible-frames $fc $k2))
      ((k:host) ()) ((k:top) ()) ((k:hole) ()) ((k:gen $g $k2) ())
      ($frame (pyrt:outer-frames (pyrt:last-of $frame))))))
(= (pyrt:link-frames $frs)
   (if (== $frs ()) (py:None)
     (let ((c:fr $f $sp) $rest) (decons-atom $frs)
       (let $back (pyrt:link-frames $rest)
         (let $fo (pyrt:frame-object $f $sp) (let $u (pyrt:set-field $fo back $back) $fo))))))
(= (pyrt:code-first-line (c:src $file $first)) $first)
(= (pyrt:span-line $sp) (case $sp (((c:span $l $c $el $ec) $l) ($none (py:None)))))
; a frame object (PyFrameObject) for a traceback or sys._getframe: the
; machine's frame as it is (its slots, globals, namespace, code), the line it
; stands at, its file and its code's name; f_back set when its caller's is made
(= (pyrt:frame-object (m:frame $l $g $ns (c:code $nm $q $a $n $cs $fs $fl $body $cofile)) $sp)
   (let $fo (pyrt:new-object (py:type frame))
     (let $u1 (pyrt:set-field $fo mframe (m:frame $l $g $ns (c:code $nm $q $a $n $cs $fs $fl $body $cofile)))
       (let $u2 (pyrt:set-field $fo lineno (case $sp (((c:span $sl $sc $sel $sec) $sl) ($none (pyrt:code-first-line $cofile)))))
         (let $u3 (pyrt:set-field $fo filename (pyrt:code-file $cofile))
           (let $u4 (pyrt:set-field $fo name $nm)
             (let $u5 (pyrt:set-field $fo back (py:None)) $fo)))))))
; a raised exception is marked so: it is one CPython reports with a traceback
(= (pyrt:raised $x) (case $x (((py:obj $id) (let $u (pyrt:set-field $x raised True) $x)) ($o $o))))
; an exception leaving a run of the machine for its caller is propagating, as
; CPython's error indicator is when a call returns NULL: where the caller
; takes it up it is not chained again (an exception is chained when raised)
(= (pyrt:propagating $x)
   (case $x
     (((py:obj $id) (let $u (pyrt:set-field $x propagating True) $x))
      ($other $other))))
(= (pyrt:break-context-cycle $o $x $seen)
   (let $c (pyrt:field-or $o context (py:None))
     (if (== $c (py:None)) ()
       (if (== $c $x) (pyrt:set-field $o context (py:None))
         (if (pyrt:in-flat $c $seen) () (pyrt:break-context-cycle $c $x (cons-atom $o $seen)))))))
; the exceptions being handled
(= (pyrt:handle $e) (let $s (get-state &py-handled) (let $u (change-state! &py-handled (cons-atom $e $s)) ok)))
(= (pyrt:unhandle) (let $s (get-state &py-handled) (let $u (change-state! &py-handled (cdr-atom $s)) ok)))
(= (pyrt:handled-top) (let $s (get-state &py-handled) (if (== (size-atom $s) 0) (m:none) (car-atom $s))))
(= (pyrt:handled-now)
   (let $h (pyrt:handled-top)
     (if (== $h (m:none)) (m:err (pyrt:exc "RuntimeError" "No active exception to reraise")) (m:val $h))))

; raise X: an exception as it is, an exception class called with no
; arguments; raise X from C: C likewise, or None; anything else is an error
(= (pyrt:exc-of $x)
   (if (pyrt:is-exc-instance $x) (m:val $x)
     (if (pyrt:is-exc-class $x) (pyrt:call-kw $x () ())
       (m:err (pyrt:exc "TypeError" "exceptions must derive from BaseException")))))
(= (pyrt:cause-of $x)
   (if (== $x (py:None)) (m:val $x)
     (if (pyrt:is-exc-instance $x) (m:val $x)
       (if (pyrt:is-exc-class $x) (pyrt:call-kw $x () ())
         (m:err (pyrt:exc "TypeError" "exception causes must derive from BaseException"))))))
(= (pyrt:not-an-exception $made $called)
   (pyrt:exc "TypeError" (str:concat (str:concat (str:concat "calling " (pyrt:repr $called))
                                                 " should have returned an instance of BaseException, not ")
                                     (pyrt:repr (pyrt:type-of $made)))))
(= (pyrt:apply2 check-exc $t $a) (if (pyrt:is-exc-instance $t) (m:val $t) (m:err (pyrt:not-an-exception $t $a))))
(= (pyrt:with-cause $t $a $tc $ca)
   (if (pyrt:is-exc-instance $t)
       (if (if (== $tc (py:None)) true (pyrt:is-exc-instance $tc))
           (let $u1 (pyrt:set-field $t cause $tc) (let $u2 (pyrt:set-field $t suppress True) (m:val $t)))
           (m:err (pyrt:not-an-exception $tc $ca)))
       (m:err (pyrt:not-an-exception $t $a))))
; except C: C an exception class or a tuple of them (not nested), then the
; exception's class or a base of it (PyErr_GivenExceptionMatches: the MRO,
; never a __subclasscheck__)
(= (pyrt:apply2 exc-match $t $c)
   (if (if (pyrt:is-tuple $c) (pyrt:all-exc-classes (cdr-atom $c)) (pyrt:is-exc-class $c))
       (pyrt:class-check (pyrt:type-of $t) $c "isinstance() arg 2 must be a type, a tuple of types, or a union")
       (m:err (pyrt:exc "TypeError" "catching classes that do not inherit from BaseException is not allowed"))))
(= (pyrt:all-exc-classes $cs)
   (if (== (size-atom $cs) 0) true (let ($c $rest) (decons-atom $cs) (if (pyrt:is-exc-class $c) (pyrt:all-exc-classes $rest) false))))

; str(e): no args "", one arg its str (KeyError: its repr), more the args'
; repr; repr(e): the class's name and the args
(= (pyrt:exc-str $x)
   (if (pyrt:exc-is $x SyntaxError) (pyrt:syntax-error-str $x) (pyrt:exc-str-of-args $x)))
; SyntaxError_str: the message, then the file's base name and the line when
; it has them
(= (pyrt:syntax-error-str $x)
   (let $msg (pyrt:text-of (pyrt:str-of (pyrt:syntax-value $x msg 0)))
     (let $file (pyrt:syntax-value $x filename 1)
       (let $line (pyrt:syntax-value $x lineno 2)
         (let $hasl (pyrt:is-int $line)
           (if (pyrt:is-str $file)
               (let $base (pyrt:basename $file)
                 (if $hasl
                     (str:concat (str:concat (str:concat (str:concat $msg " (") $base) ", line ") (str:concat (str:number->string $line) ")"))
                     (str:concat (str:concat (str:concat $msg " (") $base) ")")))
               (if $hasl (str:concat (str:concat (str:concat $msg " (line ") (str:number->string $line)) ")") $msg)))))))
(= (pyrt:basename $path)
   (let $parts (str:split "/" $path) (index-atom $parts (- (size-atom $parts) 1))))
; SyntaxError_init: msg its first argument; its second (filename, lineno,
; offset, text[, end_lineno, end_offset]) the others; each None when absent
(= (pyrt:syntax-attr $x $i)
   (let $args (cdr-atom (pyrt:field $x args))
     (if (== $i 0) (if (> (size-atom $args) 0) (car-atom $args) (py:None))
       (if (== (size-atom $args) 2)
           (let $info (index-atom $args 1)
             (if (pyrt:is-tuple $info) (if (< $i (size-atom $info)) (index-atom $info $i) (py:None)) (py:None)))
           (py:None)))))
; a SyntaxError's msg, filename, ...: as assigned, else as its arguments give
(= (pyrt:syntax-value $o $f $i)
   (let $v (pyrt:field $o $f) (if (== $v (m:missing)) (pyrt:syntax-attr $o $i) $v)))
(= (pyrt:syntax-field $o $f $i)
   (m:val (if (pyrt:exc-is $o SyntaxError) (pyrt:syntax-value $o $f $i) (pyrt:field-or $o $f (py:None)))))
(= (pyrt:exc-str-of-args $x)
   (let $args (cdr-atom (pyrt:field $x args))
     (let $n (size-atom $args)
       (if (== $n 0) ""
         (if (== $n 1)
             (if (pyrt:exc-is $x KeyError) (pyrt:text-of (pyrt:repr-of (car-atom $args))) (pyrt:text-of (pyrt:str-of (car-atom $args))))
             (pyrt:repr (pyrt:field $x args)))))))
(= (pyrt:exc-repr $x)
   (let $args (pyrt:field $x args)
     (str:concat (pyrt:type-name-of (pyrt:type-of $x))
       (if (== (size-atom $args) 2) (str:concat (str:concat "(" (pyrt:repr (index-atom $args 1))) ")") (pyrt:repr $args)))))
(= (pyrt:exit-code $x)
   (let $args (cdr-atom (pyrt:field $x args))
     (if (== (size-atom $args) 0) (py:None) (if (== (size-atom $args) 1) (car-atom $args) (pyrt:field $x args)))))
; an exception's attributes
(= (pyrt:exc-attribute $x $name)
   (case $name
     (("args" (m:val (pyrt:field $x args)))
      ("__context__" (m:val (pyrt:field-or $x context (py:None))))
      ("__cause__" (m:val (pyrt:field-or $x cause (py:None))))
      ("__suppress_context__" (m:val (pyrt:field-or $x suppress False)))
      ("__class__" (m:val (pyrt:field $x class)))
      ("value" (if (pyrt:exc-is $x StopIteration)
                   (m:val (let $args (cdr-atom (pyrt:field $x args)) (if (== (size-atom $args) 0) (py:None) (car-atom $args))))
                   (pyrt:no-attribute $x $name)))
      ("code" (if (pyrt:exc-is $x SystemExit) (m:val (pyrt:exit-code $x)) (pyrt:no-attribute $x $name)))
      ("name" (pyrt:exc-field-attribute $x name (if (pyrt:exc-is $x NameError) true (if (pyrt:exc-is $x AttributeError) true (pyrt:exc-is $x ImportError))) $name))
      ("obj" (pyrt:exc-field-attribute $x obj (pyrt:exc-is $x AttributeError) $name))
      ("path" (pyrt:exc-field-attribute $x path (pyrt:exc-is $x ImportError) $name))
      ($other (pyrt:no-attribute $x $name)))))
(= (pyrt:exc-field-attribute $x $field $has $name)
   (if $has (m:val (pyrt:field-or $x $field (py:None))) (pyrt:no-attribute $x $name)))

; del o[k]: a list's item or slice, a dict's key
(= (pyrt:apply2 del-item $o $k)
   (if (pyrt:needs-python $o) (pyrt:prelude-call "_delitem" (cons-atom $o (cons-atom $k ()))) (pyrt:del-item-builtin $o $k)))
(= (pyrt:del-item-builtin $o $k)
   (case $o
     (((py:list $id) (pyrt:list-del $o $k))
      ((py:dict $id)
       (case (pyrt:dict-key $k)
         (((m:val $ck) (if (pyrt:dict-delete $o $ck) (m:val (py:None)) (m:err (pyrt:exc-args "KeyError" (cons-atom $k ())))))
          ($e $e))))
      ($other (m:err (pyrt:exc "TypeError" (str:concat (pyrt:quoted (pyrt:type-name $o)) " object does not support item deletion")))))))
; del l[i] (list_ass_subscript): the items after it move down
(= (pyrt:list-del $l $k)
   (if (pyrt:is-slice $k) (pyrt:list-delete-slice $l $k)
     (case (pyrt:seq-index $k (pyrt:list-len $l) "list assignment index out of range" "list")
       (((m:val $j) (let $r (pyrt:list-splice $l $j (+ $j 1) ()) (m:val (py:None))))
        ($e $e)))))
; del NAME: a fast local or a cell made unbound, a namespace's or the globals'
; entry removed; a name unbound already is an error, in the words of a read
(= (pyrt:delete $t (m:frame $l $g $ns $c))
   (case $t
     (((c:local $i)
       (case (pyrt:local-value (index-atom $l $i) $i $c)
         (((m:val $v) (m:val (m:frame (pyrt:tuple-set $l $i (m:unbound)) $g $ns $c))) ($e $e))))
      ((c:cell $i)
       (case (pyrt:cell-value (index-atom $l $i) $i $c)
         (((m:val $v) (let $u (pyrt:cell-set (index-atom $l $i) (m:unbound)) (m:val (m:frame $l $g $ns $c)))) ($e $e))))
      ((c:name $n)
       (case $ns
         (((py:dict $di) (if (pyrt:dict-delete $ns $n) (m:val (m:frame $l $g $ns $c)) (m:err (pyrt:name-error $n (str:concat (str:concat "name " (pyrt:quoted $n)) " is not defined")))))
          ; DELETE_NAME from any other namespace: PyObject_DelItem, run by the machine
          ($other (m:via (pyrt:prelude-call-op "_delete_name" (LCons (c:const $ns) (LCons (c:const $n) (LNil))))
                         (m:frame $l $g $ns $c))))))
      ((c:global $n) (if (pyrt:dict-delete $g $n) (m:val (m:frame $l $g $ns $c)) (m:err (pyrt:name-error $n (str:concat (str:concat "name " (pyrt:quoted $n)) " is not defined"))))))))

; ---------------------------------------------------------------- the prelude

; lib/prelude.py (compiled by the core stage at build time) runs before every
; program in globals of its own, where the intrinsics are bound; its names
; without a leading underscore, and its dunder names, join the builtins
!(bind! &py-prelude (new-state (m:none)))
(= (pyrt:start-prelude $code)
   (let $p (pyrt:dict-new)
     (let $u1 (pyrt:dict-set $p "__name__" "builtins")
       (let $u2 (pyrt:bind-intrinsics $p (pyrt:intrinsic-names))
         (let $u3 (change-state! &py-prelude $p)
           (case (pyrt:refile $code (c:builtin))
             (((c:code $nm $q $a $ns $cs $fs $fl $body $cofile)
               (pyrt:run-to-value $body (m:frame (pyrt:unbound-n $ns) $p $p (c:code $nm $q $a $ns $cs $fs $fl $body $cofile)))))))))))
(= (pyrt:bind-intrinsics $p $names)
   (if (== (size-atom $names) 0) ()
     (let ($n $rest) (decons-atom $names)
       (let $u (pyrt:dict-set $p $n (py:builtin (str:string->symbol $n))) (pyrt:bind-intrinsics $p $rest)))))
(= (pyrt:prelude-export $n)
   (let $p (get-state &py-prelude)
     (if (== $p (m:none)) (m:missing)
       (if (if (str:starts-with $n "__") true (not (str:starts-with $n "_"))) (pyrt:dict-get $p $n) (m:missing)))))
(= (pyrt:prelude $n) (pyrt:dict-get (get-state &py-prelude) $n))
; a prelude function called by an operation (the machine runs it), or from
; the host to its value
(= (pyrt:prelude-call $n $xs) (pyrt:call-kw (pyrt:prelude $n) $xs ()))
(= (pyrt:prelude-sync $n $xs) (pyrt:call-sync (pyrt:prelude $n) $xs ()))

(= (pyrt:intrinsic-names)
   ("_cetta_null" "_cetta_type_field" "_cetta_type_dict" "_cetta_set_type_attr" "_cetta_del_type_attr" "_cetta_mro_lookup"
    "_cetta_install_builtin_members" "_cetta_bmeth_get" "_cetta_bmeth_info" "_cetta_base" "_cetta_compile" "_cetta_run_code" "_cetta_str_case" "_cetta_str_props_all" "_cetta_str_props_any" "_cetta_code_props" "_cetta_str_trim_props" "_cetta_str_trim_chars" "_cetta_str_split_ws" "_cetta_str_rsplit_ws" "_cetta_str_split" "_cetta_str_find" "_cetta_str_rfind" "_cetta_str_is_ascii" "_cetta_fs_read" "_cetta_fs_write" "_cetta_fs_append" "_cetta_fs_exists" "_cetta_read_line" "_cetta_std_write" "_cetta_str_byte_length" "_cetta_bytes_view" "_cetta_bytes" "_cetta_utf8_bytes" "_cetta_bytes_utf8" "_cetta_ascii_case" "_cetta_slice_indices" "_cetta_buffer_view" "_cetta_items_of_view" "_cetta_bytearray_iter" "_cetta_is_seq_slot" "_cetta_bytes_contains" "_cetta_slot_value" "_cetta_clear_field"
    "_cetta_new_type" "_cetta_run_class_body" "_cetta_cell_get" "_cetta_cell_set" "_cetta_new_object"
    "_cetta_object_dict" "_cetta_attribute_error" "_cetta_address" "_cetta_identity_hash" "_cetta_bind"
    "_cetta_getset" "_cetta_getset_get" "_cetta_getset_set" "_cetta_type_of" "_cetta_field" "_cetta_set_field"
    "_cetta_getattr" "_cetta_setattr" "_cetta_delattr" "_cetta_repr" "_cetta_type_module"
    "_cetta_exception_new" "_cetta_exception_str" "_cetta_exception_repr"
    "_cetta_is_python_object" "_cetta_native_truth" "_cetta_sequence_iterator" "_cetta_native_next"
    "_cetta_gen_send" "_cetta_gen_throw" "_cetta_gen_close" "_cetta_float_format"
    "_cetta_compile_file" "_cetta_exec_module" "_cetta_file_exists" "_cetta_builtin_type" "_cetta_builtin"
    "_cetta_handled" "_cetta_directory_of" "_cetta_attribute_error_text"
    "_cetta_libm" "_cetta_libm2" "_cetta_libm3" "_cetta_frexp" "_cetta_ldexp" "_cetta_modf" "_cetta_lib_module" "_cetta_code_info" "_cetta_frame" "_cetta_suggestion" "_cetta_new_lazy" "_cetta_prelude" "_cetta_repr_enter" "_cetta_repr_leave" "_cetta_parse" "_cetta_compile_ast" "_cetta_has_field" "_cetta_structseq_layouts"
    "_cetta_isinstance" "_cetta_issubclass"
    "_cetta_int_bit_length" "_cetta_int_bit_count" "_cetta_int_pow_mod" "_cetta_int_digits"
    "_cetta_frozenset_of" "_cetta_set_add" "_cetta_set_discard" "_cetta_set_contains" "_cetta_set_merge"
    "_cetta_set_update" "_cetta_set_pop" "_cetta_set_clear" "_cetta_set_table" "_cetta_set_resize"
    "_cetta_set_swap_bodies" "_cetta_is_iterator" "_cetta_gen_step" "_cetta_gen_finished" "_cetta_callable" "_cetta_id" "_cetta_exc_line" "_cetta_exc_type_display"))
(= (pyrt:is-intrinsic $b) (str:starts-with (str:symbol->string $b) "_cetta_"))
; the runtime's operations the prelude's protocols are made of
(= (pyrt:intrinsic $b $xs)
   (case (intr $b $xs)
     (((intr _cetta_null ()) (m:val (py:null)))
      ((intr _cetta_type_field ($t $f)) (pyrt:type-field $t $f))
      ((intr _cetta_type_dict ($t)) (m:val (pyrt:type-dict $t)))
      ((intr _cetta_set_type_attr ($t $n $v)) (let $u (pyrt:type-dict-set $t $n $v) (m:val (py:None))))
      ((intr _cetta_install_builtin_members ()) (let $u (pyrt:install-builtin-members) (m:val (py:None))))
      ((intr _cetta_bmeth_get ($d $obj $tp)) (case $d (((py:bmeth $bt $bn) (pyrt:bmeth-get $bt $bn $obj $tp)) ($other (m:val $d)))))
      ((intr _cetta_bmeth_info ($d $what)) (pyrt:bmeth-info $d $what))
      ((intr _cetta_del_type_attr ($t $n)) (m:val (if (pyrt:type-dict-delete $t $n) True False)))
      ((intr _cetta_mro_lookup ($t $n)) (m:val (let $v (pyrt:mro-lookup $t $n) (if (== $v (m:missing)) (py:null) $v))))
      ((intr _cetta_new_type ($meta $name $qualname $bases $d $tail)) (m:val (pyrt:new-type $meta $name $qualname $bases $d $tail)))
      ((intr _cetta_run_class_body ($f $ns)) (pyrt:run-class-body $f $ns))
      ((intr _cetta_cell_get ($cell)) (m:val (let $v (pyrt:heap-one (h:cell (pyrt:obj-cell-id $cell) $x) $x) (if (== $v (m:unbound)) (py:null) $v))))
      ((intr _cetta_cell_set ($cell $v)) (let $u (pyrt:cell-set $cell $v) (m:val (py:None))))
      ((intr _cetta_new_object ($cls)) (m:val (pyrt:new-object $cls)))
      ((intr _cetta_object_dict ($o)) (m:val (pyrt:object-dict $o)))
      ((intr _cetta_attribute_error ($o $n)) (m:val (pyrt:attribute-error $o $n)))
      ((intr _cetta_address ($o)) (m:val (pyrt:address $o)))
      ((intr _cetta_identity_hash ($o)) (m:val (pyrt:identity-hash $o)))
      ((intr _cetta_bind ($f $o)) (m:val (py:bound (pyrt:new-id) $f $o)))
      ((intr _cetta_getset ($n)) (m:val (py:getset $n)))
      ((intr _cetta_getset_get ($d $o)) (pyrt:getset-get $d $o))
      ((intr _cetta_gen_send ($g $v)) (pyrt:gen-method-result (pyrt:gen-send $g $v)))
      ((intr _cetta_gen_throw ($g $e)) (pyrt:gen-method-result (pyrt:gen-throw $g $e)))
      ((intr _cetta_gen_close ($g)) (pyrt:gen-close $g))
      ((intr _cetta_float_format ($x $c $p)) (m:val (float:format $x (str:string->symbol $c) $p)))
      ((intr _cetta_compile_file ($path)) (pyrt:compile-file $path))
      ((intr _cetta_exec_module ($code $d)) (pyrt:exec-module $code $d))
      ((intr _cetta_compile ($text $file $mode)) (pyrt:compile-source $text $file $mode))
      ((intr _cetta_parse ($text $file $mode)) (pyrt:parse-source $text $mode))
      ((intr _cetta_compile_ast ($enc $file $mode)) (pyrt:compile-ast $enc $file))
      ; the string library's Unicode operations, for the prelude's str methods
      ((intr _cetta_str_case ($k $s)) (m:val (str:case-map $k $s)))
      ((intr _cetta_str_props_all ($m $s)) (m:val (if (str:props-all $m $s) True False)))
      ((intr _cetta_str_props_any ($m $s)) (m:val (if (str:props-any $m $s) True False)))
      ((intr _cetta_code_props ($c)) (m:val (str:code-props $c)))
      ((intr _cetta_str_trim_props ($m $side $s)) (m:val (str:trim-props $m $side $s)))
      ((intr _cetta_str_trim_chars ($cs $side $s)) (m:val (str:trim-chars $cs $side $s)))
      ((intr _cetta_str_split_ws ($n $s)) (m:val (pyrt:list-new (pyrt:seq-copy (str:split-props (pyrt:space-mask) $n $s)))))
      ((intr _cetta_str_rsplit_ws ($n $s)) (m:val (pyrt:list-new (pyrt:seq-copy (str:rsplit-props (pyrt:space-mask) $n $s)))))
      ((intr _cetta_str_split ($sep $s)) (m:val (pyrt:list-new (pyrt:split-on $sep $s))))
      ((intr _cetta_str_find ($sub $s)) (m:val (str:find-index $sub $s)))
      ((intr _cetta_str_rfind ($sub $s)) (m:val (str:rfind-index $sub $s)))
      ((intr _cetta_str_is_ascii ($s)) (m:val (if (== (str:byte-length $s) (str:codepoint-count $s)) True False)))
      ; the files and the standard streams, for the prelude's io
      ((intr _cetta_fs_read ($p)) (m:val (pyrt:fs-outcome (fs:read-text $p))))
      ((intr _cetta_fs_write ($p $t)) (m:val (pyrt:fs-outcome (fs:write $p $t))))
      ((intr _cetta_fs_append ($p $t)) (m:val (pyrt:fs-outcome (fs:append-text $p $t))))
      ((intr _cetta_fs_exists ($p)) (m:val (if (fs:exists $p) True False)))
      ((intr _cetta_read_line ()) (m:val (system:read-line)))
      ((intr _cetta_std_write ($stream $t)) (let $w (system:write (if (== $stream "stderr") stderr stdout) $t) (m:val (py:None))))
      ((intr _cetta_str_byte_length ($s)) (m:val (str:byte-length $s)))
      ; bytes: (py:Bytes VIEW), VIEW one code point 0..255 per byte
      ((intr _cetta_bytes_view ($by)) (m:val (case $by (((py:Bytes $v) $v) ($o (py:null))))))
      ((intr _cetta_bytes ($v)) (m:val (py:Bytes $v)))
      ((intr _cetta_utf8_bytes ($s)) (m:val (py:Bytes (str:from-codepoints (str:byte-values $s)))))
      ((intr _cetta_bytes_utf8 ($v)) (m:val (pyrt:utf8-of-view $v)))
      ((intr _cetta_ascii_case ($v $how)) (m:val (if (== $how "upper") (pyrt:map-ascii $v 97 122 -32) (pyrt:map-ascii $v 65 90 32))))
      ((intr _cetta_run_code ($co $g $l)) (pyrt:run-code $co $g $l))
      ((intr _cetta_file_exists ($path)) (m:val (if (fs:exists $path) True False)))
      ((intr _cetta_builtin_type ($n)) (m:val (py:type (str:string->symbol $n))))
      ((intr _cetta_builtin ($n)) (let $bv (pyrt:builtin-of $n) (if (== $bv (m:missing)) (m:val (py:null)) (m:val $bv))))
      ((intr _cetta_handled ()) (let $h (pyrt:handled-top) (m:val (if (== $h (m:none)) (py:None) $h))))
      ((intr _cetta_directory_of ($path)) (m:val (pyrt:directory-of $path)))
      ((intr _cetta_attribute_error_text ($m $o $n)) (m:val (pyrt:attribute-error-text $m $o $n)))
      ((intr _cetta_libm ($name $x)) (m:val (pyrt:libm1 $name $x)))
      ((intr _cetta_libm2 ($name $x $y)) (m:val (pyrt:libm2 $name $x $y)))
      ((intr _cetta_libm3 ($name $x $y $z)) (m:val (float:fma $x $y $z)))
      ((intr _cetta_frexp ($x)) (case (float:frexp $x) (((FloatFrexp $m $e) (m:val (py:tuple $m $e))))))
      ((intr _cetta_ldexp ($x $n)) (m:val (float:ldexp $x $n)))
      ((intr _cetta_modf ($x)) (case (float:modf $x) (((FloatModf $f $w) (m:val (py:tuple $f $w))))))
      ((intr _cetta_lib_module ($n)) (m:val (pyrt:lib-module-code $n)))
      ((intr _cetta_code_info ($co)) (m:val (pyrt:code-info (pyrt:field $co core))))
      ((intr _cetta_suggestion ($n $l)) (m:val (let $s (pyrt:suggestion $n (pyrt:list-items $l)) (if (== $s (m:missing)) (py:None) $s))))
      ; sys._getframe: a question for the machine, which knows the frames
      ((intr _cetta_frame ($d)) (m:want-frame $d))
      ; a lazy import's placeholder (lazy_import): an object whose id carries
      ; the mark, so a read can tell one by its shape
      ((intr _cetta_new_lazy ()) (m:val (pyrt:lazy-new)))
      ; a held module standing for a C module reaches the runtime's own
      ; helpers (Argument Clinic's reading of arguments, say) as C reaches its API
      ((intr _cetta_prelude ($n)) (m:val (pyrt:prelude $n)))
      ((intr _cetta_structseq_layouts ()) (m:val (pyrt:structseq-layouts)))
      ; Py_ReprEnter (True when X's repr is already being made) and Py_ReprLeave
      ((intr _cetta_repr_enter ($x)) (m:val (if (pyrt:repr-enter $x) True False)))
      ((intr _cetta_repr_leave ($x)) (m:val (pyrt:repr-leave $x (py:None))))
      ; the C-level type checks the runtime's own Python code makes
      ; (PyObject_TypeCheck, PyType_IsSubtype): a class's MRO, never its
      ; __instancecheck__ or __subclasscheck__
      ((intr _cetta_isinstance ($x $t)) (pyrt:class-check (pyrt:type-of $x) $t "isinstance() arg 2 must be a type, a tuple of types, or a union"))
      ((intr _cetta_issubclass ($c $t))
       (case $c
         (((py:type $n) (pyrt:class-check $c $t "issubclass() arg 2 must be a class, a tuple of classes, or a union"))
          ($other (m:err (pyrt:exc "TypeError" "issubclass() arg 1 must be a class"))))))
      ((intr _cetta_int_bit_length ($x)) (m:val (integer:bit-length (pyrt:int-of $x))))
      ((intr _cetta_int_bit_count ($x)) (m:val (integer:bit-count (pyrt:int-of $x))))
      ((intr _cetta_int_pow_mod ($base $ex $mo)) (pyrt:pow-mod (pyrt:int-of $base) (pyrt:int-of $ex) (pyrt:int-of $mo)))
      ((intr _cetta_int_digits ($x $base)) (m:val (integer:digits (pyrt:int-of $x) $base)))
      ; the set table (sets and frozensets alike; a frozenset only while it is built)
      ((intr _cetta_frozenset_of ($s)) (m:val (pyrt:freeze $s)))
      ; a builtin value as it is; an instance of a builtin type's subclass, its
      ; builtin part (the data the builtin type's functions read)
      ((intr _cetta_base ($x)) (m:val (pyrt:base-of $x)))
      ((intr _cetta_is_iterator ($x)) (m:val (if (pyrt:is-iterator $x) True False)))
      ((intr _cetta_callable ($x)) (m:val (if (pyrt:is-callable $x) True False)))
      ((intr _cetta_id ($x)) (m:val (pyrt:id-of $x)))
      ((intr _cetta_exc_line ($x)) (m:val (pyrt:exc-line $x)))
      ((intr _cetta_exc_type_display ($t)) (m:val (pyrt:exc-type-display $t)))
      ((intr _cetta_gen_step ($g $how $v)) (pyrt:gen-step $g $how $v))
      ((intr _cetta_gen_finished ($g)) (m:val (if (== (pyrt:field $g state) finished) True False)))
      ((intr _cetta_set_add ($s $x)) (case (pyrt:set-key $x) (((m:val $k) (let $u (pyrt:set-add $s $k $x) (m:val (py:None)))) ($e $e))))
      ((intr _cetta_set_discard ($s $x)) (case (pyrt:set-key $x) (((m:val $k) (m:val (if (pyrt:set-discard $s $k) True False))) ($e $e))))
      ((intr _cetta_set_contains ($s $x))
       (case (pyrt:set-key $x) (((m:val $k) (m:val (if (== (pyrt:set-entry $s $k) (m:missing)) False True))) ($e $e))))
      ((intr _cetta_set_merge ($s $o)) (let $u (pyrt:set-merge $s $o) (m:val (py:None))))
      ((intr _cetta_set_update ($s $x)) (case (pyrt:set-update $s $x) (((m:val $t) (m:val (py:None))) ($e $e))))
      ((intr _cetta_set_pop ($s)) (pyrt:set-pop $s))
      ((intr _cetta_set_clear ($s)) (let $u (pyrt:set-clear $s) (m:val (py:None))))
      ((intr _cetta_set_table ($s)) (let (h:stab $id $mk $fl $us $fg) (pyrt:set-table (pyrt:set-id $s)) (m:val (py:tuple $mk $fl $us))))
      ((intr _cetta_set_resize ($s $n)) (let $u (pyrt:set-resize (pyrt:set-id $s) $n) (m:val (py:None))))
      ((intr _cetta_set_swap_bodies ($p $q)) (let $u (pyrt:set-swap-bodies (pyrt:set-id $p) (pyrt:set-id $q)) (m:val (py:None))))
      ((intr _cetta_getset_set ($d $o $v)) (pyrt:getset-set $d $o $v))
      ((intr _cetta_type_of ($x)) (m:val (pyrt:type-of $x)))
      ((intr _cetta_field ($o $f)) (m:val (pyrt:value-field $o (str:string->symbol $f))))
      ((intr _cetta_has_field ($o $f)) (m:val (if (== (pyrt:field-or $o (str:string->symbol $f) (m:missing)) (m:missing)) False True)))
      ((intr _cetta_slice_indices ($sl $len)) (pyrt:slice-indices-of $sl $len))
      ((intr _cetta_buffer_view ($bx)) (m:val (let $w (pyrt:bytes-view-of $bx) (if (== $w (m:missing)) (py:null) $w))))
      ((intr _cetta_items_of_view ($v)) (m:val (pyrt:list-new (pyrt:seq-copy (str:codepoint-values $v)))))
      ((intr _cetta_bytearray_iter ($l)) (pyrt:iter-new (it:bytearray $l 0)))
      ((intr _cetta_is_seq_slot ($f)) (m:val (if (pyrt:seq-slot $f) True False)))
      ((intr _cetta_bytes_contains ($bv $bx)) (pyrt:bytes-contains $bv $bx))
      ((intr _cetta_set_field ($o $f $v)) (let $u (pyrt:set-field $o (str:string->symbol $f) $v) (m:val (py:None))))
      ((intr _cetta_slot_value ($o $f)) (m:val (let $sv (pyrt:field $o (str:string->symbol $f)) (if (== $sv (m:missing)) (py:null) $sv))))
      ((intr _cetta_clear_field ($o $f)) (let $u (pyrt:clear-field $o (str:string->symbol $f)) (m:val (py:None))))
      ((intr _cetta_getattr ($o $n)) (pyrt:getattr $o $n))
      ((intr _cetta_setattr ($o $n $v)) (pyrt:setattr $o $n $v))
      ((intr _cetta_delattr ($o $n)) (pyrt:delattr $o $n))
      ((intr _cetta_repr ($x)) (pyrt:repr-of $x))
      ((intr _cetta_type_module ($t)) (m:val (pyrt:module-of-type $t)))
      ((intr _cetta_exception_new ($cls $args)) (m:val (pyrt:exc-new $cls (cdr-atom $args))))
      ((intr _cetta_exception_str ($x)) (m:val (pyrt:exc-str $x)))
      ((intr _cetta_exception_repr ($x)) (m:val (pyrt:exc-repr $x)))
      ((intr _cetta_is_python_object ($x)) (m:val (if (pyrt:needs-python $x) True False)))
      ((intr _cetta_native_truth ($x)) (m:val (if (pyrt:truthy $x) True False)))
      ((intr _cetta_sequence_iterator ($x)) (pyrt:iter-new (it:seqproto $x 0)))
      ((intr _cetta_native_next ($it))
       (case (pyrt:next-of $it) (((m:val (m:exhausted)) (m:err (pyrt:exc-args "StopIteration" ()))) ($r $r)))))))
(= (pyrt:obj-cell-id (py:cell $id)) $id)
; super() without arguments: CPython's super_init_without_args, from the
; calling frame: its first argument (a cell's content when captured) and its
; __class__ free variable
(= (pyrt:zero-arg-super (m:frame $l $g $ns (c:code $nm $q (c:args $npo $np $nk $st $ds $names) $nsl $cells $frees $fl $body $cofile)))
   (if (== (+ $npo $np) 0) (m:err (pyrt:exc "RuntimeError" "super(): no arguments"))
     (let $ci (pyrt:free-slot-named $frees $names "__class__")
       (if (== $ci -1) (m:err (pyrt:exc "RuntimeError" "super(): __class__ cell not found"))
         (let $cls (pyrt:heap-one (h:cell (pyrt:obj-cell-id (index-atom $l $ci)) $x) $x)
           (if (== $cls (m:unbound)) (m:err (pyrt:exc "RuntimeError" "super(): __class__ cell is empty"))
             (let $first (pyrt:first-argument (index-atom $l 0) $cells)
               (if (== $first (m:unbound)) (m:err (pyrt:exc "RuntimeError" "super(): arg[0] deleted"))
                 (pyrt:call-kw (py:type super) (cons-atom $cls (cons-atom $first ())) ())))))))))
(= (pyrt:free-slot-named $frees $names $n)
   (case $frees
     (((LNil) -1)
      ((LCons $i $rest) (if (== (pyrt:nth-name $names $i) $n) $i (pyrt:free-slot-named $rest $names $n))))))
(= (pyrt:first-argument $v $cells)
   (if (pyrt:in-lcons 0 $cells) (pyrt:heap-one (h:cell (pyrt:obj-cell-id $v) $x) $x) $v))
; a class's facts as Python values
(= (pyrt:type-field $t $f)
   (case $f
     (("name" (m:val (pyrt:type-name-of $t)))
      ("tp_name" (m:val (pyrt:tp-name-of $t)))
      ("qualname" (m:val (pyrt:qualname-of $t)))
      ("module" (m:val (pyrt:module-of-type $t)))
      ("mro" (m:val (cons-atom py:tuple (pyrt:mro-of $t))))
      ("bases" (m:val (cons-atom py:tuple (pyrt:bases-of $t))))
      ("subclassable" (m:val (pyrt:subclassable $t)))
      ("has_dict" (m:val (pyrt:class-has-dict $t)))
      ("has_weakref" (m:val (pyrt:class-has-weakref $t)))
      ("varsize" (m:val (pyrt:class-varsize $t)))
      ("solid" (m:val (pyrt:type-solid $t)))
      ("base" (m:val (pyrt:type-base $t)))
      ("heap" (m:val (case $t (((py:type $id) (not (pyrt:builtin-class $id)))))))
      ("noinstances" (m:val (if (pyrt:type-no-instances $t) True False)))
      ($other (m:err (pyrt:exc "SystemError" (str:concat "no class fact " $f)))))))
; a builtin type whose instances calling it does not make (tp_new NULL)
(= (pyrt:no-instances $b)
   (if (pyrt:builtin-class $b) (not (== (collapse (match &self (pyrt:bnoinstances $b) $b)) ())) False))
(= (pyrt:type-no-instances $c) (case $c (((py:type $id) (pyrt:no-instances $id)) ($other False))))
; the struct sequences' layouts (builtin_types.metta), for the prelude: a
; tuple of (TYPE, VISIBLE, UNNAMED, ((NAME, DOC), ...))
(= (pyrt:structseq-layouts)
   (let $rows (collapse (match &self (pyrt:bstructseq $t $v $u $fs) (ss-row $t $v $u $fs)))
     (cons-atom py:tuple (pyrt:structseq-rows $rows))))
(= (pyrt:structseq-rows $rows)
   (if (== $rows ()) ()
     (let ((ss-row $t $v $u $fs) $rest) (decons-atom $rows)
       (cons-atom (py:tuple (py:type $t) $v $u (cons-atom py:tuple (pyrt:structseq-fields $fs)))
                  (pyrt:structseq-rows $rest)))))
(= (pyrt:structseq-fields $fs)
   (if (== $fs ()) ()
     (let (($n $d) $rest) (decons-atom $fs) (cons-atom (py:tuple $n $d) (pyrt:structseq-fields $rest)))))
(= (pyrt:new-type $meta $name $qualname $bases $d $tail)
   (let $id (pyrt:new-id)
     (let $t (py:type $id)
       (let $u1 (pyrt:set-field $t meta $meta)
         (let $u2 (pyrt:set-field $t name $name)
           (let $u3 (pyrt:set-field $t qualname $qualname)
             (let $u4 (pyrt:set-field $t bases $bases)
               (let $u5 (pyrt:set-field $t dict $d)
                 (let $u6 (pyrt:set-field $t mro (cons-atom py:tuple (cons-atom $t (cdr-atom $tail))))
                   $t)))))))))
; a class's namespace, read only (type_dict): a new mappingproxy over it
(= (pyrt:mappingproxy-of $d)
   (let $p (pyrt:new-object (py:type mappingproxy)) (let $u (pyrt:set-field $p mapping $d) $p)))
; __module__ and __doc__ of a class live in its namespace (type_set_module,
; type_set_doc); a builtin type's cannot be set (check_set_special_type_attr)
(= (pyrt:set-type-special (py:type $t) $name $v)
   (if (pyrt:builtin-class $t)
       (m:err (pyrt:exc "TypeError" (str:concat (str:concat (str:concat "cannot set " (pyrt:quoted $name)) " attribute of immutable type ")
                                                (pyrt:quoted (pyrt:tp-name-of (py:type $t))))))
       (let $u (pyrt:type-dict-set (py:type $t) $name $v) (m:val (py:None)))))
; a class body: its function's code run with the namespace as its names
(= (pyrt:run-class-body $f $ns)
   (case (pyrt:call-kw $f () ())
     (((m:call (c:code $nm $q $a $n $cs $fs $fl $body $cofile) (m:frame $l $g $ns0 $code))
       (pyrt:run-to-value $body (m:frame $l $g $ns $code)))
      ($other $other))))
(= (pyrt:new-object $cls)
   (let $o (pyrt:obj-new $cls)
     (let $u (if (pyrt:class-has-dict $cls) (pyrt:set-field $o dict (pyrt:dict-new)) ()) $o)))
(= (pyrt:object-dict $o)
   (case $o
     (((py:obj $id)
       (let $d (pyrt:field-or $o dict (py:None))
         (if (== $d (py:None))
             (if (pyrt:builtin-instance-has-dict $o) (let $n (pyrt:dict-new) (let $u (pyrt:set-field $o dict $n) $n)) (py:None))
             $d)))
      ((py:func $id) (let $d (pyrt:field $o dict) (if (== $d (m:missing)) (let $n (pyrt:dict-new) (let $u (pyrt:set-field $o dict $n) $n)) $d)))
      ($other (py:None)))))
; an instance of a builtin class whose instances have a __dict__ (its
; tp_dictoffset: BaseException and its subclasses, ...) is given one when it
; is first asked for
(= (pyrt:builtin-instance-has-dict $o)
   (case (pyrt:field $o class)
     (((py:type $t)
       (if (pyrt:builtin-class $t)
           (case (pyrt:btype $t) (((bt $nm $md $bs $mro $sub $hd) (== $hd True)) ($x false)))
           false))
      ($y false))))
; the AttributeError of a missing attribute, in CPython's words, its name and
; object kept (for the suggestions of a traceback)
(= (pyrt:attribute-error-text $msg $o $n)
   (let $e (pyrt:exc-new (py:type AttributeError) (cons-atom $msg ()))
     (let $u1 (pyrt:set-field $e name $n) (let $u2 (pyrt:set-field $e obj $o) $e))))
(= (pyrt:attribute-error $o $n)
   (let $e (pyrt:exc-new (py:type AttributeError)
             (cons-atom (case $o
                          (((py:type $t) (str:concat (str:concat (str:concat "type object " (pyrt:quoted (pyrt:tp-name-of $o))) " has no attribute ") (pyrt:quoted $n)))
                           ($other (str:concat (str:concat (pyrt:quoted (pyrt:type-name $o)) " object has no attribute ") (pyrt:quoted $n)))))
                        ()))
     (let $u1 (pyrt:set-field $e name $n) (let $u2 (pyrt:set-field $e obj $o) $e))))
; an object's address, as CPython's default repr shows one (the checker of
; the runs compares them as equal)
; an object's address in its repr: the number id() gives it
(= (pyrt:address $o) (str:concat "0x" (pyrt:hex (pyrt:id-of $o))))
; id(x): an object's address, as its default repr shows it; a value (an int,
; a str, a tuple, a builtin type, ...) is given a number of its own the first
; time one is asked for, so equal values share one (as is compares them)
; object.__hash__: a number of the object's identity (a builtin class's is
; the number id() gives it)
(= (pyrt:identity-hash $o)
   (case $o (((py:type $t) (if (pyrt:builtin-class $t) (pyrt:id-of $o) $t))
             ((py:obj (lazy $n)) $n)
             ($other (pyrt:obj-id $o)))))
(= (pyrt:id-of $x)
   (let $i (case $x (((py:obj $k) $k) ((py:type $k) $k) ((py:func $k) $k) ((py:gen $k) $k) ((py:coro $k) $k) ((py:agen $k) $k) ($other (m:missing))))
     (if (pyrt:is-int $i) (+ 139637976727552 (* 64 $i))
       (let $known (pyrt:heap-one (h:vid $x $n) $n)
         (if (== $known (m:missing))
             (let $n2 (pyrt:new-id) (let $a (add-atom &py-heap (h:vid $x $n2)) (+ 139637976727552 (* 64 $n2))))
             (+ 139637976727552 (* 64 $known)))))))
(= (pyrt:hex $n) (if (< $n 16) (pyrt:hex-digit $n) (str:concat (pyrt:hex (pyrt:floor-div $n 16)) (pyrt:hex-digit (% $n 16)))))
; a getset descriptor's value for an object, and setting it
(= (pyrt:getset-get (py:getset $n) $o)
   (case $n
     (("__dict__" (case $o (((py:type $t) (m:val (pyrt:mappingproxy-of (pyrt:type-dict $o)))) ($other (m:val (pyrt:object-dict $o))))))
      ("__class__" (m:val (pyrt:type-of $o)))
      ("__name__" (m:val (case $o (((py:type $t) (pyrt:type-name-of $o)) ($other (pyrt:field-or $o name (py:None)))))))
      ("__qualname__" (m:val (case $o (((py:type $t) (pyrt:qualname-of $o)) ($other (pyrt:field-or $o qualname (py:None)))))))
      ("__module__" (m:val (case $o (((py:type $t) (pyrt:module-of-type $o)) ($other (pyrt:field-or $o module (py:None)))))))
      ("__mro__" (m:val (cons-atom py:tuple (pyrt:mro-of $o))))
      ("__bases__" (m:val (cons-atom py:tuple (pyrt:bases-of $o))))
      ("__base__" (m:val (pyrt:type-base $o)))
      ("__doc__" (m:val (case $o (((py:type $t) (if (pyrt:builtin-class $t) (py:None) (let $v (pyrt:dict-get (pyrt:type-dict $o) "__doc__") (if (== $v (m:missing)) (py:None) $v))))
                                  ($other (pyrt:field-or $o doc (py:None)))))))
      ("__weakref__" (m:val (py:None)))
      ; a class's from its namespace (type_get_type_params), a function's its own
      ("__type_params__" (m:val (case $o (((py:type $t) (if (pyrt:builtin-class $t) (py:tuple)
                                                            (let $v (pyrt:dict-get (pyrt:type-dict $o) "__type_params__")
                                                              (if (== $v (m:missing)) (py:tuple) $v))))
                                          ($other (pyrt:field-or $o type_params (py:tuple)))))))
      ; a number's parts: an int is its own numerator, over 1; a real number its own real part, 0 its imaginary
      ("real" (m:val (if (pyrt:is-float $o) $o (pyrt:int-of $o))))
      ("imag" (m:val (if (pyrt:is-float $o) 0.0 0)))
      ("numerator" (m:val (pyrt:int-of $o)))
      ("denominator" (m:val 1))
      ("gi_running" (m:val (if (== (pyrt:field $o state) running) True False)))
      ("gi_suspended" (m:val (if (== (pyrt:field $o state) suspended) True False)))
      ("gi_yieldfrom" (m:val (if (== (pyrt:field $o state) suspended) (case (pyrt:field $o where) (((c:yf $i $t) $i) ($w (py:None)))) (py:None))))
      ("cr_running" (m:val (if (== (pyrt:field $o state) running) True False)))
      ("cr_suspended" (m:val (if (== (pyrt:field $o state) suspended) True False)))
      ("ag_running" (m:val (if (== (pyrt:field-or $o agrun False) True) True False)))
      ("ag_suspended" (m:val (if (== (pyrt:field $o state) suspended) True False)))
      ("ag_await" (m:val (if (== (pyrt:field $o state) suspended) (case (pyrt:field $o where) (((c:yf $i $t) $i) ($w (py:None)))) (py:None))))
      ("cr_await" (m:val (if (== (pyrt:field $o state) suspended) (case (pyrt:field $o where) (((c:yf $i $t) $i) ($w (py:None)))) (py:None))))
      ("__annotations__" (case $o (((py:type $t) (pyrt:type-annotations $o)) ((py:obj $i) (pyrt:module-annotations $o)) ($other (pyrt:function-annotations $o)))))
      ("__annotate__" (case $o (((py:type $t) (pyrt:type-annotate $o)) ((py:obj $i) (pyrt:module-annotate $o)) ($other (pyrt:function-annotate $o)))))
      ("cell_contents" (pyrt:cell-contents $o))
      ; a traceback's entry (tb_frame, tb_lineno, tb_next; no instruction offsets
      ; here: tb_lasti -1) and a frame's view (PyFrameObject's getters)
      ("tb_frame" (m:val (pyrt:field $o frame)))
      ("tb_lineno" (m:val (pyrt:field $o lineno)))
      ("tb_next" (m:val (pyrt:field-or $o next (py:None))))
      ("tb_lasti" (m:val -1))
      ("f_lineno" (m:val (pyrt:field $o lineno)))
      ("f_back" (m:val (pyrt:field-or $o back (py:None))))
      ("f_code" (case (pyrt:field $o mframe) (((m:frame $l $g $ns $c) (m:val (pyrt:code-object-of $c))))))
      ("f_globals" (case (pyrt:field $o mframe) (((m:frame $l $g $ns $c) (m:val $g)))))
      ("f_locals" (pyrt:frame-locals (pyrt:field $o mframe)))
      ("f_builtins" (case (pyrt:field $o mframe) (((m:frame $l $g $ns $c) (pyrt:frame-builtins $g)))))
      ("f_lasti" (m:val -1))
      ("f_trace" (m:val (py:None)))
      ("f_generator" (m:val (py:None)))
      ("__code__" (m:val (pyrt:code-object-of (pyrt:field $o code))))
      ("__defaults__" (m:val (let $dob (pyrt:field-or $o defaults_obj (py:None))
                               (if (== $dob (py:None))
                                   (let $ds (pyrt:field $o defaults) (if (== (size-atom $ds) 0) (py:None) (cons-atom py:tuple $ds)))
                                   $dob))))
      ("__kwdefaults__" (pyrt:function-kwdefaults $o))
      ("__globals__" (m:val (pyrt:field $o globals)))
      ("__closure__" (m:val (let $cl (pyrt:field $o closure) (if (== (size-atom $cl) 0) (py:None) (cons-atom py:tuple $cl)))))
      ("args" (m:val (pyrt:field-or $o args (py:tuple))))
      ("__context__" (m:val (pyrt:field-or $o context (py:None))))
      ("__cause__" (m:val (pyrt:field-or $o cause (py:None))))
      ("__suppress_context__" (m:val (pyrt:field-or $o suppress False)))
      ("__traceback__" (m:val (pyrt:field-or $o tb (py:None))))
      ("value" (m:val (let $args (cdr-atom (pyrt:field-or $o args (py:tuple))) (if (== (size-atom $args) 0) (py:None) (car-atom $args)))))
      ("code" (m:val (pyrt:exit-code $o)))
      ("name" (m:val (pyrt:field-or $o name (py:None))))
      ("obj" (m:val (pyrt:field-or $o obj (py:None))))
      ("msg" (pyrt:syntax-field $o msg 0))
      ("filename" (pyrt:syntax-field $o filename 1))
      ("lineno" (pyrt:syntax-field $o lineno 2))
      ("offset" (pyrt:syntax-field $o offset 3))
      ("text" (pyrt:syntax-field $o text 4))
      ("end_lineno" (pyrt:syntax-field $o end_lineno 5))
      ("end_offset" (pyrt:syntax-field $o end_offset 6))
      ("print_file_and_line" (pyrt:syntax-field $o print_file_and_line 7))
      ($other (m:val (pyrt:field-or $o (str:string->symbol $n) (py:None)))))))
(= (pyrt:getset-set (py:getset $n) $o $v)
   (case $n
     (("__dict__" (case (pyrt:base-of $v)
                     (((py:dict $i) (let $u (pyrt:set-field $o dict $v) (m:val (py:None))))
                      ($other (m:err (pyrt:exc "TypeError" (str:concat "__dict__ must be set to a dictionary, not a " (pyrt:quoted (pyrt:type-name $v)))))))))
      ("args" (let $u (pyrt:set-field $o args (if (pyrt:is-tuple $v) $v (case (pyrt:elements $v) (((m:val $es) (cons-atom py:tuple $es)))))) (m:val (py:None))))
      ("__context__" (let $u (pyrt:set-field $o context $v) (m:val (py:None))))
      ; BaseException_set_tb: a traceback or None
      ("__traceback__" (if (if (== $v (py:None)) true (== (pyrt:type-of $v) (py:type traceback)))
                           (let $u (pyrt:set-field $o tb $v) (m:val (py:None)))
                           (m:err (pyrt:exc "TypeError" "__traceback__ must be a traceback or None"))))
      ("__cause__" (let $u (pyrt:set-field $o cause $v) (let $u2 (pyrt:set-field $o suppress True) (m:val (py:None)))))
      ("__suppress_context__" (let $u (pyrt:set-field $o suppress (pyrt:truthy $v)) (m:val (py:None))))
      ("__name__" (let $u (pyrt:set-field $o name $v) (m:val (py:None))))
      ("__qualname__" (let $u (pyrt:set-field $o qualname $v) (m:val (py:None))))
      ("__module__" (case $o (((py:type $t) (pyrt:set-type-special $o "__module__" $v)) ($other (let $u (pyrt:set-field $o module $v) (m:val (py:None)))))))
      ("__doc__" (case $o (((py:type $t) (pyrt:set-type-special $o "__doc__" $v)) ($other (let $u (pyrt:set-field $o doc $v) (m:val (py:None)))))))
      ("__annotations__" (case $o (((py:type $t) (pyrt:set-type-annotations $o $v)) ((py:obj $i) (pyrt:set-module-annotations $o $v)) ($other (pyrt:set-function-annotations $o $v)))))
      ("__annotate__" (case $o (((py:type $t) (pyrt:set-type-annotate $o $v)) ((py:obj $i) (pyrt:set-module-annotate $o $v)) ($other (pyrt:set-function-annotate $o $v)))))
      ("cell_contents" (let $u (pyrt:cell-set $o $v) (m:val (py:None))))
      ("__type_params__" (case $o
                           (((py:type $t) (if (pyrt:is-tuple $v) (pyrt:set-type-special $o "__type_params__" $v)
                                              (m:err (pyrt:exc "TypeError" (str:concat "__type_params__ must be set to a tuple, not " (pyrt:type-name $v))))))
                            ($other (if (pyrt:is-tuple $v) (let $u (pyrt:set-field $o type_params $v) (m:val (py:None)))
                                        (m:err (pyrt:exc "TypeError" "__type_params__ must be set to a tuple")))))))
      ; func_set_defaults, func_set_kwdefaults: None, or a tuple, a dict
      ; (subclasses too); the binder reads them at every call
      ("__defaults__" (if (== $v (py:None))
                          (let $u (pyrt:set-field $o defaults ()) (let $u2 (pyrt:set-field $o defaults_obj (py:None)) (m:val (py:None))))
                          (let $db (pyrt:base-of $v)
                            (if (pyrt:is-tuple $db)
                                (let $u (pyrt:set-field $o defaults (cdr-atom $db))
                                  (let $u2 (pyrt:set-field $o defaults_obj (if (pyrt:is-tuple $v) (py:None) $v)) (m:val (py:None))))
                                (m:err (pyrt:exc "TypeError" "__defaults__ must be set to a tuple object"))))))
      ("__kwdefaults__" (if (== $v (py:None))
                            (let $u (pyrt:set-field $o kwdefaults ()) (m:val (py:None)))
                            (case (pyrt:base-of $v)
                              (((py:dict $kdi) (let $u (pyrt:set-field $o kwdefaults $v) (m:val (py:None))))
                               ($kdo (m:err (pyrt:exc "TypeError" "__kwdefaults__ must be set to a dict object")))))))
      ($other (let $u (pyrt:set-field $o (str:string->symbol $n) $v) (m:val (py:None)))))))
; a function's __kwdefaults__ (func_get_kwdefaults): None while it has none;
; else one dict, made from the pairs the definition gave when first asked for
; and kept, so that changing it changes the calls, as CPython's does
(= (pyrt:function-kwdefaults $o)
   (let $kwf (pyrt:field $o kwdefaults)
     (if (== $kwf ()) (m:val (py:None))
       (case $kwf
         (((py:dict $kfi) (m:val $kwf))
          ((py:obj $kfo) (m:val $kwf))
          ($kfp (case (pyrt:build-dict $kfp)
                  (((m:val $kfd) (let $u (pyrt:set-field $o kwdefaults $kfd) (m:val $kfd)))
                   ($kfe $kfe)))))))))
; a keyword-only parameter's default at a call: from the pairs the
; definition gave, or from the dict __kwdefaults__ is (its own storage, as
; PyDict_GetItem reads it); (m:unbound) when it has none
(= (pyrt:kwdefault-get $kwd $name)
   (if (== $kwd ()) (m:unbound)
     (case $kwd
       (((py:dict $kgi) (pyrt:kwdefault-in $kwd $name))
        ((py:obj $kgo) (pyrt:kwdefault-in (pyrt:base-of $kwd) $name))
        ($kgp (pyrt:kw-get $kgp $name (m:unbound)))))))
(= (pyrt:kwdefault-in $d $name) (let $r (pyrt:dict-get $d $name) (if (== $r (m:missing)) (m:unbound) $r)))
; a function's __annotations__ (function___annotations___get_impl): the
; dict kept, else its __annotate__(1)'s, kept, else an empty dict kept; set
; to a dict or None (or deleted), which also drops __annotate__
(= (pyrt:function-annotations $f)
   (let $a (pyrt:field $f annotations)
     (if (== $a (m:missing))
         (let $fn (pyrt:field-or $f annotate (py:None))
           (if (pyrt:is-callable $fn)
               (case (pyrt:call-sync $fn (cons-atom 1 ()) ())
                 (((m:val $d) (if (pyrt:is-dict-like $d) (pyrt:keep-annotations $f $d)
                                  (m:err (pyrt:exc "TypeError" (str:concat "__annotate__() must return a dict, not " (pyrt:type-fqn (pyrt:type-of $d)))))))
                  ($e $e)))
               (pyrt:keep-annotations $f (pyrt:dict-new))))
         (m:val $a))))
(= (pyrt:keep-annotations $f $d) (let $u (pyrt:set-field $f annotations $d) (m:val $d)))
(= (pyrt:is-dict-like $d) (case (pyrt:base-of $d) (((py:dict $i) true) ($o false))))
(= (pyrt:set-function-annotations $f $v)
   (if (if (== $v (py:None)) true (pyrt:is-dict-like $v))
       (let $u (if (== $v (py:None)) (pyrt:clear-field $f annotations) (pyrt:set-field $f annotations $v))
         (let $u2 (pyrt:clear-field $f annotate) (m:val (py:None))))
       (m:err (pyrt:exc "TypeError" "__annotations__ must be set to a dict object"))))
; its __annotate__ (function___annotate___get_impl, _set_impl): None, or a
; callable, which drops the annotations kept
(= (pyrt:function-annotate $f) (m:val (pyrt:field-or $f annotate (py:None))))
(= (pyrt:set-function-annotate $f $v)
   (if (== $v (py:None)) (let $u (pyrt:set-field $f annotate $v) (m:val (py:None)))
     (if (pyrt:is-callable $v)
         (let $u (pyrt:set-field $f annotate $v) (let $u2 (pyrt:clear-field $f annotations) (m:val (py:None))))
         (m:err (pyrt:exc "TypeError" "__annotate__ must be callable or None")))))
; a class's __annotate__ (type_get_annotate): its namespace's __annotate__,
; else its __annotate_func__ (both through their __get__ with no instance),
; else None, put there as __annotate_func__; a builtin type has none
(= (pyrt:type-annotate (py:type $t))
   (if (pyrt:builtin-class $t)
       (pyrt:type-no-attribute (py:type $t) "__annotate__")
       (let $d (pyrt:type-dict (py:type $t))
         (let $a (pyrt:dict-first $d "__annotate__" "__annotate_func__")
           (if (== $a (m:missing))
               (let $u (pyrt:type-dict-set (py:type $t) "__annotate_func__" (py:None)) (m:val (py:None)))
               (pyrt:class-get $a (py:type $t)))))))
; its __annotations__ (type_get_annotations): its namespace's __annotations__
; (from __future__ import annotations), else its __annotations_cache__, else
; its __annotate__(1)'s (a dict) or a new dict when it has none, kept as its
; __annotations_cache__
(= (pyrt:type-annotations (py:type $t))
   (if (pyrt:builtin-class $t)
       (pyrt:type-no-attribute (py:type $t) "__annotations__")
       (let $d (pyrt:type-dict (py:type $t))
         (let $a (pyrt:dict-first $d "__annotations__" "__annotations_cache__")
           (if (== $a (m:missing))
               (case (pyrt:type-annotate (py:type $t))
                 (((m:val $fn)
                   (case (pyrt:annotations-of $fn)
                     (((m:val $r) (let $u (pyrt:type-dict-set (py:type $t) "__annotations_cache__" $r) (m:val $r)))
                      ($e $e))))
                  ($e $e)))
               (pyrt:class-get $a (py:type $t)))))))
; what an object's __annotate__ gives, as the getters call it: a callable's
; result for format 1 (VALUE), which must be a dict; else a new dict
(= (pyrt:annotations-of $fn)
   (if (pyrt:is-callable $fn)
       (case (pyrt:call-sync $fn (cons-atom 1 ()) ())
         (((m:val $r) (if (pyrt:is-dict-like $r) (m:val $r)
                          (m:err (pyrt:exc "TypeError" (str:concat "__annotate__() must return a dict, not " (pyrt:type-fqn (pyrt:type-of $r)))))))
          ($e $e)))
       (m:val (pyrt:dict-new))))
(= (pyrt:dict-first $d $k1 $k2)
   (let $a (pyrt:dict-get $d $k1) (if (== $a (m:missing)) (pyrt:dict-get $d $k2) $a)))
; a namespace's value read from its class (tp_descr_get with no instance)
(= (pyrt:class-get $v $tp)
   (if (== (pyrt:mro-lookup (pyrt:type-of $v) "__get__") (m:missing)) (m:val $v)
       (pyrt:call-sync (pyrt:mro-lookup (pyrt:type-of $v) "__get__") (cons-atom $v (cons-atom (py:None) (cons-atom $tp ()))) ())))
(= (pyrt:type-no-attribute $tp $name)
   (m:err (pyrt:exc "AttributeError" (str:concat (str:concat (str:concat "type object " (pyrt:quoted (pyrt:tp-name-of $tp))) " has no attribute ") (pyrt:quoted $name)))))
(= (pyrt:type-immutable $tp $name)
   (m:err (pyrt:exc "TypeError" (str:concat (str:concat (str:concat "cannot set " (pyrt:quoted $name)) " attribute of immutable type ") (pyrt:quoted (pyrt:tp-name-of $tp))))))
; set (type_set_annotate): None or a callable, as __annotate_func__; a
; callable drops __annotations_cache__
(= (pyrt:set-type-annotate (py:type $t) $v)
   (if (pyrt:builtin-class $t) (pyrt:type-immutable (py:type $t) "__annotate__")
       (if (if (== $v (py:None)) true (pyrt:is-callable $v))
           (let $u (pyrt:type-dict-set (py:type $t) "__annotate_func__" $v)
             (let $u2 (if (== $v (py:None)) false (pyrt:type-dict-delete (py:type $t) "__annotations_cache__"))
               (m:val (py:None))))
           (m:err (pyrt:exc "TypeError" "__annotate__ must be callable or None")))))
; set (type_set_annotations): __annotations__ replaced when the namespace has
; one (and __annotations_cache__ dropped), else __annotations_cache__ set;
; then __annotate_func__ and __annotate__ dropped
(= (pyrt:set-type-annotations (py:type $t) $v)
   (if (pyrt:builtin-class $t) (pyrt:type-immutable (py:type $t) "__annotations__")
       (let $u (if (== (pyrt:dict-get (pyrt:type-dict (py:type $t)) "__annotations__") (m:missing))
                   (pyrt:type-dict-set (py:type $t) "__annotations_cache__" $v)
                   (let $u1 (pyrt:type-dict-set (py:type $t) "__annotations__" $v) (pyrt:type-dict-delete (py:type $t) "__annotations_cache__")))
         (let $u2 (pyrt:type-dict-delete (py:type $t) "__annotate_func__")
           (let $u3 (pyrt:type-dict-delete (py:type $t) "__annotate__") (m:val (py:None)))))))
; a module's __annotate__ (module_get_annotate): its namespace's, else None
; put there
(= (pyrt:module-annotate $m)
   (let $d (pyrt:object-dict $m)
     (let $a (pyrt:dict-get $d "__annotate__")
       (if (== $a (m:missing)) (let $u (pyrt:dict-set $d "__annotate__" (py:None)) (m:val (py:None))) (m:val $a)))))
; its __annotations__ (module_get_annotations): its namespace's, else its
; __annotate__(1)'s or a new dict, kept there unless the module is still
; being imported (its __spec__._initializing true)
(= (pyrt:module-annotations $m)
   (let $d (pyrt:object-dict $m)
     (let $a (pyrt:dict-get $d "__annotations__")
       (if (!= $a (m:missing)) (m:val $a)
           (case (pyrt:prelude-sync "_spec_initializing" (cons-atom (pyrt:dict-get-or $d "__spec__" (py:None)) ()))
             (((m:val $init)
               (let $fn (pyrt:dict-get-or $d "__annotate__" (py:None))
                 (case (pyrt:annotations-of $fn)
                   (((m:val $r) (if (== $init True) (m:val $r) (let $u (pyrt:dict-set $d "__annotations__" $r) (m:val $r))))
                    ($e $e)))))
              ($e $e)))))))
(= (pyrt:dict-get-or $d $k $default) (let $a (pyrt:dict-get $d $k) (if (== $a (m:missing)) $default $a)))
; set (module_set_annotate): None or a callable; a callable drops __annotations__
(= (pyrt:set-module-annotate $m $v)
   (if (if (== $v (py:None)) true (pyrt:is-callable $v))
       (let $d (pyrt:object-dict $m)
         (let $u (pyrt:dict-set $d "__annotate__" $v)
           (let $u2 (if (== $v (py:None)) false (pyrt:dict-delete $d "__annotations__")) (m:val (py:None)))))
       (m:err (pyrt:exc "TypeError" "__annotate__ must be callable or None"))))
; set (module_set_annotations): __annotations__ set, __annotate__ dropped
(= (pyrt:set-module-annotations $m $v)
   (let $d (pyrt:object-dict $m)
     (let $u (pyrt:dict-set $d "__annotations__" $v)
       (let $u2 (pyrt:dict-delete $d "__annotate__") (m:val (py:None))))))
; attribute access on objects and classes: the prelude's protocols
(= (pyrt:set-attr-on $ro $name $rv)
   (case $ro (((m:val $o) (case $rv (((m:val $v) (pyrt:setattr $o $name $v)) ($e $e)))) ($e $e))))
; o.name = v and del o.name: the type's __setattr__ and __delattr__ (for a
; builtin value object's, the generic ones: a data descriptor, else no
; __dict__ to hold it)
(= (pyrt:setattr $o $name $v)
   (case $o
     (((py:obj $id) (pyrt:object-setattr $o $name $v))
      ($other (pyrt:prelude-call "_setattr" (cons-atom $o (cons-atom $name (cons-atom $v ()))))))))
; o.name = v for a program's object whose class keeps object's __setattr__
; (PyObject_GenericSetAttr), here when the class's attribute of that name has
; no __set__ (none, a builtin type's value, a function, a builtin member's
; descriptor) and the object's __dict__ is a dict: the entry set
(= (pyrt:object-setattr $o $name $v)
   (let $tp (pyrt:type-of $o)
     (if (if (== (pyrt:mro-lookup $tp "__setattr__") (py:bmeth object "__setattr__"))
             (!= (pyrt:plain-descriptor-kind (pyrt:mro-lookup $tp $name)) other)
             false)
         (let $d (pyrt:object-dict $o)
           (case $d
             (((py:dict $did) (let $u (pyrt:dict-set $d $name $v) (m:val (py:None))))
              ($nd (pyrt:prelude-call "_setattr" (cons-atom $o (cons-atom $name (cons-atom $v ()))))))))
         (pyrt:prelude-call "_setattr" (cons-atom $o (cons-atom $name (cons-atom $v ())))))))
(= (pyrt:delattr $o $name) (pyrt:prelude-call "_delattr" (cons-atom $o (cons-atom $name ()))))

; ---------------------------------------------------------------- with

; the with statement (codegen 3.14): the manager's __exit__ and __enter__
; looked up on its type, in that order (a special lookup, bound through their
; descriptors), then __enter__ called; a missing one named, and the async
; protocol suggested when the type has it (_PyEval_SpecialMethodCanSuggest)
(= (pyrt:before-with $a $f)
   (case (pyrt:atom $a $f)
     (((m:val $m)
       (case (pyrt:lookup-special $m "__exit__")
         (((m:val (py:null)) (pyrt:missed-special $m "__exit__"))
          ((m:val $exit)
           (case (pyrt:lookup-special $m "__enter__")
             (((m:val (py:null)) (pyrt:missed-special $m "__enter__"))
              ((m:val $enter) (case (pyrt:call-sync $enter () ()) (((m:val $v) (m:val (c:entered $exit $v))) ($e $e))))
              ($e $e))))
          ($e $e))))
      ($e $e))))
(= (pyrt:missed-special $m $name)
   (let $t (pyrt:type-of $m)
     (m:err (pyrt:exc "TypeError"
              (str:concat (str:concat (str:concat (str:concat (str:concat "'" (pyrt:type-fqn $t)) "' object does not support the context manager protocol (missed ") $name) " method)")
                          (if (if (!= (pyrt:mro-lookup $t "__aenter__") (m:missing)) (!= (pyrt:mro-lookup $t "__aexit__") (m:missing)) false)
                              " but it supports the asynchronous context manager protocol. Did you mean to use 'async with'?"
                              ""))))))
(= (pyrt:lookup-special $m $name) (pyrt:prelude-sync "_lookup_special" (cons-atom $m (cons-atom $name ()))))
; __exit__ called: without an exception its result is dropped; with one,
; which is being handled while __exit__ runs and its result is tested, the
; result's truth says whether it is suppressed
(= (pyrt:with-exit $exit $how)
   (case $how
     (((c:no-exc) (case (pyrt:call-sync $exit (cons-atom (py:None) (cons-atom (py:None) (cons-atom (py:None) ()))) ())
                    (((m:val $r) (m:val (py:False))) ($e $e))))
      ((c:exc $e)
       (let $u1 (pyrt:handle $e)
         (let $r (case (pyrt:call-sync $exit (cons-atom (pyrt:type-of $e) (cons-atom $e (cons-atom (py:None) ()))) ())
                   (((m:val $x) (case (pyrt:truth-of $x) (((m:val $b) (m:val (if $b (py:True) (py:False)))) ($e2 $e2))))
                    ($e2 $e2)))
           (let $u2 (pyrt:unhandle) $r)))))))

; ---------------------------------------------------------------- generators

; A generator (py:gen ID): its code and frame, its state (created,
; suspended, running, finished), and while suspended where it waits (the
; target of what it is sent, or (c:yf ITER TARGET) in a yield from), its
; continuation up to its delimiter, and the exceptions it was handling (its
; part of the stack of handled exceptions, CPython's gi_exc_state).
(= (pyrt:gen-new $code $frame) (pyrt:gen-init (py:gen (pyrt:new-id)) $code $frame))
; a coroutine (py:coro ID): a generator's state under its own type, not
; iterable; sending to one that has finished is an error
(= (pyrt:coro-new $code $frame) (pyrt:gen-init (py:coro (pyrt:new-id)) $code $frame))
(= (pyrt:gen-noun (py:gen $id)) "generator")
(= (pyrt:gen-noun (py:coro $id)) "coroutine")
(= (pyrt:gen-noun (py:agen $id)) "async generator")
; an async generator (py:agen ID): a generator's state whose yields are
; wrapped (py:agwrap V); its __anext__/asend/athrow/aclose awaitables (the
; prelude's) step it
(= (pyrt:agen-new $code $frame) (pyrt:gen-init (py:agen (pyrt:new-id)) $code $frame))
(= (pyrt:eval (c:agwrap $a) (m:frame $l $g $ns $c))
   (case (pyrt:atom $a (m:frame $l $g $ns $c)) (((m:val $x) (m:val (py:agwrap $x))) ($e $e))))
; a raw step of a generator (gen_send or _gen_throw) for the async
; generator's awaitables: ("w" V) an async yield, ("y" V) a value an await
; inside passes on, ("r" V) its end; its exception raised
(= (pyrt:gen-step $g $how $v)
   (case (if (== $how "send") (pyrt:gen-send $g $v) (pyrt:gen-throw $g $v))
     (((m:yielded (py:agwrap $x)) (m:val (py:tuple "w" $x)))
      ((m:yielded $x) (m:val (py:tuple "y" $x)))
      ((m:returned $x) (m:val (py:tuple "r" $x)))
      ($e $e))))
(= (pyrt:gen-init $g (c:code $nm $q $a $ns $cs $fs $fl $body $cofile) $frame)
   (let $u1 (pyrt:set-field $g code (c:code $nm $q $a $ns $cs $fs $fl $body $cofile))
     (let $u2 (pyrt:set-field $g frame $frame)
       (let $u3 (pyrt:set-field $g state created)
         (let $u4 (pyrt:set-field $g name $nm)
           (let $u5 (pyrt:set-field $g qualname $q)
             (let $u6 (pyrt:set-field $g handled ()) $g)))))))
; a step of a generator: sending V (next() sends None); (m:yielded X),
; (m:returned X) or (m:err E)
(= (pyrt:gen-send $g $v)
   (let $st (pyrt:field $g state)
     (if (== $st created)
         (if (== $v (py:None))
             (case (pyrt:field $g code)
               (((c:code $nm $q $a $ns $cs $fs $fl $body $cofile)
                 (pyrt:gen-run $g (MExec $body (pyrt:field $g frame) (k:gen $g (k:host)))))))
             (m:err (pyrt:raise-instance (pyrt:exc "TypeError" (str:concat "can't send non-None value to a just-started " (pyrt:gen-noun $g))))))
         (if (== $st suspended)
             (pyrt:gen-run $g (MGenResume (pyrt:field $g where) $v (pyrt:field $g frame) (pyrt:gen-plug (pyrt:field $g inner) $g)))
             (if (== $st running)
                 (m:err (pyrt:raise-instance (pyrt:exc "ValueError" (str:concat (pyrt:gen-noun $g) " already executing"))))
                 (pyrt:gen-done $g))))))
; a finished generator returns None; a finished coroutine cannot be resumed
(= (pyrt:gen-done $g)
   (case $g
     (((py:coro $id) (m:err (pyrt:raise-instance (pyrt:exc "RuntimeError" "cannot reuse already awaited coroutine"))))
      ($other (m:returned (py:None))))))
; an exception thrown in (_gen_throw): raised where the generator waits (in a
; yield from passed on), its context the exception the generator handles
; there; a generator not yet started or finished raises it at once
(= (pyrt:gen-throw $g $e)
   (let $st (pyrt:field $g state)
     (if (== $st suspended)
         (pyrt:gen-run $g (MGenThrow (pyrt:gen-chain $g $e) (pyrt:field $g where) (pyrt:field $g frame) (pyrt:gen-plug (pyrt:field $g inner) $g)))
         (if (== $st running)
             (m:err (pyrt:raise-instance (pyrt:exc "ValueError" (str:concat (pyrt:gen-noun $g) " already executing"))))
             (if (if (== $st finished) (== (pyrt:gen-noun $g) "coroutine") false)
                 (pyrt:gen-done $g)
                 (let $u (pyrt:set-field $g state finished) (m:err (pyrt:propagating $e))))))))
; raised where it waits, not passed on to what it yields from
(= (pyrt:gen-throw-here $g $e)
   (pyrt:gen-run $g (MGenThrow (pyrt:gen-chain $g $e) (c:sent (c:discard)) (pyrt:field $g frame) (pyrt:gen-plug (pyrt:field $g inner) $g))))
(= (pyrt:gen-chain $g $e)
   (let $h (pyrt:field $g handled)
     (if (== $h ()) $e
       (let $top (car-atom $h)
         (if (== $top $e) $e
           (let $b (pyrt:break-context-cycle $top $e ()) (let $u (pyrt:set-field $e context $top) $e)))))))
; close() (gen_close): GeneratorExit raised where it waits, after what it
; yields from is closed; yielding again is a RuntimeError; its return value
; is close()'s
(= (pyrt:gen-close $g)
   (let $st (pyrt:field $g state)
     (if (== $st created) (let $u (pyrt:set-field $g state finished) (m:val (py:None)))
       (if (== $st finished) (m:val (py:None))
         (if (== $st running) (m:err (pyrt:exc "ValueError" (str:concat (pyrt:gen-noun $g) " already executing")))
           (case (case (pyrt:field $g where) (((c:yf $i $t) (pyrt:close-iter $i)) ($o (m:val (py:None)))))
             (((m:err $e2) (pyrt:gen-close-result $g (pyrt:gen-throw-here $g (pyrt:raise-instance $e2))))
              ($ok (pyrt:gen-close-result $g (pyrt:gen-throw-here $g (pyrt:exc-new (py:type GeneratorExit) ())))))))))))
(= (pyrt:gen-close-result $g $r)
   (case $r
     (((m:yielded $x) (m:err (pyrt:exc "RuntimeError" (str:concat (pyrt:gen-noun $g) " ignored GeneratorExit"))))
      ((m:returned $x) (m:val $x))
      ((m:err $e) (if (pyrt:exc-is $e GeneratorExit) (m:val (py:None)) (m:err $e))))))
; closing what a yield from iterates (gen_close_iter): a generator's close(),
; else its close method if it has one
(= (pyrt:close-iter $i)
   (case $i
     (((py:gen $id) (pyrt:gen-close $i))
      ((py:coro $id) (pyrt:gen-close $i))
      ($other (case (pyrt:getattr-now $i "close")
                (((m:val $m) (pyrt:call-sync $m () ()))
                 ($e (m:val (py:None)))))))))
; a step run: its exceptions pushed back on the handled stack, the machine
; run from the configuration to the generator's next yield, return or raise
(= (pyrt:gen-run $g $config)
   (let $base (size-atom (get-state &py-handled))
     (let $u1 (change-state! &py-handled (pyrt:append (pyrt:field $g handled) (get-state &py-handled)))
       (let $u2 (pyrt:set-field $g base $base)
         (let $u3 (pyrt:set-field $g state running)
           (case (collapse (gslt:entry:python-machine:rewrite:10 (quote $config)))
             ((((gslt:result:python-machine:rewrite:10 $r)) $r)
              ($none (m:err (pyrt:exc "SystemError" "a generator's step did not finish"))))))))))
; a yield: the continuation up to the delimiter kept with the frame and where
; the sent value goes, the generator's handled exceptions taken off the
; stack; the delimiter's outer continuation returned
(= (pyrt:gen-suspend $k $w $f)
   (let (c:split $inner $g $kc) (pyrt:gen-split $k)
     (let $u1 (pyrt:set-field $g inner $inner)
       (let $u2 (pyrt:set-field $g where $w)
         (let $u3 (pyrt:set-field $g frame $f)
           (let $u4 (pyrt:set-field $g handled (pyrt:handled-take (pyrt:field $g base)))
             (let $u5 (pyrt:set-field $g state suspended) $kc)))))))
(= (pyrt:handled-take $base)
   (let $s (get-state &py-handled)
     (let $n (- (size-atom $s) $base)
       (let $u (change-state! &py-handled (pyrt:drop $s $n)) (pyrt:take $s $n)))))
(= (pyrt:gen-finish $g)
   (let $u1 (pyrt:handled-take (pyrt:field $g base))
     (let $u2 (pyrt:set-field $g state finished)
       (let $u3 (pyrt:set-field $g handled ())
         (let $u4 (pyrt:clear-field $g inner) ok)))))
; an exception ended the generator: a StopIteration from its body becomes a
; RuntimeError caused by it (PEP 479); it propagates to the caller
(= (pyrt:gen-raised $g $e)
   (let $u (pyrt:gen-finish $g)
     (pyrt:propagating
       (if (if (pyrt:exc-is $e StopIteration) true (if (pyrt:exc-is $e StopAsyncIteration) (== (pyrt:gen-noun $g) "async generator") false))
           (let $r (pyrt:raise-instance (pyrt:exc "RuntimeError" (str:concat (pyrt:gen-noun $g) (if (pyrt:exc-is $e StopIteration) " raised StopIteration" " raised StopAsyncIteration"))))
             (let $u1 (pyrt:set-field $r cause $e)
               (let $u2 (pyrt:set-field $r context $e)
                 (let $u3 (pyrt:set-field $r suppress True) $r))))
           $e))))
; the continuation up to the delimiter, the delimiter replaced by (k:hole);
; each continuation frame holds the rest of the continuation last
(= (pyrt:gen-split $k)
   (case $k
     (((k:gen $g $kc) (c:split (k:hole) $g $kc))
      ($frame (let (c:split $inner $g $kc) (pyrt:gen-split (pyrt:last-of $frame))
                (c:split (pyrt:with-last $frame $inner) $g $kc))))))
(= (pyrt:gen-plug $k $g)
   (case $k
     (((k:hole) (k:gen $g (k:host)))
      ($frame (pyrt:with-last $frame (pyrt:gen-plug (pyrt:last-of $frame) $g))))))
(= (pyrt:with-last $e $x)
   (let ($h $t) (decons-atom $e)
     (if (== $t ()) (cons-atom $x ()) (cons-atom $h (pyrt:with-last $t $x)))))
; next(): the next value, the end (m:exhausted)
(= (pyrt:gen-next $g)
   (case (pyrt:gen-send $g (py:None))
     (((m:yielded $x) (m:val $x))
      ((m:returned $x) (m:val (m:exhausted)))
      ((m:err $e) (m:err $e)))))
; send(), throw() and __next__ as methods: a return is StopIteration(value)
(= (pyrt:gen-method-result $r)
   (case $r
     (((m:yielded $x) (m:val $x))
      ((m:returned $x) (m:err (pyrt:stop-iteration $x)))
      ((m:err $e) (m:err $e)))))
(= (pyrt:stop-iteration $x)
   (if (== $x (py:None)) (py:exca "StopIteration" ()) (py:exca "StopIteration" (cons-atom $x ()))))
(= (pyrt:stop-value $x)
   (let $args (cdr-atom (pyrt:field-or $x args (py:tuple))) (if (== (size-atom $args) 0) (py:None) (car-atom $args))))
; yield from's step: a generator is sent the value; any other iterator is
; asked for its next value when it is None, else its send method is called
(= (pyrt:yf-send $i $v)
   (case $i
     (((py:gen $id) (pyrt:gen-send $i $v))
      ((py:coro $id) (pyrt:gen-send $i $v))
      ; a program's iterator (PyIter_Send): its __next__ (the prelude's _next),
      ; a StopIteration's value the result
      ((py:obj $id)
       (if (== $v (py:None))
           (pyrt:yf-method (m:val (pyrt:prelude "_next")) (cons-atom $i ()))
           (pyrt:yf-method (pyrt:getattr-now $i "send") (cons-atom $v ()))))
      ($other
       (if (== $v (py:None))
           (case (pyrt:next-of $i)
             (((m:val (m:exhausted)) (m:returned (py:None)))
              ((m:val $x) (m:yielded $x))
              ((m:err $e) (m:err (pyrt:raise-instance $e)))))
           (pyrt:yf-method (pyrt:getattr-now $i "send") (cons-atom $v ())))))))
(= (pyrt:yf-method $rm $xs)
   (case $rm
     (((m:val $m) (case (pyrt:call-sync $m $xs ())
                    (((m:val $x) (m:yielded $x))
                     ((m:err $e) (let $x (pyrt:raise-instance $e)
                                   (if (pyrt:exc-is $x StopIteration) (m:returned (pyrt:stop-value $x)) (m:err $x)))))))
      ((m:err $e) (m:err (pyrt:raise-instance $e))))))
; an exception thrown into a yield from: GeneratorExit closes the iterator
; and is raised here; any other goes to a generator's throw(), or to the
; iterator's throw method, else is raised here
(= (pyrt:yf-throw $i $e)
   (if (pyrt:exc-is $e GeneratorExit)
       (case (pyrt:close-iter $i)
         (((m:err $e2) (m:err (pyrt:raise-instance $e2)))
          ($ok (m:err $e))))
       (case $i
         (((py:gen $id) (pyrt:gen-throw $i $e))
          ((py:coro $id) (pyrt:gen-throw $i $e))
          ($other (case (pyrt:getattr-now $i "throw")
                    (((m:val $m) (pyrt:yf-method (m:val $m) (cons-atom $e ())))
                     ($missing (m:err $e)))))))))

; ---------------------------------------------------------------- the machine's questions

(= (pyrt:truth $a $f)
   (case (pyrt:atom $a $f) (((m:val $x) (case (pyrt:truth-of $x) (((m:val $b) (if $b (m:true) (m:false))) ($e $e)))) ($e $e))))
; an object's truth as a result: a program's object through __bool__ or
; __len__ (Python, from the host)
(= (pyrt:truth-of $x)
   (if (pyrt:needs-python $x) (pyrt:prelude-sync "_truth" (cons-atom $x ())) (m:val (pyrt:truthy $x))))
(= (pyrt:takes $c $k)
   (case (tk $c $k)
     (((tk (c:break $l1) (k:block $l2 $rest)) (if (== $l1 $l2) (m:break) (m:pass)))
      ((tk (c:break $l1) (k:loop $l2 $s $rest)) (if (== $l1 $l2) (m:break) (m:pass)))
      ((tk (c:continue $l1) (k:loop $l2 $s $rest)) (if (== $l1 $l2) (m:continue) (m:pass)))
      ($other (m:pass)))))
(= (pyrt:none) (py:None))
(= (pyrt:status (m:ok)) 0)
; an exception no frame took: SystemExit ends the run with its code; any
; other is reported, the exceptions it was raised from or during first
(= (pyrt:report $e)
   (let $x (pyrt:exc-instance $e)
     (if (pyrt:exc-is $x SystemExit) (pyrt:system-exit $x)
       (case (pyrt:prelude-sync "_report_text" (cons-atom $x ()))
         (((m:val $text) (let $w (system:write stderr $text) 1))
          ($e (let $w (pyrt:print-chain $x ()) 1)))))))
(= (pyrt:print-chain $x $seen)
   (let $cause (pyrt:field-or $x cause (py:None))
     (let $ctx (pyrt:field-or $x context (py:None))
       (let $u (if (if (== $cause (py:None)) false (not (pyrt:in-flat $cause $seen)))
                   (let $p (pyrt:print-chain $cause (cons-atom $x $seen))
                     (system:write stderr "\nThe above exception was the direct cause of the following exception:\n\n"))
                   (if (if (== $cause (py:None))
                           (if (== $ctx (py:None)) false
                               (if (pyrt:field-or $x suppress False) false (not (pyrt:in-flat $ctx $seen))))
                           false)
                       (let $p (pyrt:print-chain $ctx (cons-atom $x $seen))
                         (system:write stderr "\nDuring handling of the above exception, another exception occurred:\n\n"))
                       ()))
         (let $w (system:write stderr "Traceback (most recent call last):\n")
           (system:write stderr (str:concat (pyrt:exc-line $x) "\n")))))))
; the last line: the type (after its module, unless __main__ or builtins), and
; the exception's str when it is not empty
(= (pyrt:exc-line $x)
   (let $tn (pyrt:exc-type-display (pyrt:type-of $x))
     (let $s (pyrt:exc-str $x) (if (== $s "") $tn (str:concat (str:concat $tn ": ") $s)))))
(= (pyrt:exc-type-display $t)
   (let $m (pyrt:module-of-type $t)
     (if (if (== $m "builtins") true (== $m "__main__")) (pyrt:type-name-of $t)
         (str:concat (str:concat $m ".") (pyrt:type-name-of $t)))))
; SystemExit: no code or None is 0, an int its own status; any other is
; written to standard error, and the status is 1
(= (pyrt:system-exit $x)
   (let $code (pyrt:exit-code $x)
     (if (== $code (py:None)) 0
       (if (pyrt:intish $code) (pyrt:int-of $code)
         (let $w (system:write stderr (str:concat (pyrt:str $code) "\n")) 1)))))
