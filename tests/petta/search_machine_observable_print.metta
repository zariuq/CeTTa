; PeTTa observables follow the SWI syntax: quoted literal backslashes are
; escaped, and finite floats use the shortest round-tripping decimal.
!(test "path\\file" "path\\file")
!(test (/ 2.0 12.0) 0.16666666666666666)
!(println! 0.35000000000000003)
