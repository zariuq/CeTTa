; Native HOL Light kernel. Import with (import! &self holkernel).
; Sessions default to standard axioms; (hol:kernel open) explicitly allows user axioms.
; Checking does not record derivations; (hol:kernel standard record) retains them for export.
; Canonical type/term data is held as written. Use let to supply a prepared value.
; Sessions, terms and theorems are opaque seals; printed names carry no authority.
; See docs/hol_light.txt for the kernel, ownership and observation contracts.

(: hol:kernel (-> Atom))
(= (hol:kernel)
   (function (chain (eval (__cetta_lib_hol_kernel standard)) $result (return $result))))

(: hol:kernel (-> %Undefined% Atom))
(: __cetta_lib_hol_kernel (-> Atom Atom))
(= (hol:kernel $policy)
   (function (chain (eval (__cetta_lib_hol_kernel $policy)) $result (return $result))))

(: hol:kernel (-> %Undefined% %Undefined% Atom))
(: __cetta_lib_hol_kernel (-> Atom Atom Atom))
(= (hol:kernel $policy $mode)
   (function (chain (eval (__cetta_lib_hol_kernel $policy $mode)) $result (return $result))))

(: hol:stats (-> %Undefined% Atom))
(: __cetta_lib_hol_stats (-> Atom Atom))
(= (hol:stats $k)
   (function (chain (eval (__cetta_lib_hol_stats $k)) $result (return $result))))

(: hol:check-dag (-> %Undefined% Atom %Undefined% Atom))
(: __cetta_lib_hol_check-dag (-> Atom Atom Atom Atom))
(= (hol:check-dag $k $rows $root)
   (function (chain (eval (__cetta_lib_hol_check-dag $k $rows $root)) $result (return $result))))

(: hol:budget (-> %Undefined% %Undefined% Atom))
(: __cetta_lib_hol_budget (-> Atom Atom Atom))
(= (hol:budget $k $budget)
   (function (chain (eval (__cetta_lib_hol_budget $k $budget)) $result (return $result))))

(: hol:replay (-> %Undefined% %Undefined% Atom))
(: __cetta_lib_hol_replay (-> Atom Atom Atom))
(= (hol:replay $k $path)
   (function (chain (eval (__cetta_lib_hol_replay $k $path)) $result (return $result))))

(: hol:replay (-> %Undefined% %Undefined% %Undefined% Atom))
(: __cetta_lib_hol_replay (-> Atom Atom Atom Atom))
(= (hol:replay $k $path $policy)
   (function (chain (eval (__cetta_lib_hol_replay $k $path $policy)) $result (return $result))))

(: hol:theorem (-> %Undefined% %Undefined% Atom))
(: __cetta_lib_hol_theorem (-> Atom Atom Atom))
(= (hol:theorem $k $label)
   (function (chain (eval (__cetta_lib_hol_theorem $k $label)) $result (return $result))))

(: hol:sequent (-> %Undefined% Atom))
(: __cetta_lib_hol_sequent (-> Atom Atom))
(= (hol:sequent $th)
   (function (chain (eval (__cetta_lib_hol_sequent $th)) $result (return $result))))

(: hol:axioms (-> %Undefined% Atom))
(: __cetta_lib_hol_axioms (-> Atom Atom))
(= (hol:axioms $th)
   (function (chain (eval (__cetta_lib_hol_axioms $th)) $result (return $result))))

(: hol:standard (-> %Undefined% Atom))
(: __cetta_lib_hol_standard (-> Atom Atom))
(= (hol:standard $th)
   (function (chain (eval (__cetta_lib_hol_standard $th)) $result (return $result))))

(: hol:text (-> %Undefined% Atom))
(: __cetta_lib_hol_text (-> Atom Atom))
(= (hol:text $th)
   (function (chain (eval (__cetta_lib_hol_text $th)) $result (return $result))))

(: hol:event (-> %Undefined% Atom))
(: __cetta_lib_hol_event (-> Atom Atom))
(= (hol:event $th)
   (function (chain (eval (__cetta_lib_hol_event $th)) $result (return $result))))

