; The members of the core builtin types' namespaces that CPython implements in
; C, for host_runtime.metta: the table (pyrt:bmember TYPE NAME KIND) of
; builtin_types.metta, generated from CPython, made values.  A member is
; (py:bmeth TYPE NAME): a method descriptor, a slot wrapper, a classmethod
; descriptor or tp_new as __new__.  Bound, it is (py:bmeth-bound TYPE NAME
; SELF).  Called with self, it runs the builtin operation on self's builtin
; part: an instance of a program's subclass of a builtin type is an object
; (py:obj ID) whose field value holds that part (its layout's base).

; ---------------------------------------------------------------- the table

(= (pyrt:bmember-kind $t $n)
   (let $ks (collapse (match &self (pyrt:bmember $t $n $k) $k))
     (if (== $ks ()) (m:missing) (car-atom $ks))))
; the builtin types' members put in their namespaces, after the prelude's own
; (those it writes in Python stay); __hash__ None where CPython has it
(= (pyrt:install-builtin-members)
   (let $u (pyrt:install-members (collapse (match &self (pyrt:bmember $t $n $k) (pyrt:bm $t $n $k))))
     (pyrt:install-docs (collapse (match &self (pyrt:bdoc $t $d) (pyrt:bd $t $d))))))
(= (pyrt:install-docs $ds)
   (if (== $ds ()) ()
     (let ((pyrt:bd $t $d) $rest) (decons-atom $ds)
       (let $u (if (== (pyrt:dict-get (pyrt:type-dict (py:type $t)) "__doc__") (m:missing)) (pyrt:type-dict-set (py:type $t) "__doc__" $d) ())
         (pyrt:install-docs $rest)))))
(= (pyrt:install-members $ms)
   (if (== (size-atom $ms) 0) ()
     (let ((pyrt:bm $t $n $k) $rest) (decons-atom $ms)
       (let $v (pyrt:dict-get (pyrt:type-dict (py:type $t)) $n)
         (let $u (if (== $v (m:missing)) (pyrt:install-member $t $n $k) (pyrt:adopt-member $t $n $k $v))
           (pyrt:install-members $rest))))))
; a member the prelude writes in Python: the namespace holds the member's
; descriptor, as CPython's does, the prelude's function its implementation
(= (pyrt:adopt-member $t $n $k $v)
   (case (pyrt:implementation-of $k $v)
     (((m:val $f) (let $a (add-atom &py-heap (h:bimpl $t $n $f)) (pyrt:type-dict-set (py:type $t) $n (py:bmeth $t $n))))
      ($none ()))))
(= (pyrt:implementation-of $k $v)
   (case $k
     ((method (pyrt:function-value $v))
      (slot (pyrt:function-value $v))
      (classmethod (pyrt:wrapped-function $v classmethod))
      (static (pyrt:wrapped-function $v staticmethod))
      (new (case (pyrt:wrapped-function $v staticmethod) (((m:val $f) (m:val $f)) ($none (pyrt:function-value $v)))))
      ($other (m:none)))))
(= (pyrt:function-value $v) (case $v (((py:func $i) (m:val $v)) ($o (m:none)))))
(= (pyrt:wrapped-function $v $kind)
   (if (== (pyrt:type-of $v) (py:type $kind)) (pyrt:function-value (pyrt:field $v func)) (m:none)))
(= (pyrt:install-member $t $n $k)
   (case $k
     ((unhashable (pyrt:type-dict-set (py:type $t) $n (py:None)))
      (method (pyrt:type-dict-set (py:type $t) $n (py:bmeth $t $n)))
      (slot (pyrt:type-dict-set (py:type $t) $n (py:bmeth $t $n)))
      (classmethod (pyrt:type-dict-set (py:type $t) $n (py:bmeth $t $n)))
      (static (pyrt:type-dict-set (py:type $t) $n (py:bmeth $t $n)))
      (new (pyrt:type-dict-set (py:type $t) $n (py:bmeth $t $n)))
      ($other ()))))

; ---------------------------------------------------------------- the values

; a builtin value's builtin part: an instance of a subclass gives its base
(= (pyrt:base-of $o)
   (case $o
     (((py:obj $id) (let $v (pyrt:field $o value) (if (== $v (m:missing)) $o $v)))
      ($other $o))))
(= (pyrt:bmeth-type $t $n)
   (case (pyrt:bmember-kind $t $n)
     ((method (py:type method_descriptor))
      (slot (py:type wrapper_descriptor))
      (classmethod (py:type classmethod_descriptor))
      ($other (py:type builtin_function_or_method)))))
(= (pyrt:bmeth-bound-type $t $n)
   (case (pyrt:bmember-kind $t $n)
     ((slot (py:type method-wrapper))
      ($other (py:type builtin_function_or_method)))))
(= (pyrt:bmeth-repr $t $n)
   (case (pyrt:bmember-kind $t $n)
     ((slot (str:concat (str:concat (str:concat (str:concat "<slot wrapper " (pyrt:quoted $n)) " of ") (pyrt:quoted (pyrt:tp-name-of (py:type $t)))) " objects>"))
      (new (str:concat (str:concat (str:concat "<built-in method __new__ of type object at " (pyrt:address (py:type $t))) "") ">"))
      (static (str:concat (str:concat (str:concat (str:concat "<built-in method " $n) " of type object at ") (pyrt:address (py:type $t))) ">"))
      ($other (str:concat (str:concat (str:concat (str:concat "<method " (pyrt:quoted $n)) " of ") (pyrt:quoted (pyrt:tp-name-of (py:type $t)))) " objects>")))))
(= (pyrt:bmeth-bound-repr $t $n $s)
   (case (pyrt:bmember-kind $t $n)
     ((slot (str:concat (str:concat (str:concat (str:concat (str:concat "<method-wrapper " (pyrt:quoted $n)) " of ") (pyrt:type-name $s)) " object at ") (str:concat (pyrt:address $s) ">")))
      (classmethod (str:concat (str:concat (str:concat (str:concat "<built-in method " $n) " of type object at ") (pyrt:address $s)) ">"))
      ($other (str:concat (str:concat (str:concat (str:concat (str:concat "<built-in method " $n) " of ") (pyrt:type-name $s)) " object at ") (str:concat (pyrt:address $s) ">"))))))
; tp_descr_get (method_get, wrapperdescr_get, classmethod_get): no instance
; (null) gives the descriptor, an instance (None too) binds it after
; descr_check; a classmethod binds the class
(= (pyrt:bmeth-get $t $n $obj $tp)
   (case (pyrt:bmember-kind $t $n)
     ((classmethod (m:val (py:bmeth-bound $t $n (if (== $obj (py:null)) $tp (pyrt:type-of $obj)))))
      (static (m:val (py:bmeth $t $n)))
      ($k (if (== $obj (py:null)) (m:val (py:bmeth $t $n))
            (if (pyrt:instance-of-builtin $obj $t) (m:val (py:bmeth-bound $t $n $obj))
                (m:err (pyrt:exc "TypeError" (pyrt:descriptor-mismatch $k $t $n $obj)))))))))
(= (pyrt:instance-of-builtin $x $t) (pyrt:in-mro (py:type $t) (pyrt:mro-of (pyrt:type-of $x))))
(= (pyrt:descriptor-mismatch $k $t $n $obj)
   (if (== $k slot)
       (str:concat (str:concat (str:concat (str:concat (str:concat "descriptor " (pyrt:quoted $n)) " requires a ") (pyrt:quoted (pyrt:tp-name-of (py:type $t)))) " object but received a ") (pyrt:quoted (pyrt:type-name $obj)))
       (str:concat (str:concat (str:concat (str:concat (str:concat "descriptor " (pyrt:quoted $n)) " for ") (pyrt:quoted (pyrt:tp-name-of (py:type $t)))) " objects doesn't apply to a ") (str:concat (pyrt:quoted (pyrt:type-name $obj)) " object"))))

; ---------------------------------------------------------------- calls

(= (pyrt:call-bmeth $t $n $xs $ks)
   (let $k (pyrt:bmember-kind $t $n)
     (let $impl (pyrt:bmeth-impl $t $n)
       (case $k
         ((new (if (== $impl (m:missing)) (pyrt:builtin-new $t $xs $ks) (pyrt:call-kw $impl $xs $ks)))
          (classmethod (pyrt:bmeth-classmethod $t $n $xs $ks))
          (static (if (== $impl (m:missing)) (pyrt:call-bound-kw (py:type $t) $n $xs $ks) (pyrt:call-kw $impl $xs $ks)))
          ($other
           (if (== $xs ())
               (m:err (pyrt:exc "TypeError" (pyrt:needs-argument $k $t $n)))
               (let ($self $rest) (decons-atom $xs)
                 (if (pyrt:instance-of-builtin $self $t)
                     (if (== $impl (m:missing)) (pyrt:bmeth-apply $t $n $self $rest $ks) (pyrt:call-kw $impl $xs $ks))
                     (m:err (pyrt:exc "TypeError" (pyrt:descriptor-mismatch $k $t $n $self))))))))))))
; the prelude's implementation of a member it writes in Python, else missing
(= (pyrt:bmeth-impl $t $n)
   (let $fs (collapse (match &py-heap (h:bimpl $t $n $f) $f))
     (if (== (size-atom $fs) 0) (m:missing) (car-atom $fs))))
(= (pyrt:needs-argument $k $t $n)
   (if (== $k slot)
       (str:concat (str:concat (str:concat (str:concat "descriptor " (pyrt:quoted $n)) " of ") (pyrt:quoted (pyrt:tp-name-of (py:type $t)))) " object needs an argument")
       (str:concat (str:concat (str:concat (str:concat "unbound method " (pyrt:type-name-of (py:type $t))) ".") $n) "() needs an argument")))
; a classmethod descriptor's call: the class first (dict.fromkeys)
(= (pyrt:bmeth-classmethod $t $n $xs $ks)
   (if (== $xs ()) (m:err (pyrt:exc "TypeError" (str:concat (str:concat (str:concat "descriptor " (pyrt:quoted $n)) " of ") (str:concat (pyrt:quoted (pyrt:tp-name-of (py:type $t))) " object needs an argument"))))
     (let ($cls $rest) (decons-atom $xs) (pyrt:class-method-call $t $n $cls $rest $ks))))
; object.__subclasshook__ answers NotImplemented to any class (the ABCs ask
; it); the others are the type's own methods
(= (pyrt:class-method-call $t $n $cls $xs $ks)
   (let $impl (pyrt:bmeth-impl $t $n)
     (if (== $impl (m:missing))
         (case (cm $t $n)
           (((cm object "__subclasshook__") (m:val (py:NotImplemented)))
            ($other (pyrt:call-bound-kw (py:type $t) $n $xs $ks))))
         (pyrt:call-kw $impl (cons-atom $cls $xs) $ks))))
; a dunder by its slot's meaning (__init__ with its keywords); any other
; member by the builtin method of that name on self's builtin part
(= (pyrt:bmeth-apply $t $n $o $xs $ks)
   (if (== $n "__init__") (pyrt:builtin-init $t (pyrt:base-of $o) $xs $ks)
     (if (str:starts-with $n "__")
         (if (== $ks ()) (pyrt:slot-call $t $n $o (pyrt:base-of $o) $xs)
           (m:err (pyrt:exc "TypeError" (str:concat (str:concat (pyrt:type-name-of (py:type $t)) ".") (str:concat $n "() takes no keyword arguments")))))
         (pyrt:call-bound-kw (pyrt:base-of $o) $n $xs $ks))))

; ---------------------------------------------------------------- slots

; the slot functions of the core builtin types (wrap_* of typeobject.c's
; slotdefs over listobject.c, dictobject.c, longobject.c, floatobject.c, ...),
; for O, its builtin part S.  A sequence's index is judged by its builtin
; part; a dict's key stays as given (its own __hash__ and __eq__); attribute
; access is the generic one on O (PyObject_GenericGetAttr)
(= (pyrt:slot-call $t $n $o $s $xs)
   (case (slot $n $xs)
     (((slot "__len__" ()) (pyrt:len-builtin $s))
      ((slot "__getitem__" ($k)) (if (== $t dict) (pyrt:dict-subscript $o $s $k) (pyrt:getitem-builtin $s (pyrt:base-of $k))))
      ((slot "__setitem__" ($k $v)) (pyrt:setitem-builtin $s (pyrt:index-or-key $t $k) $v))
      ((slot "__delitem__" ($k)) (pyrt:del-item-builtin $s (pyrt:index-or-key $t $k)))
      ((slot "__contains__" ($x)) (pyrt:contains $s $x))
      ((slot "__iter__" ()) (pyrt:iter-builtin $s))
      ((slot "__repr__" ()) (pyrt:repr-of $s))
      ((slot "__str__" ()) (pyrt:str-of $s))
      ((slot "__hash__" ()) (pyrt:hash $s))
      ((slot "__bool__" ()) (m:val (if (pyrt:truthy $s) True False)))
      ((slot "__getattribute__" ($nm)) (pyrt:prelude-call "_object_getattribute" (cons-atom $o (cons-atom $nm ()))))
      ((slot "__setattr__" ($nm $v)) (pyrt:prelude-call "_object_setattr" (cons-atom $o (cons-atom $nm (cons-atom $v ())))))
      ((slot "__delattr__" ($nm)) (pyrt:prelude-call "_object_delattr" (cons-atom $o (cons-atom $nm ()))))
      ((slot "__eq__" ($x)) (pyrt:slot-compare $t (py:Eq) $s $x))
      ((slot "__ne__" ($x)) (pyrt:slot-compare $t (py:NotEq) $s $x))
      ((slot "__lt__" ($x)) (pyrt:slot-compare $t (py:Lt) $s $x))
      ((slot "__le__" ($x)) (pyrt:slot-compare $t (py:LtE) $s $x))
      ((slot "__gt__" ($x)) (pyrt:slot-compare $t (py:Gt) $s $x))
      ((slot "__ge__" ($x)) (pyrt:slot-compare $t (py:GtE) $s $x))
      ((slot "__round__" ()) (pyrt:round $s (py:None)))
      ((slot "__round__" ($nd)) (pyrt:round $s $nd))
      ((slot "__divmod__" ($x)) (let $b (pyrt:base-of $x) (if (pyrt:in-domain $t $b) (pyrt:divmod $s $b) (m:val (py:NotImplemented)))))
      ((slot "__rdivmod__" ($x)) (let $b (pyrt:base-of $x) (if (pyrt:in-domain $t $b) (pyrt:divmod $b $s) (m:val (py:NotImplemented)))))
      ((slot $d ($x)) (pyrt:slot-binary-by-name $t $d $o $s $x))
      ((slot $d ()) (pyrt:slot-unary-by-name $t $d $s))
      ($other (pyrt:call-bound $s $n $xs)))))
(= (pyrt:index-or-key $t $k) (if (== $t dict) $k (pyrt:base-of $k)))
; dict_subscript: the key's value; for a subclass's instance a missing key
; goes to type(O).__missing__(O, key) when it has one, else KeyError
(= (pyrt:dict-subscript $o $s $k)
   (let $r (pyrt:getitem-builtin $s $k)
     (case $r
       (((m:err $e) (if (== $o $s) $r
                      (if (pyrt:exc-is (pyrt:exc-instance $e) KeyError)
                          (pyrt:prelude-call "_dict_missing" (cons-atom $o (cons-atom $k ())))
                          $r)))
        ($v $v)))))
; the binary slots: the operation when the other operand is of the type's
; domain (an int's another int; a float's a float or an int; a sequence's
; its own kind), else NotImplemented; the reflected ones swap the operands;
; the in-place ones change S and answer O
(= (pyrt:slot-binary-by-name $t $d $o $s $x)
   (case (pyrt:dunder-op $d)
     (((op $op left) (pyrt:slot-binary $t $op $s (pyrt:base-of $x) $s (pyrt:base-of $x)))
      ((op $op right) (pyrt:slot-binary $t $op $s (pyrt:base-of $x) (pyrt:base-of $x) $s))
      ((op $op inplace) (pyrt:slot-inplace $t $op $o $s (pyrt:base-of $x)))
      ($none (pyrt:call-bound $s $d (cons-atom $x ()))))))
(= (pyrt:slot-binary $t $op $s $o $a $b)
   (if (pyrt:in-domain $t $o) (pyrt:binop-builtin $op $a $b) (pyrt:slot-sequence-or-not $t $op $s $o)))
; a sequence's concatenation refuses another kind with its own message
; (list_concat, tuple_concat, unicode_concat); repetition takes an int
(= (pyrt:slot-sequence-or-not $t $op $s $o)
   (if (pyrt:in-flat $t (list tuple str bytes))
       (case $op
         (((py:Add) (m:err (pyrt:exc "TypeError" (pyrt:concat-refused $t $o))))
          ((py:Mult) (if (pyrt:intish $o) (pyrt:binop-builtin (py:Mult) $s $o) (m:val (py:NotImplemented))))
          ($other (m:val (py:NotImplemented)))))
       (m:val (py:NotImplemented))))
(= (pyrt:concat-refused $t $o)
   (let $tn (pyrt:type-name-of (py:type $t))
     (if (pyrt:in-flat $t (bytes bytearray))
         (str:concat (str:concat (str:concat "can't concat " (pyrt:type-name $o)) " to ") $tn)
         (str:concat (str:concat (str:concat (str:concat (str:concat "can only concatenate " $tn) " (not ") (pyrt:quoted2 (pyrt:type-name $o))) ") to ") $tn))))
(= (pyrt:quoted2 $x) (str:concat (str:concat "\"" $x) "\""))
; list_inplace_concat, list_inplace_repeat, dict_ior (dict.update's
; argument): S changed, O the result
(= (pyrt:slot-inplace $t $op $o $s $x)
   (case $t
     ((list (case $op
              (((py:Add) (if (pyrt:may-iterate $x)
                             (case (pyrt:elements $x) (((m:val $xs) (let $a (pyrt:list-append-all $s $xs) (m:val $o))) ($e $e)))
                             (m:err (pyrt:exc "TypeError" (str:concat (pyrt:quoted (pyrt:type-name $x)) " object is not iterable")))))
               ((py:Mult) (if (pyrt:intish $x) (let $r (pyrt:list-reset $s (pyrt:repeat-items (pyrt:list-items $s) (pyrt:int-of $x))) (m:val $o)) (m:val (py:NotImplemented))))
               ($other (m:val (py:NotImplemented))))))
      (dict (case $op
              (((py:BitOr) (case (pyrt:dict-fill-from $s $x) (((m:val $u) (m:val $o)) ($e $e))))
               ($other (m:val (py:NotImplemented))))))
      ($other (m:val (py:NotImplemented))))))
(= (pyrt:slot-compare $t $op $s $o)
   (let $ob (pyrt:base-of $o)
     (if (pyrt:in-domain $t $ob)
         (if (pyrt:in-flat $t (dict set frozenset))
             (if (pyrt:in-flat $op ((py:Eq) (py:NotEq))) (pyrt:compare-builtin $op $s $ob) (m:val (py:NotImplemented)))
             ; bytes_richcompare: with any bytes-like object, by their bytes
             (if (== $t bytes)
                 (pyrt:compare-builtin $op (py:Bytes (pyrt:bytes-view-of $s)) (py:Bytes (pyrt:bytes-view-of $ob)))
                 (pyrt:compare-builtin $op $s $ob)))
         (m:val (py:NotImplemented)))))
; the other operand a slot of type T takes (PyLong_Check, PyFloat_Check or an
; int, PyList_Check, ...): its builtin part judged
(= (pyrt:in-domain $t $o)
   (case $t
     ((int (pyrt:intish $o))
      (bool (pyrt:intish $o))
      (float (if (pyrt:is-float $o) true (pyrt:intish $o)))
      (complex (pyrt:is-number $o))
      (str (pyrt:is-str $o))
      (bytes (!= (pyrt:bytes-view-of $o) (m:missing)))
      (list (== (pyrt:kind-of $o) list))
      (tuple (pyrt:is-tuple $o))
      (dict (== (pyrt:kind-of $o) dict))
      (set (pyrt:is-anyset $o))
      (frozenset (pyrt:is-anyset $o))
      ($other false))))
(= (pyrt:slot-unary-by-name $t $d $s)
   (case $d
     (("__neg__" (pyrt:unary-builtin (py:USub) $s))
      ("__pos__" (pyrt:unary-builtin (py:UAdd) $s))
      ("__invert__" (pyrt:unary-builtin (py:Invert) $s))
      ("__abs__" (pyrt:call-builtin-pos abs (cons-atom $s ())))
      ("__int__" (pyrt:call-type int (cons-atom $s ()) ()))
      ("__float__" (pyrt:call-type float (cons-atom $s ()) ()))
      ("__index__" (if (pyrt:intish $s) (m:val (pyrt:int-of $s)) (m:val $s)))
      ("__trunc__" (pyrt:to-int $s))
      ("__floor__" (pyrt:int-by $s floor))
      ("__ceil__" (pyrt:int-by $s ceil))
      ($other (pyrt:call-bound $s $d ())))))
; int.__floor__ and __ceil__ answer the int; float's the floor or the
; ceiling as an int (NaN and the infinities refused, as math.floor)
(= (pyrt:int-by $s $how)
   (if (pyrt:intish $s) (m:val (pyrt:int-of $s))
     (if (float:is-nan $s) (m:err (pyrt:exc "ValueError" "cannot convert float NaN to integer"))
       (if (float:is-inf $s) (m:err (pyrt:exc "OverflowError" "cannot convert float infinity to integer"))
         (m:val (float:to-int (if (== $how floor) (float:floor $s) (float:ceil $s))))))))
(= (pyrt:dunder-op $d)
   (case $d
     (("__add__" (op (py:Add) left)) ("__radd__" (op (py:Add) right)) ("__iadd__" (op (py:Add) inplace))
      ("__sub__" (op (py:Sub) left)) ("__rsub__" (op (py:Sub) right))
      ("__mul__" (op (py:Mult) left)) ("__rmul__" (op (py:Mult) right)) ("__imul__" (op (py:Mult) inplace))
      ("__truediv__" (op (py:Div) left)) ("__rtruediv__" (op (py:Div) right))
      ("__floordiv__" (op (py:FloorDiv) left)) ("__rfloordiv__" (op (py:FloorDiv) right))
      ("__mod__" (op (py:Mod) left)) ("__rmod__" (op (py:Mod) right))
      ("__pow__" (op (py:Pow) left)) ("__rpow__" (op (py:Pow) right))
      ("__lshift__" (op (py:LShift) left)) ("__rlshift__" (op (py:LShift) right))
      ("__rshift__" (op (py:RShift) left)) ("__rrshift__" (op (py:RShift) right))
      ("__and__" (op (py:BitAnd) left)) ("__rand__" (op (py:BitAnd) right))
      ("__or__" (op (py:BitOr) left)) ("__ror__" (op (py:BitOr) right)) ("__ior__" (op (py:BitOr) inplace))
      ("__xor__" (op (py:BitXor) left)) ("__rxor__" (op (py:BitXor) right))
      ($other none))))

; ---------------------------------------------------------------- tp_new and __init__

; T.__new__(CLS, ...): T itself makes its value (a mutable container empty,
; its __init__ fills it); a subclass an object holding that value
(= (pyrt:builtin-new $t $xs $ks)
   (if (== $xs ())
       (m:err (pyrt:exc "TypeError" (str:concat (pyrt:tp-name-of (py:type $t)) ".__new__(): not enough arguments")))
       (let ($cls $rest) (decons-atom $xs) (pyrt:builtin-new-for $t $cls $rest $ks))))
(= (pyrt:builtin-new-for $t $cls $xs $ks)
   (case $cls
     (((py:type $c)
       (if (pyrt:in-mro (py:type $t) (pyrt:mro-of $cls))
           (let $sb (pyrt:static-base $cls)
             (if (pyrt:new-unsafe $t $sb) (pyrt:new-refusal $t $cls $sb)
               (case (pyrt:builtin-new-value $t $xs $ks)
                 (((m:val $base) (m:val (if (== $cls (py:type $t)) $base (pyrt:subclass-instance $cls $base))))
                  ($e $e)))))
           (m:err (pyrt:exc "TypeError" (pyrt:not-subtype-text $t $cls)))))
      ($other (m:err (pyrt:exc "TypeError" (pyrt:not-type-text $t $cls)))))))
; tp_new_wrapper's check: the most derived base of CLS that is not a class
; whose __new__ is written in Python (slot_tp_new) must make its instances
; with T's __new__: a type whose instances calling it does not make refuses
; (its tp_new NULL), one with a __new__ of its own is not safe to bypass
(= (pyrt:static-base $c)
   (case $c
     (((py:type $id)
       (if (pyrt:builtin-class $id) $c
         (if (== (pyrt:type-of (pyrt:mro-lookup $c "__new__")) (py:type staticmethod)) (pyrt:static-base (pyrt:type-base $c)) $c)))
      ($other $c))))
(= (pyrt:new-unsafe $t $sb)
   (if (== $sb (py:type $t)) False
     (if (pyrt:type-no-instances $sb) True
       (not (== (pyrt:mro-lookup $sb "__new__") (pyrt:mro-lookup (py:type $t) "__new__"))))))
(= (pyrt:new-refusal $t $cls $sb)
   (m:err (pyrt:exc "TypeError"
     (if (pyrt:type-no-instances $sb)
         (str:concat (str:concat "cannot create '" (pyrt:tp-name-of $cls)) "' instances")
         (str:concat (str:concat (str:concat (str:concat (str:concat (pyrt:tp-name-of (py:type $t)) ".__new__(") (pyrt:tp-name-of $cls))
                                             ") is not safe, use ") (pyrt:tp-name-of $sb)) ".__new__()")))))
(= (pyrt:not-subtype-text $t $cls)
   (let $tn (pyrt:tp-name-of (py:type $t))
     (let $cn (pyrt:tp-name-of $cls)
       (str:concat (str:concat (str:concat (str:concat $tn ".__new__(") $cn) "): ")
                   (str:concat (str:concat $cn " is not a subtype of ") $tn)))))
(= (pyrt:not-type-text $t $cls)
   (str:concat (str:concat (str:concat (pyrt:tp-name-of (py:type $t)) ".__new__(X): X is not a type object (")
                           (pyrt:type-name $cls)) ")"))
; a mutable container's tp_new makes it empty, whatever the arguments (its
; __init__ reads them); an immutable value is made from them, at once (str(x)
; may run x.__str__)
(= (pyrt:builtin-new-value $t $xs $ks)
   (if (pyrt:in-flat $t (list dict set bytearray)) (pyrt:call-type $t () ()) (pyrt:now (pyrt:call-type $t $xs $ks))))
