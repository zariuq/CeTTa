; Generic runtime surface for compiled compositional GSLT language definitions.
; A manifest selects only generated artifacts and their digest lock.  All
; language-specific syntax and transformations remain data below langdef/.

(= (langdef:load $manifest)
   (function
     (case (__cetta_lib_langdef_load $manifest)
       (((return $handle) (return $handle))
        ($result (return $result))))))

(= (langdef:close $handle)
   (function
     (case (__cetta_lib_langdef_close $handle)
       (((return $result) (return $result))
        ($result (return $result))))))

(= (langdef:info $handle)
   (function
     (case (__cetta_lib_langdef_info $handle)
       (((return $result) (return $result))
        ($result (return $result))))))

(= (langdef:parse-text $handle $source)
   (function
     (case (__cetta_lib_langdef_parse_text $handle $source)
       (((return $result) (return $result))
        ($result (return $result))))))

(= (langdef:parse-file $handle $path)
   (function
     (case (__cetta_lib_langdef_parse_file $handle $path)
       (((return $result) (return $result))
        ($result (return $result))))))

(= (langdef:prepare-import-file $handle $path)
   (function
     (case (__cetta_lib_langdef_prepare_import_file $handle $path)
       (((return $result) (return $result))
        ($result (return $result))))))

(= (langdef:import-identity-file $handle $path)
   (function
     (case (__cetta_lib_langdef_import_identity_file $handle $path)
       (((return $result) (return $result))
        ($result (return $result))))))

; The native syntax-import surface is retained as an explicit diagnostic seam.
; Language users normally call langdef:import-file, whose generated entry
; transforms syntax before anything is published.
(= (langdef:import-syntax-file $handle $path)
   (function
     (case (__cetta_lib_langdef_import_syntax_file $handle $path)
       (((return $result) (return $result))
        ($result (return $result))))))

(= (langdef:import-entry $handle)
   (function
     (case (__cetta_lib_langdef_import_entry $handle)
       (((return $entry) (return $entry))
        ($result (return $result))))))

(= (langdef:publish $handle $protocol)
   (function
     (case (__cetta_lib_langdef_publish $handle $protocol)
       (((return $result) (return $result))
        ($result (return $result))))))

; Recognition and generated semantic replay finish before the target space
; changes. Rejection leaves the target untouched; acceptance publishes the
; generated constructs as one transaction.
(= (langdef:import-file $handle $path)
   (function
     (let $entry (langdef:import-entry $handle)
       (case $entry
         ((LangDef.ImportIdentity
           (let $published
             (langdef:import-identity-file $handle $path)
             (return $published)))
          ($_
           (let $parsed (langdef:prepare-import-file $handle $path)
             (case $parsed
               (((LangDef.ParseAccepted $name $digest $values)
                 (let $protocol (eval ($entry $parsed))
                   (return (langdef:publish $handle $protocol))))
                ($_ (return $parsed)))))))))))

; Generic projection from SyntaxCoreV1's cp/pair/cons carrier to one symbol.
; Language stages use this instead of reimplementing UTF-8 assembly.
(= (langdef:codepoints->symbol $codepoints)
   (function
     (case (__cetta_lib_langdef_codepoints_symbol $codepoints)
       (((return $result) (return $result))
        ($result (return $result))))))