(: hol:same-payload (-> %Undefined% %Undefined% Atom))
(: __cetta_lib_hol_same-payload (-> Atom Atom Atom))
(= (hol:same-payload $th1 $th2)
   (function (chain (eval (__cetta_lib_hol_same-payload $th1 $th2)) $result (return $result))))

(: hol:refl (-> %Undefined% Atom Atom))
(: __cetta_lib_hol_refl (-> Atom Atom Atom))
(= (hol:refl $k $tm)
   (function (chain (eval (__cetta_lib_hol_refl $k $tm)) $result (return $result))))

(: hol:trans (-> %Undefined% %Undefined% Atom))
(: __cetta_lib_hol_trans (-> Atom Atom Atom))
(= (hol:trans $th1 $th2)
   (function (chain (eval (__cetta_lib_hol_trans $th1 $th2)) $result (return $result))))

(: hol:mk-comb (-> %Undefined% %Undefined% Atom))
(: __cetta_lib_hol_mk-comb (-> Atom Atom Atom))
(= (hol:mk-comb $th1 $th2)
   (function (chain (eval (__cetta_lib_hol_mk-comb $th1 $th2)) $result (return $result))))

(: hol:abs (-> %Undefined% Atom %Undefined% Atom))
(: __cetta_lib_hol_abs (-> Atom Atom Atom Atom))
(= (hol:abs $k $v $th)
   (function (chain (eval (__cetta_lib_hol_abs $k $v $th)) $result (return $result))))

(: hol:beta (-> %Undefined% Atom Atom))
(: __cetta_lib_hol_beta (-> Atom Atom Atom))
(= (hol:beta $k $tm)
   (function (chain (eval (__cetta_lib_hol_beta $k $tm)) $result (return $result))))

(: hol:assume (-> %Undefined% Atom Atom))
(: __cetta_lib_hol_assume (-> Atom Atom Atom))
(= (hol:assume $k $tm)
   (function (chain (eval (__cetta_lib_hol_assume $k $tm)) $result (return $result))))

(: hol:eq-mp (-> %Undefined% %Undefined% Atom))
(: __cetta_lib_hol_eq-mp (-> Atom Atom Atom))
(= (hol:eq-mp $th1 $th2)
   (function (chain (eval (__cetta_lib_hol_eq-mp $th1 $th2)) $result (return $result))))

(: hol:deduct-antisym (-> %Undefined% %Undefined% Atom))
(: __cetta_lib_hol_deduct-antisym (-> Atom Atom Atom))
(= (hol:deduct-antisym $th1 $th2)
   (function (chain (eval (__cetta_lib_hol_deduct-antisym $th1 $th2)) $result (return $result))))

(: hol:inst (-> %Undefined% Atom Atom))
(: __cetta_lib_hol_inst (-> Atom Atom Atom))
(= (hol:inst $th $pairs)
   (function (chain (eval (__cetta_lib_hol_inst $th $pairs)) $result (return $result))))

(: hol:inst-type (-> %Undefined% Atom Atom))
(: __cetta_lib_hol_inst-type (-> Atom Atom Atom))
(= (hol:inst-type $th $pairs)
   (function (chain (eval (__cetta_lib_hol_inst-type $th $pairs)) $result (return $result))))

(: hol:new-type (-> %Undefined% %Undefined% %Undefined% Atom))
(: __cetta_lib_hol_new-type (-> Atom Atom Atom Atom))
(= (hol:new-type $k $name $arity)
   (function (chain (eval (__cetta_lib_hol_new-type $k $name $arity)) $result (return $result))))

(: hol:new-constant (-> %Undefined% %Undefined% Atom Atom))
(: __cetta_lib_hol_new-constant (-> Atom Atom Atom Atom))
(= (hol:new-constant $k $name $ty)
   (function (chain (eval (__cetta_lib_hol_new-constant $k $name $ty)) $result (return $result))))

