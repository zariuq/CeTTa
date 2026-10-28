; Inspection of the existing canonical Pattern packets. These functions do
; not establish declaration formation or authorize computation equations.
!(import! &self ../../../../Mettapedia/MettaKernel/kernel/generic_open_rule_programs.metta)

(= (family:entries (quote (PApp "GeneratedFamily"
       (LCons (PApp "Declarations" $entries) (LCons $rows LNil)))))
   (quote $entries){$entries})
(= (family:rows (quote (PApp "GeneratedFamily"
       (LCons $entries (LCons (PApp "Computations" $rows) LNil)))))
   (quote $rows){$rows})
(= (family:count (quote $rows)) (gic-list-length $rows))
(= (family:at (quote $rows) $index)
   (case (gic-list-get $rows $index)
     (((GICListValue $value) (Some (quote $value){$value}))
      ($_ None))))
(= (family:row-view (quote (PApp "Computation"
       (LCons $arity (LCons $context (LCons $left (LCons $right (LCons $type LNil))))))))
   (quote (ComputationView $arity $context $left $right $type)){$arity $context $left $right $type})
(= (family:entry-view (quote (PApp "Declaration"
       (LCons $name (LCons $type (LCons $body LNil))))))
   (quote (DeclarationView $name $type $body)){$name $type $body})
(= (family:drop-first-row (quote (PApp "GeneratedFamily"
       (LCons $entries (LCons (PApp "Computations" (LCons $first $rest)) LNil)))))
   (quote (PApp "GeneratedFamily"
       (LCons $entries (LCons (PApp "Computations" $rest) LNil)))){$entries $rest})
