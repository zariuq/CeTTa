$( An axiom and two theorems whose $d conditions matter, applied with
   substitutions that keep them, by normal and compressed proofs. $)
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
  dj-c $p |- ( z -> w ) $= ( ax-dj ) ABC $.
$}
${
  $d x y $.
  dj-nn $p |- ( x -> y ) $= wx wy dj-n $.
  dj-cc $p |- ( x -> y ) $= ( dj-c ) ABC $.
$}
${
  $d x z $. $d x w $.
  dj-e $p |- ( ( z -> w ) -> x ) $= wz ww wi wx ax-dj $.
  dj-ec $p |- ( ( z -> w ) -> x ) $= ( wi ax-dj ) BCDAE $.
$}
