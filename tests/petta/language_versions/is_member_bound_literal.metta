; With a bound probe and a ground list in which no element repeats,
; is-member answers exactly once, so its Bool can feed and.
(: listed-and-on? (-[det]-> Atom Bool))
(= (listed-and-on? $name) (and (is-member $name (red green blue)) true))

!(test (listed-and-on? green) true)
!(test (listed-and-on? black) false)
