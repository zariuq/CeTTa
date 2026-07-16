; Atom is the expected-side value top, while %Undefined% remains dynamically
; consistent in either direction. An actual Atom cannot satisfy a concrete
; expected type merely by being maximally imprecise.
(: Nat Type)
(: Accepted Type)
(: DynamicAccepted Type)

(: actual-atom Atom)
(: actual-nat Nat)
(: actual-dynamic %Undefined%)

(: takes-nat (-> Nat Accepted))
(: takes-atom (-> Atom Accepted))
(: takes-dynamic (-> %Undefined% DynamicAccepted))

; Negative: value-top does not flow down into a concrete type.
!(assertEqualToResult (get-type (takes-nat actual-atom)) ())

; Positive: every concrete actual type flows up to expected Atom.
!(assertEqual (get-type (takes-atom actual-nat)) Accepted)

; Positive: the dynamic type stays consistent as either actual or expected.
!(assertEqual (get-type (takes-nat actual-dynamic)) Accepted)
!(assertEqual (get-type (takes-dynamic actual-atom)) DynamicAccepted)

; An extracted element is dynamically unknown, not value-top. This upstream
; signature keeps ordinary dynamic consumers such as arithmetic available.
!(assertEqual (get-type car-atom) (-> Expression %Undefined%))
!(assertEqual (get-type (+ (car-atom (1 2)) 3)) Number)
