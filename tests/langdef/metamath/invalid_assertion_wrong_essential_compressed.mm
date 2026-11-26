$( Refused: the theorem mp2 is applied with |- ch where its frame asks for
   |- ph. $)
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
  mp2.1 $e |- ph $.
  mp2.2 $e |- ( ph -> ps ) $.
  mp2.3 $e |- ( ps -> ch ) $.
  mp2 $p |- ch $= ( ax-mp ) BCABDEGFG $.
$}
${
  bad.1 $e |- ch $.
  bad.2 $e |- ( ph -> ps ) $.
  bad.3 $e |- ( ps -> ch ) $.
  bad $p |- ch $= ( mp2 ) ABCDEFG $.
$}
