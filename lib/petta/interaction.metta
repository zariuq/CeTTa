; Interaction on the PeTTa lane: the same commit engine as the Prime lane
; (src/interaction_core.c), driven by this library.  Import with
; (import! &self interaction).  The sources and the template are syntax;
; values read from the store reach code quoted, as lib_transform quotes
; them.  Do not import it together with lib_transform: both define
; `transform`.
;
;   (transform R SOURCES TEMPLATE)        fire now, or no answer
;   (transform R SOURCES TEMPLATE DEST)   the instances are made in DEST
;
; The source and template grammar is the Prime lane's (lib/prime/interaction.metta).

(: __cetta_lib_interaction_fire-lane (-> Atom Atom Atom Atom %Undefined%))
(: __cetta_lib_interaction_fire-lane (-> Atom Atom Atom Atom Atom %Undefined%))
(: transform (-> Atom Atom Atom %Undefined%))
(: transform (-> Atom Atom Atom Atom %Undefined%))
; A native call's arguments are code on this lane: sources and template go
; quoted, and the engine unquotes them.
(= (transform $r $s $t)
   (ia-finish (ia-run (__cetta_lib_interaction_fire-lane petta $r (quote $s) (quote $t)))))
(= (transform $r $s $t $d)
   (ia-finish (ia-run (__cetta_lib_interaction_fire-lane petta $r (quote $s) (quote $t) $d))))

; The engine stops only to have code run. Compute evaluates M's stored request
; and retains its reported Space reads before handing the values back.
(= (ia-run $i)
   (case $i
     (((ia:need $m $e) (ia-run (__cetta_lib_interaction_compute $m)))
      ($done $done))))

(= (ia-finish $i)
   (case $i
     (((ia:done (ia:fired $x)) (eval $x))
      ((ia:done ia:none) (empty))
      ((ia:done $e) $e))))
