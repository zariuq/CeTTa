$( Refused: ax-dj is applied with x := ( z -> w ) and y := w, so both
   substituted expressions hold w; every $d among the distinct variables
   is active. $)
$c ( ) -> wff |- $.
$v x y z w $.
wx $f wff x $.
wy $f wff y $.
wz $f wff z $.
ww $f wff w $.
wi $a wff ( x -> y ) $.
${
  $d x y $.
  ax-dj $a |- ( x -> y ) $.
$}
${
  $d x z $. $d x w $. $d z w $.
  bad $p |- ( ( z -> w ) -> w ) $= wz ww wi ww ax-dj $.
$}