(: hol:new-axiom (-> %Undefined% Atom Atom))
(: __cetta_lib_hol_new-axiom (-> Atom Atom Atom))
(= (hol:new-axiom $k $tm)
   (function (chain (eval (__cetta_lib_hol_new-axiom $k $tm)) $result (return $result))))

(: hol:new-definition (-> %Undefined% Atom Atom))
(: __cetta_lib_hol_new-definition (-> Atom Atom Atom))
(= (hol:new-definition $k $eqn)
   (function (chain (eval (__cetta_lib_hol_new-definition $k $eqn)) $result (return $result))))

(: hol:new-type-definition (-> %Undefined% %Undefined% %Undefined% %Undefined% %Undefined% Atom))
(: __cetta_lib_hol_new-type-definition (-> Atom Atom Atom Atom Atom Atom))
(= (hol:new-type-definition $k $tyname $absname $repname $witness)
   (function (chain (eval (__cetta_lib_hol_new-type-definition $k $tyname $absname $repname $witness)) $result (return $result))))

(: hol:policy (-> %Undefined% %Undefined% Atom))
(: __cetta_lib_hol_policy (-> Atom Atom Atom))
(= (hol:policy $k $policy)
   (function (chain (eval (__cetta_lib_hol_policy $k $policy)) $result (return $result))))

(: hol:checkpoint (-> %Undefined% Atom))
(: __cetta_lib_hol_checkpoint (-> Atom Atom))
(= (hol:checkpoint $k)
   (function (chain (eval (__cetta_lib_hol_checkpoint $k)) $result (return $result))))

(: hol:restore (-> %Undefined% %Undefined% Atom))
(: __cetta_lib_hol_restore (-> Atom Atom Atom))
(= (hol:restore $k $env)
   (function (chain (eval (__cetta_lib_hol_restore $k $env)) $result (return $result))))

(: hol:admits (-> %Undefined% %Undefined% Atom))
(: __cetta_lib_hol_admits (-> Atom Atom Atom))
(= (hol:admits $k $th)
   (function (chain (eval (__cetta_lib_hol_admits $k $th)) $result (return $result))))

(: hol:term (-> %Undefined% Atom Atom))
(: __cetta_lib_hol_term (-> Atom Atom Atom))
(= (hol:term $k $tm)
   (function (chain (eval (__cetta_lib_hol_term $k $tm)) $result (return $result))))

(: hol:type (-> %Undefined% Atom Atom))
(: __cetta_lib_hol_type (-> Atom Atom Atom))
(= (hol:type $k $ty)
   (function (chain (eval (__cetta_lib_hol_type $k $ty)) $result (return $result))))

(: hol:term-data (-> %Undefined% Atom))
(: __cetta_lib_hol_term-data (-> Atom Atom))
(= (hol:term-data $tm)
   (function (chain (eval (__cetta_lib_hol_term-data $tm)) $result (return $result))))

(: hol:type-data (-> %Undefined% Atom))
(: __cetta_lib_hol_type-data (-> Atom Atom))
(= (hol:type-data $ty)
   (function (chain (eval (__cetta_lib_hol_type-data $ty)) $result (return $result))))

(: hol:conclusion (-> %Undefined% Atom))
(: __cetta_lib_hol_conclusion (-> Atom Atom))
(= (hol:conclusion $th)
   (function (chain (eval (__cetta_lib_hol_conclusion $th)) $result (return $result))))

(: hol:aconv (-> %Undefined% Atom Atom Atom))
(: __cetta_lib_hol_aconv (-> Atom Atom Atom Atom))
(= (hol:aconv $k $t1 $t2)
   (function (chain (eval (__cetta_lib_hol_aconv $k $t1 $t2)) $result (return $result))))

(: hol:type-of (-> %Undefined% Atom Atom))
(: __cetta_lib_hol_type-of (-> Atom Atom Atom))
(= (hol:type-of $k $tm)
   (function (chain (eval (__cetta_lib_hol_type-of $k $tm)) $result (return $result))))
