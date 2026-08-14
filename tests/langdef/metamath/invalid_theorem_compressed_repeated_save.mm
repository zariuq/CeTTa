$c |- p $.

ax $a |- p $.

${
  left $e |- p $.
  right $e |- p $.
  merge $a |- p $.
$}

bad $p |- p $= ( ax merge ) AZZDB $.
