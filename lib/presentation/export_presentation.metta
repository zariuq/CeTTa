; Write a presentation module's syntax, as CeTTa assembles it, for the
; build-time presentation compiler (tools/gslt_presentation_compile_v1.c).
;   arguments: MODULE THEORY ENTRY OUT
; The written data is langdef:inspect-syntax-gslt of the module's language:
; its grammar rules after list and coercion expansion, and its token layer.
; The same assembly feeds the run-time GLL route, so both routes read one
; grammar.

!(import! &self (library lib_module))
!(import! &self system)
!(import! &self fs)
!(import! &self str)

; command-line words may arrive as symbols
(= (as-string $x) (if (== (get-metatype $x) Symbol) (str:symbol->string $x) $x))

(= (export-presentation $path $theory $entry $out)
   (let* (($reader (langdef:load-baked-parser "module-core-reader"))
          ($module (module:read-file $reader "presentation" $path))
          ($closed (langdef:close-parser $reader))
          ($th (module:assemble $module $theory LNil LNil OriginViewV1 "presentation"))
          ($language (module:language $th "Presentation" $entry (SyntaxLexicalPolicyV1 bnfc-documented)))
          ($syntax (module:language-syntax $language))
          ($insp (langdef:inspect-syntax-gslt $syntax)))
     (case $insp
       (((SyntaxGSLTInspectionV1 $wire $a $b $c)
          (fs:write $out (str:concat (repr $insp) "\n")))
        ($other
          (fs:write "/dev/stderr" (str:concat "export-presentation: " (str:concat (repr $other) "\n"))))))))

!(let ($path $theory $entry $out) (system:args)
   (export-presentation (as-string $path) (as-string $theory) (as-string $entry) (as-string $out)))
