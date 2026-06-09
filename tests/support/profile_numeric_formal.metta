; Formal HE keeps CeTTa's exact numeric tower and floor-division operator.

!(/ 7 3)
!(pragma! prefer-rationals True)
!(/ 7 3)

!(// 7 3)
!(// -7 3)
!(// 7 -3)
!(// -7 -3)
!(get-type //)

!(import! &self math)
!(math.mod -7 3)
!(math.mod 7 -3)
!(math.mod 7/2 3)
