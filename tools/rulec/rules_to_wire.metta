; The core of an exported ModuleRewriteRulesV1 (module-authoring's output) as wire rows, one per line,
; as dump_wire.metta prints a core.  Usage: cetta rules_to_wire.metta (reads system:arg 0).
!(import! &self fs)
!(import! &self system)
(= (wire:rows LNil $n) $n)
(= (wire:rows (LCons $x $rest) $n) (let $_ (println! $x) (wire:rows $rest (+ $n 1))))
!(let* (($path (system:arg 0))
        ($text (fs:read-text $path))
        ($value (parse $text)))
   (case $value
     (((ModuleRewriteRulesV1 (LanguageDefV2 $name $types $terms $eqs $rws) $ordered)
       (let* (($h (println! (wire-module $name "Format")))
              ($nt (wire:rows $types 0)) ($nc (wire:rows $terms 0)) ($ne (wire:rows $eqs 0)) ($nr (wire:rows $rws 0)))
         (println! (wire-end $nt $nc $ne $nr))))
      ($other (println! (wire-refused (car-atom $other)))))))
