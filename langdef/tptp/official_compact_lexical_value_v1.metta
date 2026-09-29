; Semantic values of compact TPTP word and name records.
; Raw spellings remain in the public records; this view removes delimiters
; and decodes the two escapes licensed for single-quoted TPTP words.
(gslt-presentation-v1 TptpOfficialCompactLexicalValueV1
  (signature
    (operator metta-equation 2)
    (operator langdef:ground-equal 2)
    (operator langdef:string->codepoints 1)
    (operator langdef:codepoints->string 1)
    (operator cp 1) (operator cons 2)
    (operator tptp-lex-v1:word-value 1)
    (operator tptp-lex-v1:name-value 1)
    (operator tptp-lex-v1:decode-quoted 2)
    (operator tptp-lex-v1:decode-quoted-start 3)
    (operator tptp-lex-v1:decode-body 2)
    (operator tptp-lex-v1:decode-body-step 5)
    (operator tptp-lex-v1:decode-quote-end 2)
    (operator tptp-lex-v1:decode-escape 2)
    (operator tptp-lex-v1:decode-escape-step 5)
    (operator tptp-lex-v1:prepend 2)
    (operator tptp-lex-v1:finish 1)
    (operator tptp-lex-v1:decode-back 2)
    (operator tptp-lex-v1:decode-back-start 3)
    (operator tptp-lex-v1:codepoints 1)
    (operator tptp-rec:word-lower 1)
    (operator tptp-rec:word-quoted 1)
    (operator tptp-rec:word-back 1)
    (operator tptp-rec:name-word 1)
    (operator tptp-rec:name-integer 1)
    (operator TPTP:LexicalValue 1)
    (operator TPTP:LexicalFault 2))
  (equations)
  (rewrites
    (rule word-lower (head (metta-equation
      (tptp-lex-v1:word-value (tptp-rec:word-lower ?raw))
      (TPTP:LexicalValue ?raw))) (body))
    (rule word-quoted (head (metta-equation
      (tptp-lex-v1:word-value (tptp-rec:word-quoted ?raw))
      (tptp-lex-v1:decode-quoted
        ?raw (langdef:string->codepoints ?raw)))) (body))
    (rule word-back (head (metta-equation
      (tptp-lex-v1:word-value (tptp-rec:word-back ?raw))
      (tptp-lex-v1:decode-back
        ?raw (langdef:string->codepoints ?raw)))) (body))
    (rule name-word (head (metta-equation
      (tptp-lex-v1:name-value (tptp-rec:name-word ?word))
      (tptp-lex-v1:word-value ?word))) (body))
    (rule name-integer (head (metta-equation
      (tptp-lex-v1:name-value (tptp-rec:name-integer ?raw))
      (TPTP:LexicalValue ?raw))) (body))

    (rule quoted-empty (head (metta-equation
      (tptp-lex-v1:decode-quoted ?raw nil)
      (TPTP:LexicalFault missing-opening-quote ?raw))) (body))
    (rule quoted-start (head (metta-equation
      (tptp-lex-v1:decode-quoted ?raw (cons (cp ?first) ?rest))
      (tptp-lex-v1:decode-quoted-start
        (langdef:ground-equal ?first 39) ?raw ?rest))) (body))
    (rule quoted-start-valid (head (metta-equation
      (tptp-lex-v1:decode-quoted-start LangDef:SameValue ?raw ?rest)
      (tptp-lex-v1:finish (tptp-lex-v1:decode-body ?raw ?rest)))) (body))
    (rule quoted-start-invalid (head (metta-equation
      (tptp-lex-v1:decode-quoted-start LangDef:DifferentValue ?raw ?rest)
      (TPTP:LexicalFault missing-opening-quote ?raw))) (body))

    (rule quoted-body-empty (head (metta-equation
      (tptp-lex-v1:decode-body ?raw nil)
      (TPTP:LexicalFault missing-closing-quote ?raw))) (body))
    (rule quoted-body-step (head (metta-equation
      (tptp-lex-v1:decode-body ?raw (cons (cp ?head) ?tail))
      (tptp-lex-v1:decode-body-step
        (langdef:ground-equal ?head 39)
        (langdef:ground-equal ?head 92) ?raw ?head ?tail))) (body))
    (rule quoted-body-end (head (metta-equation
      (tptp-lex-v1:decode-body-step
        LangDef:SameValue ?slash ?raw ?head ?tail)
      (tptp-lex-v1:decode-quote-end ?raw ?tail))) (body))
    (rule quoted-body-escape (head (metta-equation
      (tptp-lex-v1:decode-body-step
        LangDef:DifferentValue LangDef:SameValue ?raw ?head ?tail)
      (tptp-lex-v1:decode-escape ?raw ?tail))) (body))
    (rule quoted-body-plain (head (metta-equation
      (tptp-lex-v1:decode-body-step
        LangDef:DifferentValue LangDef:DifferentValue ?raw ?head ?tail)
      (tptp-lex-v1:prepend
        ?head (tptp-lex-v1:decode-body ?raw ?tail)))) (body))
    (rule quoted-end-valid (head (metta-equation
      (tptp-lex-v1:decode-quote-end ?raw nil)
      (tptp-lex-v1:codepoints nil))) (body))
    (rule quoted-end-trailing (head (metta-equation
      (tptp-lex-v1:decode-quote-end ?raw (cons ?head ?tail))
      (TPTP:LexicalFault trailing-after-closing-quote ?raw))) (body))

    (rule quoted-escape-empty (head (metta-equation
      (tptp-lex-v1:decode-escape ?raw nil)
      (TPTP:LexicalFault incomplete-escape ?raw))) (body))
    (rule quoted-escape-step (head (metta-equation
      (tptp-lex-v1:decode-escape ?raw (cons (cp ?head) ?tail))
      (tptp-lex-v1:decode-escape-step
        (langdef:ground-equal ?head 39)
        (langdef:ground-equal ?head 92) ?raw ?head ?tail))) (body))
    (rule quoted-escape-quote (head (metta-equation
      (tptp-lex-v1:decode-escape-step
        LangDef:SameValue ?slash ?raw ?head ?tail)
      (tptp-lex-v1:prepend
        ?head (tptp-lex-v1:decode-body ?raw ?tail)))) (body))
    (rule quoted-escape-slash (head (metta-equation
      (tptp-lex-v1:decode-escape-step
        LangDef:DifferentValue LangDef:SameValue ?raw ?head ?tail)
      (tptp-lex-v1:prepend
        ?head (tptp-lex-v1:decode-body ?raw ?tail)))) (body))
    (rule quoted-escape-invalid (head (metta-equation
      (tptp-lex-v1:decode-escape-step
        LangDef:DifferentValue LangDef:DifferentValue ?raw ?head ?tail)
      (TPTP:LexicalFault invalid-escape ?raw))) (body))

    (rule prepend-value (head (metta-equation
      (tptp-lex-v1:prepend ?head (tptp-lex-v1:codepoints ?tail))
      (tptp-lex-v1:codepoints (cons (cp ?head) ?tail)))) (body))
    (rule prepend-fault (head (metta-equation
      (tptp-lex-v1:prepend ?head (TPTP:LexicalFault ?kind ?raw))
      (TPTP:LexicalFault ?kind ?raw))) (body))
    (rule finish-value (head (metta-equation
      (tptp-lex-v1:finish (tptp-lex-v1:codepoints ?characters))
      (TPTP:LexicalValue
        (langdef:codepoints->string ?characters)))) (body))
    (rule finish-fault (head (metta-equation
      (tptp-lex-v1:finish (TPTP:LexicalFault ?kind ?raw))
      (TPTP:LexicalFault ?kind ?raw))) (body))

    (rule back-empty (head (metta-equation
      (tptp-lex-v1:decode-back ?raw nil)
      (TPTP:LexicalFault missing-back-quote ?raw))) (body))
    (rule back-start (head (metta-equation
      (tptp-lex-v1:decode-back ?raw (cons (cp ?first) ?rest))
      (tptp-lex-v1:decode-back-start
        (langdef:ground-equal ?first 96) ?raw ?rest))) (body))
    (rule back-valid (head (metta-equation
      (tptp-lex-v1:decode-back-start LangDef:SameValue ?raw ?rest)
      (TPTP:LexicalValue (langdef:codepoints->string ?rest)))) (body))
    (rule back-invalid (head (metta-equation
      (tptp-lex-v1:decode-back-start LangDef:DifferentValue ?raw ?rest)
      (TPTP:LexicalFault missing-back-quote ?raw))) (body))))
