; Prime's native higher-order logic, for the HE and PeTTa extended profiles.
;
; A theory space is an ordinary space that selects a profile of the logic:
;   !(bind! &logic (new-space))
;   !(add-atom &logic (hol:profile core))             ; the logic alone
;   !(add-atom &sets (hol:profile megalodon-hotg))     ; HOTG set theory over it
; Every judgment names its theory space first; its other operands are
; syntax, read by Prime's checker and never evaluated here.  The program's
; own space is not a theory space: its type declarations are the dialect's.
;
; Answers are data: a theorem's name when it is published into the theory
; space, a value, True or False; a judgment the checker does not decide
; stays as written.  (hol:try J) answers the verdict of J as data:
; (Established evidence), (Refuted reason), (Undetermined reason) or
; (Incomplete reason).
(: __cetta_lib_hol_judge_v1 (-> Atom %Undefined%))
(: __cetta_lib_hol_try_v1 (-> Atom %Undefined%))
(: hol:proves (-> Atom Atom Atom %Undefined%))
(= (hol:proves $space $proof $statement) (__cetta_lib_hol_judge_v1 (proves $space $proof $statement)))
(: hol:theorem (-> Atom Atom Atom Atom %Undefined%))
(= (hol:theorem $space $name $statement $proof) (__cetta_lib_hol_judge_v1 (theorem $space $name $statement $proof)))
(: hol:axiom (-> Atom Atom Atom %Undefined%))
(= (hol:axiom $space $name $statement) (__cetta_lib_hol_judge_v1 (axiom $space $name $statement)))
(: hol:of (-> Atom Atom %Undefined%))
(= (hol:of $space $term) (__cetta_lib_hol_judge_v1 (of $space $term)))
(: hol:check (-> Atom Atom Atom %Undefined%))
(= (hol:check $space $term $type) (__cetta_lib_hol_judge_v1 (check $space $term $type)))
(: hol:eq (-> Atom Atom Atom %Undefined%))
(= (hol:eq $space $left $right) (__cetta_lib_hol_judge_v1 (eq $space $left $right)))
(: hol:formed (-> Atom Atom %Undefined%))
(= (hol:formed $space $type) (__cetta_lib_hol_judge_v1 (formed $space $type)))
(: hol:define (-> Atom Atom Atom Atom %Undefined%))
(= (hol:define $space $name $type $e1) (__cetta_lib_hol_judge_v1 (define $space $name $type $e1)))
(: hol:define (-> Atom Atom Atom Atom Atom %Undefined%))
(= (hol:define $space $name $type $e1 $e2) (__cetta_lib_hol_judge_v1 (define $space $name $type $e1 $e2)))
(: hol:define (-> Atom Atom Atom Atom Atom Atom %Undefined%))
(= (hol:define $space $name $type $e1 $e2 $e3) (__cetta_lib_hol_judge_v1 (define $space $name $type $e1 $e2 $e3)))
(: hol:define (-> Atom Atom Atom Atom Atom Atom Atom %Undefined%))
(= (hol:define $space $name $type $e1 $e2 $e3 $e4) (__cetta_lib_hol_judge_v1 (define $space $name $type $e1 $e2 $e3 $e4)))
(: hol:inductive (-> Atom Atom Atom %Undefined%))
(= (hol:inductive $space $name $sort) (__cetta_lib_hol_judge_v1 (inductive $space $name $sort)))
(: hol:inductive (-> Atom Atom Atom Atom %Undefined%))
(= (hol:inductive $space $name $sort $c1) (__cetta_lib_hol_judge_v1 (inductive $space $name $sort $c1)))
(: hol:inductive (-> Atom Atom Atom Atom Atom %Undefined%))
(= (hol:inductive $space $name $sort $c1 $c2) (__cetta_lib_hol_judge_v1 (inductive $space $name $sort $c1 $c2)))
(: hol:inductive (-> Atom Atom Atom Atom Atom Atom %Undefined%))
(= (hol:inductive $space $name $sort $c1 $c2 $c3) (__cetta_lib_hol_judge_v1 (inductive $space $name $sort $c1 $c2 $c3)))
(: hol:inductive (-> Atom Atom Atom Atom Atom Atom Atom %Undefined%))
(= (hol:inductive $space $name $sort $c1 $c2 $c3 $c4) (__cetta_lib_hol_judge_v1 (inductive $space $name $sort $c1 $c2 $c3 $c4)))
(: hol:recheck (-> Atom Atom %Undefined%))
(= (hol:recheck $space $name) (__cetta_lib_hol_judge_v1 (recheck $space $name)))
(: hol:known-proof (-> Atom Atom %Undefined%))
(= (hol:known-proof $space $name) (__cetta_lib_hol_judge_v1 (known-proof $space $name)))
(: hol:known-proposition (-> Atom Atom %Undefined%))
(= (hol:known-proposition $space $name) (__cetta_lib_hol_judge_v1 (known-proposition $space $name)))
(: hol:signature (-> Atom %Undefined%))
(= (hol:signature $space) (__cetta_lib_hol_judge_v1 (signature $space)))
(: hol:signature (-> Atom Atom %Undefined%))
(= (hol:signature $space $view) (__cetta_lib_hol_judge_v1 (signature $space $view)))
(: hol:signature-digest (-> Atom %Undefined%))
(= (hol:signature-digest $space) (__cetta_lib_hol_judge_v1 (signature-digest $space)))
(: hol:native-proof (-> Atom Atom %Undefined%))
(= (hol:native-proof $space $name) (__cetta_lib_hol_judge_v1 (native-proof $space $name)))
(: hol:native-use (-> Atom Atom Atom %Undefined%))
(= (hol:native-use $space $package $consumer) (__cetta_lib_hol_judge_v1 (native-use $space $package $consumer)))
(: hol:native-normalize (-> Atom Atom Atom %Undefined%))
(= (hol:native-normalize $space $package $consumer) (__cetta_lib_hol_judge_v1 (native-normalize $space $package $consumer)))
(: hol:try (-> Atom %Undefined%))
(= (hol:try $judgment) (__cetta_lib_hol_try_v1 $judgment))
