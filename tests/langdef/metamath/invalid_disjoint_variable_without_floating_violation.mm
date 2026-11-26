$( Refused: in the scope of a $d that names z, which has no floating
   hypothesis, ax is applied with x := x and y := x. $)
$c wff |- $.
$v x y z $.
wx $f wff x $.
wy $f wff y $.
$d x z $.
${
  $d x y $.
  ax $a |- x y $.
$}
bad $p |- x x $= wx wx ax $.
