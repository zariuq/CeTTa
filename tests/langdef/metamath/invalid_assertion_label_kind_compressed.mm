$( Refused: a compressed proof gives an essential hypothesis where the
   frame of ax-mp asks for the floating hypothesis of ph. $)
$c ( ) -> wff |- $.
$v ph ps ch $.
wph $f wff ph $.
wps $f wff ps $.
wch $f wff ch $.
wi $a wff ( ph -> ps ) $.
${
  min $e |- ph $.
  maj $e |- ( ph -> ps ) $.
  ax-mp $a |- ps $.
$}
${
  bad.1 $e |- ph $.
  bad.2 $e |- ( ph -> ps ) $.
  bad $p |- ps $= ( ax-mp ) CBCDE $.
$}