(= (pyrt:subclass-instance $cls $base)
   (let $o (pyrt:new-object $cls) (let $u (pyrt:set-field $o value $base) $o)))
; list.__init__(iterable=(), /) and set.__init__: the contents made anew from
; the argument, as list(...) and set(...) make them (their messages too);
; dict.__init__(*args, **kwds) updates as dict.update does, removing nothing
(= (pyrt:builtin-init $t $s $args $ks)
   (case $t
     ((list (case (pyrt:now (pyrt:call-type list $args $ks))
              (((m:val $fresh) (let $u (pyrt:list-reset $s (pyrt:list-items $fresh)) (m:val (py:None))))
               ($e $e))))
      (set (case (pyrt:now (pyrt:call-type set $args $ks))
             (((m:val $fresh) (let $c (pyrt:set-clear $s) (case (pyrt:set-update $s $fresh) (((m:val $u) (m:val (py:None))) ($e $e)))))
              ($e $e))))
      (dict (pyrt:dict-update-as $s $args $ks "dict"))
      ($other (m:val (py:None))))))

; a member's name, its qualified name and the class it belongs to
(= (pyrt:bmeth-info (py:bmeth $t $n) $what)
   (case $what
     (("name" (m:val $n))
      ("qualname" (m:val (str:concat (str:concat (pyrt:qualname-of (py:type $t)) ".") $n)))
      ("objclass" (m:val (py:type $t)))
      ; a type's __new__ is bound to the type; a static method to nothing,
      ; and its module is builtins
      ("self" (m:val (if (== (pyrt:bmember-kind $t $n) static) (py:None) (py:type $t))))
      ("module" (m:val (if (== (pyrt:bmember-kind $t $n) static) "builtins" (py:None))))
      ($other (m:val (py:None))))))
