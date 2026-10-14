(= (items) 1)
(= (items) 2)
; Printed identifiers cannot manufacture a live continuation capability.
!(select 1 (search-policy (control FIFO (budget 20)))
   (NativeHandle "prime-search" 99))
; Changing source equations invalidates the retained snapshot.
!(let (Observation $p (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))
   (select 1 (search-policy (control FIFO (retain True))) (items))
   (let $ignored (add-atom &self (= (items) 3))
     (select 0 (search-policy (control FIFO (demand all) (retain True))) $r)))
; A priority cannot acquire host-effect authority from its control descriptor.
!(select 1 (search-policy (control WO
   (key $candidate (println! "GRADE-EFFECT-RAN")) (budget 100))) (items))

; A joint collection has an absolute cardinality.  These combinations have
; no declared joint-selection meaning and remain ordinary uninterpreted data.
!(select 2 (search-policy (control FIFO (goal $xs True) (demand all))) (items))
!(select 2 (search-policy (control FIFO (goal $xs True) (demand additional))) (items))
