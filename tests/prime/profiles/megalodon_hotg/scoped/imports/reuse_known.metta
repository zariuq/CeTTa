(hol:profile megalodon-hotg)

; Imported by import_admissions.metta: this file reuses the fact its importer
; published, and publishes a theorem proved from it.
!(let $r (try (hol:proves (pf:known self-imp) (imp (In Empty Empty) (In Empty Empty))))
   (let $verdict (car-atom $r)
     (add-atom &self (InsideImport $verdict))))
!(let $r (try (hol:proves (pf:known forged) (imp (In Empty Empty) (In Empty Empty))))
   (let $verdict (car-atom $r)
     (add-atom &self (ForgedInsideImport $verdict))))
!(hol:theorem reused (imp (In Empty Empty) (In Empty Empty)) (pf:known self-imp))
