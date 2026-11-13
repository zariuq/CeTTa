; Read a program written in a GSLT presentation of MeTTa and write the
; standard text of its atoms to standard output, for `--syntax NAME`.
;   arguments: DIRECTORY NAME FILE
; DIRECTORY holds the presentation modules and their forms. The parser is
; compiled from the module by CeTTa's GLL/GLR machinery; the forms give each
; production its atom. On success the output begins with the line
; "; presentation NAME"; a rejected program writes the parser's diagnosis to
; standard error and nothing to standard output.

!(import! &self system)
!(import! &self fs)
!(import! &self "./presentation.metta")
!(import! &self "MeTTaCStylePresentation.forms.metta")
!(import! &self "MeTTaPythonStylePresentation.forms.metta")

(presentation-module cstyle "MeTTaCStylePresentation.module" "CStyle" "Prog")
; Python style: CeTTa's run-time token route resolves the module's layout.
(presentation-module pystyle "MeTTaPythonStylePresentation.module" "PythonStyle" "Prog")

(= (read-presentation $dir $name $file)
   (match &self (presentation-module $name $module $theory $entry)
     (let* (($loaded (pres:load $name (str:concat $dir (str:concat "/" $module)) $theory $entry))
            ($program (pres:read $name (fs:read-text $file))))
       (case $program
         (((pres:rejected $why)
            (fs:write "/dev/stderr" (str:concat "presentation reader: " (str:concat (repr $why) "\n"))))
          ((pres:ambiguous $n)
            (fs:write "/dev/stderr" (str:concat "presentation reader: ambiguous, readings: " (str:concat (repr $n) "\n"))))
          ($ok
            (fs:write "/dev/stdout"
              (str:concat "; presentation " (str:concat (repr $name) (str:concat "\n" (pres:sexpr-program $program)))))))))))

(= (as-symbol $x) (if (== (get-metatype $x) Symbol) $x (str:string->symbol $x)))
!(let ($dir $name $file) (system:args)
   (read-presentation $dir (as-symbol $name) $file))
