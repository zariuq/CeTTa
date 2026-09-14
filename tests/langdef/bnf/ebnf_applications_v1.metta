!(import! &self ../../../lib/lib_bnf.metta)

; Only source-level observations are pinned: acceptance, rejection and the
; number of derivations. Every probe also requests independent GLL/GLR evidence.
(= (ebnf-app:count (EBNF:DerivationsNil)) 0)
(= (ebnf-app:count (EBNF:DerivationsCons $tree $rest))
   (+ 1 (ebnf-app:count $rest)))
(= (ebnf-app:observe (EBNF:SourceParse $outcome $binding $source $receipt))
   (ebnf-app:observe $outcome))
(= (ebnf-app:observe (EBNF:EvidenceAccepted $binding $source $digest $forest $trees))
   (accepted (ebnf-app:count $trees)))
(= (ebnf-app:observe (EBNF:EvidenceRejected $binding $source $offset $expected $digest $forest))
   rejected)
(= (ebnf-app:observe (EBNF:Accepted $binding $source $trees))
   (accepted (ebnf-app:count $trees)))
(= (ebnf-app:parse (EBNF:SourceParser $handle $binding $source $receipt) (file $path))
   (case (langdef:parse-authored-file-evidence $handle $path 50000)
     (((LangDef:AuthoredParseEvidenceAccepted $lang $b $s $digest $forest ($tree))
        (bnf:parse-ebnf-file (EBNF:SourceParser $handle $binding $source $receipt) $path))
      ($unexpected (EbnfApplicationFileEvidenceFailureV1 $unexpected)))))
(= (ebnf-app:parse $parser (text $input))
   (bnf:parse-ebnf-text-evidence $parser $input 50000))

(= (ebnf-app:run $label $file $cases)
   (let $parser (bnf:parser-from-ebnf-file $file "s")
     (case $parser
       (((EBNF:SourceParser $handle $binding $source $receipt)
          (let $answers
               (collapse
                 (let ($input $expected) (superpose $cases)
                   (== $expected
                       (ebnf-app:observe
                         (ebnf-app:parse $parser $input)))))
            (let $_ (bnf:close-ebnf-parser $parser)
              (EbnfApplicationV1 $label $answers))))
        ($unexpected (EbnfApplicationLoadFailureV1 $label $unexpected))))))

!(ebnf-app:run records "tests/langdef/bnf/ebnf_records_v1.ebnf"
   (((text "{}") (accepted 1))
    ((text "[]") (accepted 1))
    ((text "{\"a\":[1,-2.5,true,null,{\"雪\":\"λ\"}]}") (accepted 1))
    ((text "[[[{}]],{\"x\":false}]") (accepted 1))
    ((file "tests/langdef/bnf/ebnf_records_escaped_v1.input") (accepted 1))
    ((text "-0.01") (accepted 1))
    ((text "") rejected)
    ((text "[1,]") rejected)
    ((text "{\"a\":}") rejected)
    ((text "{\"a\":1,}") rejected)
    ((text "01") rejected)
    ((text "1.") rejected)))

; The dangling else attaches to either conditional. A parser which picks one
; attachment silently is wrong even though it accepts the same string.
!(ebnf-app:run dangling-else "tests/langdef/bnf/ebnf_dangling_else_v1.ebnf"
   (((text "x") (accepted 1))
    ((text "if(a)x") (accepted 1))
    ((text "if(a)xelsex") (accepted 1))
    ((text "if(a)if(b)xelsex") (accepted 2))
    ((text "if(a)if(b)xelsexelsex") (accepted 1))
    ((text "if(a)if(b)if(a)xelsex") (accepted 3))
    ((text "if(a)xelse") rejected)
    ((text "elsex") rejected)))

; A deliberately small higher-order syntax grammar, not TPTP admission.
!(ebnf-app:run ho-syntax "tests/langdef/bnf/ebnf_ho_syntax_v1.ebnf"
   (((text "thf(f,type,(f:($i>$i))).") (accepted 1))
    ((text "thf(p,axiom,![X:$i]:((f@X)=X)).") (accepted 1))
    ((text "thf(p,conjecture,((^[X:$i]:X)@a)).") (accepted 1))
    ((text "thf(p,axiom,![F:($i>$i),X:$i]:((F@X)=X)).") (accepted 1))
    ((text "thf(f,type,(f:($i>$i))).thf(p,axiom,(a=a)).") (accepted 1))
    ((text "thf(p,axiom,^[X:$i]:^[Y:$i]:X).") (accepted 1))
    ((text "thf(p,axiom,![X:$i]((f@X)=X)).") rejected)
    ((text "thf(p,axiom,(f@)).") rejected)
    ((text "thf(p,axiom,a)") rejected)
    ((text "thf(p,axiom,).") rejected)))
