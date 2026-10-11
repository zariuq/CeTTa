!(bind! &kb (new-space))
!(include &kb include_simple)
!(assertEqualToResult (match &kb (inc-simple $x) $x) (ok))
!(assertEqualToResult (match &self (inc-simple $x) $x) ())

!(add-atom &kb (seed))
; Check the error's target by identity; its printed address is not portable.
!(collapse
  (case (include &kb support/import_fail/module.metta)
    (((Error (include $target $file) $reason)
      (let $same (== $target &kb)
        (quote (IncludeFailure $same $reason)))))))
!(collapse (match &kb (seed) (seed)))
!(collapse (match &kb (leaked-before-error) (leaked-before-error)))

!(include &self include_simple)
!(assertEqualToResult (match &self (inc-simple $x) $x) (ok))
