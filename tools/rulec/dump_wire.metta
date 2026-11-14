; Front end of the rule compiler: CeTTa's own module reader is the only reader of the module.
; It reads MODULE through the baked "module-core-reader", assembles theory THEORY, takes its core
; (LanguageDefV2) and prints every row of the core, one per line, in the core's order:
;   (wire-module NAME THEORY)
;   (TypeDecl ...)              one per sort
;   (GrammarRuleV2 ...)         one per constructor
;   (ScopedRewriteRuleV1 ...)   one per rule
;   (wire-end NTYPES NTERMS NEQUATIONS NREWRITES)
; Nothing is interpreted here; the compiler parses these rows.
;
; Usage: the driver writes a one-line file that imports this file and calls
;   !(wire:dump "MODULE-PATH" "THEORY")

!(import! &self "../../lib/lib_module.metta")

(= (wire:rows LNil $n) $n)
(= (wire:rows (LCons $x $rest) $n) (let $_ (println! $x) (wire:rows $rest (+ $n 1))))

(= (wire:dump $path $theory)
   (let* (($reader (langdef:load-baked-parser "module-core-reader"))
          ($module (module:read-file $reader "module" $path))
          ($closed (langdef:close-parser $reader))
          ($assembled (module:assemble $module $theory LNil LNil OriginViewV1 "rule-compiler:front"))
          ($core (module:core $assembled $theory)))
     (case $core
       (((LanguageDefV2 $name $types $terms $eqs $rws)
         (let* (($h (println! (wire-module $name $theory)))
                ($nt (wire:rows $types 0))
                ($nc (wire:rows $terms 0))
                ($ne (wire:rows $eqs 0))
                ($nr (wire:rows $rws 0)))
           (println! (wire-end $nt $nc $ne $nr))))
        ($other (println! (wire-refused $other)))))))
