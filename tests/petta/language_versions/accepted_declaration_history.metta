; Rejected kind declarations remain data but never acquire typing authority.
(: Stable (Newtype Number))
(: Stable (Newtype String))
!(remove-atom &self (: Stable (Newtype Number)))
!(add-atom &self (: Stable (Alias Number)))
(: take-stable (-> Stable Number))
(= (take-stable $x) $x)
!(test (take-stable 7) 7)
!(test (collapse (match &self (: Stable (Newtype String)) raw-row)) (raw-row))

; Removing a different declaration rechecks the accepted environment.
(: Temporary (Newtype Number))
!(remove-atom &self (: Temporary (Newtype Number)))
!(test (take-stable 8) 8)

; The first kind wins in a block, including SpaceOf and cross-kind repeats.
(: &rows (SpaceOf (Number)))
(: &rows (SpaceOf (String)))
(: &rows (Alias String))
!(add-atom &rows (9))
!(test (collapse (match &rows $row $row)) ((9)))
!(test (collapse (match &self (: &rows (SpaceOf (String))) raw-row)) (raw-row))

; A new type error is still rejected at the residual runtime boundary.
!(test (let $call (quote (take-stable "wrong"))
         (car-atom (catch (eval $call)))) Error)
