; Prime's set theories, each a theory over its native higher-order logic,
; for the HE and PeTTa extended profiles.
;
; A theory space is an ordinary space that selects its theory:
;   !(bind! &sets (new-space))
;   !(add-atom &sets (set:profile prime-common))       ; the default theory
;   !(bind! &hotg (new-space))
;   !(add-atom &hotg (set:profile megalodon-hotg))     ; HOTG, opt-in
;   !(set:signature &hotg base-logic)                  ; (hol:profile base)
; names the logic a set theory is over.  A space with no selection is a
; prime-common space.  PeTTa creates a space only when it is first written,
; so there a theory space selects its theory before it is judged.
; Every judgment names its theory space first; its other operands are
; syntax, read by Prime's checker and never evaluated here.  The program's
; own space is not a theory space: its type declarations are the dialect's.
;
; Answers are data: a theorem's name when it is published into the theory
; space, a value, True or False; a judgment the checker does not decide
; stays as written.  (set:try J) answers the verdict of J as data:
; (Established evidence), (Refuted reason), (Undetermined reason) or
; (Incomplete reason).
(: __cetta_lib_set_judge_v1 (-> Atom %Undefined%))
(: __cetta_lib_set_try_v1 (-> Atom %Undefined%))
(: set:proves (-> Atom Atom Atom %Undefined%))
(= (set:proves $space $proof $statement) (__cetta_lib_set_judge_v1 (proves $space $proof $statement)))
(: set:theorem (-> Atom Atom Atom Atom %Undefined%))
(= (set:theorem $space $name $statement $proof) (__cetta_lib_set_judge_v1 (theorem $space $name $statement $proof)))
(: set:axiom (-> Atom Atom Atom %Undefined%))
(= (set:axiom $space $name $statement) (__cetta_lib_set_judge_v1 (axiom $space $name $statement)))
(: set:of (-> Atom Atom %Undefined%))
(= (set:of $space $term) (__cetta_lib_set_judge_v1 (of $space $term)))
(: set:check (-> Atom Atom Atom %Undefined%))
(= (set:check $space $term $type) (__cetta_lib_set_judge_v1 (check $space $term $type)))
(: set:eq (-> Atom Atom Atom %Undefined%))
(= (set:eq $space $left $right) (__cetta_lib_set_judge_v1 (eq $space $left $right)))
(: set:formed (-> Atom Atom %Undefined%))
(= (set:formed $space $type) (__cetta_lib_set_judge_v1 (formed $space $type)))
(: set:define (-> Atom Atom Atom Atom %Undefined%))
(= (set:define $space $name $type $e1) (__cetta_lib_set_judge_v1 (define $space $name $type $e1)))
(: set:define (-> Atom Atom Atom Atom Atom %Undefined%))
(= (set:define $space $name $type $e1 $e2) (__cetta_lib_set_judge_v1 (define $space $name $type $e1 $e2)))
(: set:define (-> Atom Atom Atom Atom Atom Atom %Undefined%))
(= (set:define $space $name $type $e1 $e2 $e3) (__cetta_lib_set_judge_v1 (define $space $name $type $e1 $e2 $e3)))
(: set:define (-> Atom Atom Atom Atom Atom Atom Atom %Undefined%))
(= (set:define $space $name $type $e1 $e2 $e3 $e4) (__cetta_lib_set_judge_v1 (define $space $name $type $e1 $e2 $e3 $e4)))
(: set:inductive (-> Atom Atom Atom %Undefined%))
(= (set:inductive $space $name $sort) (__cetta_lib_set_judge_v1 (inductive $space $name $sort)))
(: set:inductive (-> Atom Atom Atom Atom %Undefined%))
(= (set:inductive $space $name $sort $c1) (__cetta_lib_set_judge_v1 (inductive $space $name $sort $c1)))
(: set:inductive (-> Atom Atom Atom Atom Atom %Undefined%))
(= (set:inductive $space $name $sort $c1 $c2) (__cetta_lib_set_judge_v1 (inductive $space $name $sort $c1 $c2)))
(: set:inductive (-> Atom Atom Atom Atom Atom Atom %Undefined%))
(= (set:inductive $space $name $sort $c1 $c2 $c3) (__cetta_lib_set_judge_v1 (inductive $space $name $sort $c1 $c2 $c3)))
(: set:inductive (-> Atom Atom Atom Atom Atom Atom Atom %Undefined%))
(= (set:inductive $space $name $sort $c1 $c2 $c3 $c4) (__cetta_lib_set_judge_v1 (inductive $space $name $sort $c1 $c2 $c3 $c4)))
(: set:recheck (-> Atom Atom %Undefined%))
(= (set:recheck $space $name) (__cetta_lib_set_judge_v1 (recheck $space $name)))
(: set:known-proof (-> Atom Atom %Undefined%))
(= (set:known-proof $space $name) (__cetta_lib_set_judge_v1 (known-proof $space $name)))
(: set:known-proposition (-> Atom Atom %Undefined%))
(= (set:known-proposition $space $name) (__cetta_lib_set_judge_v1 (known-proposition $space $name)))
(: set:signature (-> Atom %Undefined%))
(= (set:signature $space) (__cetta_lib_set_judge_v1 (signature $space)))
(: set:signature (-> Atom Atom %Undefined%))
(= (set:signature $space $view) (__cetta_lib_set_judge_v1 (signature $space $view)))
(: set:signature-digest (-> Atom %Undefined%))
(= (set:signature-digest $space) (__cetta_lib_set_judge_v1 (signature-digest $space)))
(: set:native-proof (-> Atom Atom %Undefined%))
(= (set:native-proof $space $name) (__cetta_lib_set_judge_v1 (native-proof $space $name)))
(: set:native-use (-> Atom Atom Atom %Undefined%))
(= (set:native-use $space $package $consumer) (__cetta_lib_set_judge_v1 (native-use $space $package $consumer)))
(: set:native-normalize (-> Atom Atom Atom %Undefined%))
(= (set:native-normalize $space $package $consumer) (__cetta_lib_set_judge_v1 (native-normalize $space $package $consumer)))
(: set:try (-> Atom %Undefined%))
(= (set:try $judgment) (__cetta_lib_set_try_v1 $judgment))
