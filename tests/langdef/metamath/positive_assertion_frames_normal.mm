$( Normal proofs that apply an axiom and a theorem whose frames hold
   essential hypotheses; mp2rc applies the normal theorem mp2 by a
   compressed proof.  ch is active at ax-mp but not mandatory there, so
   wch is outside the frame of ax-mp. $)
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
  mp2 $p |- ch $= wps wch wph wps mp2.1 mp2.2 ax-mp mp2.3 ax-mp $.
$}
${
  mpi.1 $e |- ph $.
  mpi.2 $e |- ( ph -> ( ps -> ch ) ) $.
  mpi $p |- ( ps -> ch ) $= wph wps wch wi mpi.1 mpi.2 ax-mp $.
$}
${
  mp2r.1 $e |- ps $.
  mp2r.2 $e |- ( ps -> ch ) $.
  mp2r.3 $e |- ( ch -> ph ) $.
  mp2r $p |- ph $= wps wch wph mp2r.1 mp2r.2 mp2r.3 mp2 $.
  mp2rc $p |- ph $= ( mp2 ) BCADEFG $.
$}
