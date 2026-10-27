; ST1 is source syntax, not an executable command or a binding proof. Every
; form's variable inventory is part of the charged representation. Source
; ordinals preserve identity relationships without exporting process VarIds.
!(import! &self (library syntax))
!(import! &self "lib/petta/lib_binary_schema.metta")

(= (source-codec:term) (Either (Syntax:Variable Syntax:Literal Syntax:Expr)))
(= (source-codec:schema)
   ((Binary:Form 1 Syntax:Document (Text (Record Syntax:Reader) (Vector (Record Syntax:Form))))
    (Binary:Form 2 Syntax:Reader (Text Text Text))
    (Binary:Form 3 Syntax:Form
      ((Vector (Either (Syntax:PlainName Syntax:NameKey))) (Either (Syntax:Variable Syntax:Literal Syntax:Expr))))
    (Binary:Form 4 Syntax:PlainName (Text))
    (Binary:Form 5 Syntax:NameKey ((Either (Syntax:Variable Syntax:Literal Syntax:Expr))))
    (Binary:Form 6 Syntax:Variable (Natural))
    (Binary:Form 7 Syntax:Literal (Symbol Bytes))
    (Binary:Form 8 Syntax:Expr ((Vector (Either (Syntax:Variable Syntax:Literal Syntax:Expr)))))))

(= (source-codec:valid? $document)
   (== $document (syntax:freeze (syntax:restore $document))))

(= (syntax:pack $document)
   (if (source-codec:valid? $document)
     (binary-schema:encode (source-codec:schema) (Record Syntax:Document) $document)
     (Syntax:Fault InvalidDocument)))

(= (syntax:unpack $bytes)
   (case (binary-schema:decode (source-codec:schema) (Record Syntax:Document) $bytes)
     (((Binary:Value $document)
       (if (source-codec:valid? $document) (Syntax:Value $document) (Syntax:Fault InvalidDocument)))
      ($other $other))))
