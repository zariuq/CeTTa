(set:profile hol)

; Datatypes declared with parameters, `(T (: A1 S1) ... (: An Sn))`.  The
; parameters are uniform: every occurrence of T in a constructor is T at
; exactly its parameters, in order.  Such a declaration is admitted.  What
; used to answer every declaration with parameters, not admitted yet, now
; answers only where those conditions fail; a declaration that cannot have
; a model is refuted.
!(bind! &thy (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(set:inductive &thy num (u 0) (: zero num) (: suc (-> num num)))

; ---- Admitted: uniform parameters.
!(set:inductive &thy (List (: A set)) set
   (: nil (List A))
   (: cons (-> A (List A) (List A))))
; Several parameters.
!(set:inductive &thy (Pair (: A set) (: B set)) set (: pair (-> A B (Pair A B))))
!(set:inductive &thy (Assoc (: K set) (: V set)) set
   (: anil (Assoc K V))
   (: acons (-> K V (Assoc K V) (Assoc K V))))
; A datatype at a universe takes parameters in it, or in a universe below
; the datatype's sort.
!(set:inductive &thy (Seq (: A (u 0))) (u 0) (: snil (Seq A)) (: scons (-> A (Seq A) (Seq A))))
!(set:inductive &thy (Low (: A (u 0))) set (: low (-> A (Low A))))
; A parameter may stand at a negative position: only the datatype itself
; must occur strictly positively.
!(set:inductive &thy (Pred (: A set)) set (: pred (-> (-> A prop) (Pred A))))
; What is published: List takes its parameter to the sort, and each
; constructor takes the parameters first, then its fields, named.
!(match &thy (: List $t) $t)
!(match &thy (: cons $t) $t)
!(match &thy (: pair $t) $t)
!(match &thy (: Seq $t) $t)
; With two parameters: the projections of a pair, by cases on it, typed and
; computed at two instances; and the keys of an association list, by
; recursion.  The space is &thy, where the judgments compute them.
!(set:define &thy first (-> (A B : set) (p : (Pair A B)) A)
   (= (first $A $B (pair $A $B $a $b)) $a))
!(set:define &thy second (-> (A B : set) (p : (Pair A B)) B)
   (= (second $A $B (pair $A $B $a $b)) $b))
!(type:of &thy (first num (List num) (pair num (List num) zero (nil num))))
!(type:eq &thy (second num (List num) (pair num (List num) zero (cons num zero (nil num))))
               (cons num zero (nil num)))
!(type:eq &thy (first (List num) num (pair (List num) num nil (suc zero))) (nil num))
!(set:define &thy keys (-> (K V : set) (m : (Assoc K V)) (List K))
   (= (keys $K $V anil) nil)
   (= (keys $K $V (acons $k $v $m)) (cons $k (keys $K $V $m))))
!(type:eq &thy (keys num (List num) (acons zero nil (acons (suc zero) (cons zero nil) anil)))
               (cons zero (cons (suc zero) nil)))

; ---- Legal, not admitted yet.
; The datatype at other arguments inside its own constructors: a nested
; occurrence,
!(try (set:inductive &thy (Nest (: A set)) set
        (: nnil (Nest A))
        (: nwrap (-> (Nest (Nest A)) (Nest A)))))
; and the parameters in another order.
!(try (set:inductive &thy (Swap (: A set) (: B set)) set
        (: snil2 (Swap A B))
        (: swap (-> (Swap B A) (Swap A B)))))
; The datatype inside another type, the elements of a box:
!(set:inductive &thy (Box (: A set)) set (: box (-> A (Box A))))
!(try (set:inductive &thy (Rose (: A set)) set (: rose (-> A (Box (Rose A)) (Rose A)))))
; the datatype as the result of a function field;
!(try (set:inductive &thy (Ord (: A set)) set
        (: ozero (Ord A))
        (: olim (-> (-> A (Ord A)) (Ord A)))))
; a constructor returning the datatype at another argument, an indexed
; family;
!(try (set:inductive &thy (Idx (: A set)) set
        (: inil (Idx num))
        (: icons (-> A (Idx A) (Idx A)))))
; a parameter whose type is not a sort, a parameter whose sort mentions
; another parameter;
!(try (set:inductive &thy (Fin (: n num)) set (: fz (Fin n))))
!(try (set:inductive &thy (Dep (: A set) (: a A)) set (: dep (Dep A a))))
; a parameter in a sort above the datatype's that no field uses.  The level
; rule of the set model, a datatype at a universe takes parameters in it,
; does not hold; nothing refutes the declaration.
!(try (set:inductive &thy (Tag (: A set)) (u 0) (: tag (Tag A))))

; ---- Refuted.
; The datatype at a negative position, at its parameter.
!(try (set:inductive &thy (Neg (: A set)) set (: mk (-> (-> (Neg A) A) (Neg A)))))
; The datatype at another number of arguments than it has parameters is no
; type: in a field, alone in a field, and as a result.
!(try (set:inductive &thy (Two (: A set)) set
        (: tnil (Two A))
        (: tcons (-> A (Two A A) (Two A)))))
!(try (set:inductive &thy (Bare (: A set)) set
        (: bnil (Bare A))
        (: bcons (-> A Bare (Bare A)))))
!(try (set:inductive &thy (Res (: A set)) set (: rnil Res)))
; A parameter of a sort above the datatype's, used in a field: the field is
; not of the datatype's sort.
!(try (set:inductive &thy (Big (: A (u 1))) (u 0) (: big (-> A (Big A)))))
!(try (set:inductive &thy (Small (: A set)) (u 0) (: small (-> A (Small A)))))
; A parameter that is not a typed name, and two parameters of one name.
!(try (set:inductive &thy (Plain A) set (: plain (Plain A))))
!(try (set:inductive &thy (Twice (: A set) (: A set)) set (: twice (Twice A A))))
; Nothing of a refused declaration is published.
!(== (collapse (match &thy (: Neg $type) $type){$type}) ())
!(== (collapse (match &thy (: Nest-rec $type) $type){$type}) ())

; ---- The admitted type former at the wrong arity.
!(try (type:formed &thy (List num num)))
!(try (type:formed &thy (List)))
!(try (type:check &thy (nil num num) (List num)))
!(type:formed &thy (List (Pair num (List num))))
