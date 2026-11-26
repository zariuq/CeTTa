$( Refused: the theorem dj-n, proved by a normal proof, keeps $d z w; it is
   applied to x and y where no $d x y is active. $)
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
  $d z w $.
  dj-n $p |- ( z -> w ) $= wz ww ax-dj $.
$}
bad $p |- ( x -> y ) $= wx wy dj-n $.
