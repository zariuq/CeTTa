; A batch is sequential: a later fault retains completed earlier imports,
; and does not enter later members. The ordinary catch owner handles it.
!(test (car-atom (catch
    (import! &self (str "./missing-explicit-import-fixture.metta" json)))) Error)
!(test (str:starts-with "abc" "a") true)
!(test (json:parse "17") (quote (json:parse "17")))
!(test (import! &self json) true)
!(test (json:parse "17") 17)