(= (pyrt:bmeth-info (py:bmeth-bound $t $n $s) $what)
   (case $what
     (("name" (m:val $n))
      ("qualname" (m:val (pyrt:bound-qualname $s $n)))
      ("objclass" (m:val (py:type $t)))
      ("self" (m:val $s))
      ("module" (m:val (py:None)))
      ($other (m:val (py:None))))))
; a builtin function (PyCFunctionObject of the builtins module) and a native
; method bound to its value: meth_get__name__, meth_get__qualname__,
; meth_get__self__ (a builtin function's self is its module), __module__
(= (pyrt:bmeth-info (py:builtin $b) $what)
   (case $what
     (("name" (m:val (str:symbol->string $b)))
      ("qualname" (m:val (str:symbol->string $b)))
      ("self" (m:val (pyrt:prelude "_builtins_module")))
      ("module" (m:val "builtins"))
      ($other (m:val (py:None))))))
(= (pyrt:bmeth-info (py:method $o $n) $what)
   (case $what
     (("name" (m:val $n))
      ("qualname" (m:val (pyrt:bound-qualname $o $n)))
      ("self" (m:val $o))
      ("module" (m:val (py:None)))
      ($other (m:val (py:None))))))
; meth_get__qualname__: the qualname of SELF's type (of SELF when it is a
; class), a dot, the name
(= (pyrt:bound-qualname $s $n)
   (str:concat (str:concat (pyrt:qualname-of (case $s (((py:type $k) $s) ($o (pyrt:type-of $s))))) ".") $n))
