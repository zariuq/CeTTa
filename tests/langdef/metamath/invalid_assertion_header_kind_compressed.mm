$( Refused: a compressed proof names an essential hypothesis, which is
   always mandatory, among its labels (Appendix B); the proof is otherwise
   correct. $)
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
  bad $p |- ps $= ( bad.1 ax-mp ) ABEDF $.
$}
