; Run with --lang petta --profile extended or --lang he --profile extended.
!(import! &self "../lib/lib_exec.metta")
!(bind! &work (new-space))
!(add-atom-nodup &work (edge a b))
!(add-atom-nodup &work (edge b c))

; A rule value can bind several premises and publish several templates.
!(add-atom-nodup &work
   (exec paths (, (edge $x $y) (edge $y $z))
      (, (path $x $z) (back $z $x))))
!(exec:step-key &work paths)
!(match &work (path $from $to) (path $from $to))

; Rules can publish more rules. Publication does not execute the new rule.
!(add-atom-nodup &work
   (exec prepare (,) (, (exec tag (, (path $x $y)) (, (tagged $x $y))))))
!(exec:step-key &work prepare)
!(match &work (exec tag $query $templates) ready-to-tag)
!(exec:step-key &work tag)
!(match &work (tagged $from $to) (tagged $from $to))
