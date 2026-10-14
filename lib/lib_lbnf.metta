!(import! &self langdef)

(: __cetta_lib_lbnf_bootstrap_v1 (-> Atom Atom))
(: __cetta_lib_lbnf_finish_v1 (-> Atom Atom))
(: __cetta_lib_lbnf_document_v1 (-> Atom Atom Atom))
(: __cetta_lib_lbnf_lower_v1 (-> Atom Atom Atom Atom Atom))
(: lbnf:bootstrap-document (-> Atom))
(: lbnf:document-from-ast (-> Atom Atom Atom))
(: lbnf:lower-document (-> Atom Atom Atom Atom Atom))
(: lbnf:finish-read (-> Atom Atom))
(: lbnf:inspect-document (-> Atom Atom))

(= (lbnf:bootstrap-document)
   (function
     (chain (eval (__cetta_lib_lbnf_bootstrap_v1 LbnfReaderV1)) $value (return $value))))

(= (lbnf:document-from-ast $namespace $ast)
   (function
     (chain (eval (__cetta_lib_lbnf_document_v1 $namespace $ast))
       $document (return $document))))

(= (lbnf:lower-document $document $name $start $policy)
   (function
     (chain (eval (__cetta_lib_lbnf_lower_v1 $document $name $start $policy))
       $syntax (return $syntax))))

(= (lbnf:open-reader)
   (let $document (lbnf:bootstrap-document)
     (let $syntax (lbnf:lower-document $document "LbnfReaderV1" "Grammar"
                    (SyntaxLexicalPolicyV1 bnfc-documented))
       (langdef:load-syntax-gslt-parser $syntax))))

(= (lbnf:finish-read $result)
   (function
     (chain (eval (__cetta_lib_lbnf_finish_v1 $result))
       $value (return $value))))

(= (lbnf:read-text $text)
   (let $reader (lbnf:open-reader)
     (let $result (langdef:parse-authored-text $reader $text)
       (let $_closed (langdef:close-authored-parser $reader)
         (lbnf:finish-read $result)))))

(= (lbnf:read-file $path)
   (let $reader (lbnf:open-reader)
     (let $result (langdef:parse-authored-file $reader $path)
       (let $_closed (langdef:close-authored-parser $reader)
         (lbnf:finish-read $result)))))

(= (lbnf:read-text-evidence $text $limit)
   (let $reader (lbnf:open-reader)
     (let $result (langdef:parse-authored-text-evidence $reader $text $limit)
       (let $_closed (langdef:close-authored-parser $reader)
         (lbnf:finish-read $result)))))

(= (lbnf:read-file-evidence $path $limit)
   (let $reader (lbnf:open-reader)
     (let $result (langdef:parse-authored-file-evidence $reader $path $limit)
       (let $_closed (langdef:close-authored-parser $reader)
         (lbnf:finish-read $result)))))

(= (lbnf:inspect-document $document)
   (function
     (chain (eval (__cetta_lib_lbnf_finish_v1 $document))
       $value (return $value))))
