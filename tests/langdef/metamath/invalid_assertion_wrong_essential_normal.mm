$( Refused: the minor premise given to ax-mp is |- ps where its frame asks
   for |- ph. $)
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
  bad.1 $e |- ps $.
  bad.2 $e |- ( ph -> ps ) $.
  bad $p |- ps $= wph wps bad.1 bad.2 ax-mp $.
$}
