$( Compressed proofs that apply an axiom and a theorem whose frames hold
   essential hypotheses; mp2rn applies the compressed theorem mp2 by a
   normal proof. $)
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
  mpi.1 $e |- ph $.
  mpi.2 $e |- ( ph -> ( ps -> ch ) ) $.
  mpi $p |- ( ps -> ch ) $= ( wi ax-mp ) ABCFDEG $.
$}
${
  mp2r.1 $e |- ps $.
  mp2r.2 $e |- ( ps -> ch ) $.
  mp2r.3 $e |- ( ch -> ph ) $.
  mp2r $p |- ph $= ( mp2 ) BCADEFG $.
  mp2rn $p |- ph $= wps wch wph mp2r.1 mp2r.2 mp2r.3 mp2 $.
$}
