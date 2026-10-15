; A repeated element lets a bound probe answer true twice.
(: listed-twice? (-[det]-> Atom Bool))
(= (listed-twice? $name) (is-member $name (red red)))
