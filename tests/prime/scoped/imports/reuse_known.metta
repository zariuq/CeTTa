; Imported by import_admissions.metta: this file reuses the fact its importer
; published, and publishes a theorem proved from it.
!(let $r (try (set:proves (pf:known self-imp) (imp (In Empty Empty) (In Empty Empty))))
   (add-atom &self (InsideImport (car-atom $r))))
!(let $r (try (set:proves (pf:known forged) (imp (In Empty Empty) (In Empty Empty))))
   (add-atom &self (ForgedInsideImport (car-atom $r))))
!(set:theorem reused (imp (In Empty Empty) (In Empty Empty)) (pf:known self-imp))
