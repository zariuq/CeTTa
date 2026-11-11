; Generic presentation engine. A GSLT presentation module gives the grammar;
; CeTTa compiles its parser (GLL/GLR) from the module; a forms table gives each
; production the MeTTa atom it denotes. Reading = parse + instantiate forms.
; Printing = match forms against the atom + render that production's own
; syntax items, so a printed view always expands back to the atom it prints.
;
; Atoms are handled in a boxed encoding so that no program atom is evaluated
; while it is read or printed (an eager dialect would otherwise run (+ 1 2)):
;   expression (h a b) -> (cs:x h a b)     variable $n -> (cs:var "n")
;   symbols, numbers and strings are themselves; (cs:run e) marks !e.
; pres:sexpr prints the boxed atoms in the standard MeTTa presentation.

!(import! &self (library lib_module))
!(import! &self str)
!(bind! &pres (new-space))

; ---------------------------------------------------------------- loading
(= (pres:load $name $path $theory $entry)
   (let* (($reader (langdef:load-baked-parser "module-core-reader"))
          ($module (module:read-file $reader "presentation" $path))
          ($closed (langdef:close-parser $reader))
          ($th (module:assemble $module $theory LNil LNil OriginViewV1 "presentation"))
          ($language (module:language $th "Presentation" $entry (SyntaxLexicalPolicyV1 bnfc-documented)))
          ($syntax (module:language-syntax $language))
          ($parser (langdef:compile-parser $syntax auto))
          ($insp (langdef:inspect-syntax-gslt $syntax))
          ($n (pres:install $name $insp))
          ($forms (pres:forms-of $name))
          ($enriched (pres:enrich $name $forms))
          ($s1 (add-atom &pres (pres:parser $name $parser)))
          ($s2 (add-atom &pres (pres:ordered $name $enriched))))
     (pres:loaded $name $n)))

; A program can extend a presentation with its own notation: a module whose
; theory joins the base presentation with further Terms. CATALOG lists the
; modules it imports as ((import-name path) ...). Productions without a form
; denote the expression headed by their label: (Label child ...), so a
; constructor's notation needs no interpretation table; such forms are tried
; before the base forms when printing.
(= (pres:load-extended $name $base $path $theory $entry $catalog)
   (let* (($reader (langdef:load-baked-parser "module-core-reader"))
          ($module (module:read-file $reader "program" $path))
          ($entries (pres:catalog $reader $catalog))
          ($closed (langdef:close-parser $reader))
          ($th (module:assemble $module $theory LNil $entries OriginViewV1 "program"))
          ($language (module:language $th "Program" $entry (SyntaxLexicalPolicyV1 bnfc-documented)))
          ($syntax (module:language-syntax $language))
          ($parser (langdef:compile-parser $syntax auto))
          ($insp (langdef:inspect-syntax-gslt $syntax))
          ($n (pres:install $name $insp))
          ($baseforms (pres:forms-of $base))
          ($own (pres:own-forms $name))
          ($defaults (pres:default-forms $name (pres:labels $name)
                        (pres:append $own $baseforms) (pres:token-forms-of $base)))
          ($extra (pres:append $own $defaults))
          ($s0 (add-atom &pres (pres:inherits $name $base $extra)))
          ($enriched (pres:enrich $name (pres:append $extra $baseforms)))
          ($s1 (add-atom &pres (pres:parser $name $parser)))
          ($s2 (add-atom &pres (pres:ordered $name $enriched))))
     (pres:loaded $name $n (size-atom $extra))))
(= (pres:catalog $reader $entries)
   (if (== $entries ()) LNil
     (let* (($cell (decons-atom $entries))
            (($entry $rest) $cell)
            (($import $path) $entry)
            ($m (module:read-file $reader $import $path))
            ($tail (pres:catalog $reader $rest)))
       (LCons (ModuleCatalogEntryV1 $import $m) $tail))))
(= (pres:labels $name) (collapse (match &pres (pres:rule $name $label $cat $items) ($label $items))))
(= (pres:own-forms $name)
   (case (collapse (match &self (pres:forms $name $fs) $fs))
     ((() ()) (($fs) $fs))))
(= (pres:default-forms $name $rules $forms $tokforms)
   (if (== $rules ()) ()
     (let* ((($r $rest) (decons-atom $rules))
            (($label $items) $r)
            ($tail (pres:default-forms $name $rest $forms $tokforms)))
       (if (pres:constructor-label? $label $forms $tokforms)
           (cons-atom ((str:string->symbol $label) (pres:constructor-template $label $items)) $tail)
           $tail))))
(= (pres:constructor-label? $label $forms $tokforms)
   (if (or (== $label "_") (or (str:starts-with $label "[") (str:starts-with $label "(")))
       False
       (if (== (pres:assoc $label $forms) pres:none)
           (== (pres:assoc $label $tokforms) pres:none)
           False)))
(= (pres:constructor-template $label $items)
   (pres:box (cons-atom (str:string->symbol $label) (pres:arg-holes $items 0))))
(= (pres:arg-holes $items $k)
   (if (== $items ()) ()
     (let ($it $rest) (decons-atom $items)
       (case $it
         (((nt $c) (cons-atom (cs:arg $k) (pres:arg-holes $rest (+ $k 1))))
          ($o (pres:arg-holes $rest $k)))))))

(= (pres:forms-of $name)
   (case (collapse (match &pres (pres:inherits $name $base $extra) ($base $extra)))
     ((() (match &self (pres:forms $name $fs) $fs))
      ((($base $extra)) (pres:append $extra (pres:forms-of $base))))))
(= (pres:token-forms-of $name)
   (case (collapse (match &pres (pres:inherits $name $base $extra) $base))
     ((() (match &self (pres:token-forms $name $fs) $fs))
      (($base) (pres:token-forms-of $base)))))
; Reserved words are derived from the grammar: every terminal that is a word.
(= (pres:keywords-of $name)
   (case (collapse (match &pres (pres:kw $name $ks) $ks))
     ((($ks) $ks)
      (() (let $ks (pres:grammar-words (pres:labels $name) ())
            (let $_ (add-atom &pres (pres:kw $name $ks)) $ks))))))
(= (pres:grammar-words $rules $acc)
   (if (== $rules ()) $acc
     (let* ((($r $rest) (decons-atom $rules))
            (($label $items) $r))
       (pres:grammar-words $rest (pres:item-words $items $acc)))))
(= (pres:item-words $items $acc)
   (if (== $items ()) $acc
     (let ($it $rest) (decons-atom $items)
       (case $it
         (((t $s) (pres:item-words $rest
                    (if (str:is-initial-in alpha $s)
                        (if (pres:member? $s $acc) $acc (cons-atom $s $acc))
                        $acc)))
          ($o (pres:item-words $rest $acc)))))))
(= (pres:member? $x $l)
   (if (== $l ()) False
     (let ($h $t) (decons-atom $l) (if (== $h $x) True (pres:member? $x $t)))))
; an extended presentation keeps its base's spelling rules
(= (pres:root $name)
   (case (collapse (match &pres (pres:inherits $name $base $extra) $base))
     ((() $name) (($b) (pres:root $b)))))
(= (pres:get-parser $name) (match &pres (pres:parser $name $p) $p))
(= (pres:ordered-of $name) (match &pres (pres:ordered $name $o) $o))
(= (pres:list-of $l) (if (== $l []) () (pres:list-of-cells $l)))
(= (pres:list-of-cells [$h | $t]) (let $r (pres:list-of $t) (cons-atom $h $r)))

(= (pres:install $name (SyntaxGSLTInspectionV1 (GSLTLanguageDefWireV1 $n $types $rules $a $b) $p $q $r))
   (pres:install-rules $name $rules 0))
(= (pres:install-rules $name LNil $k) $k)
(= (pres:install-rules $name (LCons (GrammarRule $label $cat $params $items $ev) $rest) $k)
   (let $its (pres:items $items $params)
     (let $_ (add-atom &pres (pres:rule $name $label $cat $its))
       (pres:install-rules $name $rest (+ $k 1)))))

(= (pres:items LNil $ps) ())
(= (pres:items (LCons (SyntaxTerminal $s) $rest) $ps)
   (let $r (pres:items $rest $ps) (cons-atom (t $s) $r)))
(= (pres:items (LCons (SyntaxNonTerminal $a) $rest) (LCons (TermSimple $an (TBase $c)) $ps))
   (let $r (pres:items $rest $ps) (cons-atom (nt $c) $r)))

(= (pres:rule-of $name $label) (match &pres (pres:rule $name $label $cat $items) ($cat $items)))
(= (pres:list-rule $name $label $cat) (match &pres (pres:rule $name $label $cat $items) $items))

; (label template) -> (label template category family level items), in order
(= (pres:enrich $name $forms)
   (if (== $forms ()) ()
     (let* ((($f $rest) (decons-atom $forms))
            (($label $tpl) $f)
            (($cat $items) (pres:rule-of $name (str:symbol->string $label)))
            (($fam $lvl) (pres:split-cat $cat))
            ($tail (pres:enrich $name $rest)))
       (cons-atom ($label $tpl $cat $fam $lvl $items) $tail))))

; "Exp7" -> ("Exp" 7); "Conj" -> ("Conj" 0)
(= (pres:split-cat $cat)
   (let $n (str:byte-length $cat) (pres:split-at $cat $n $n)))
(= (pres:split-at $cat $k $n)
   (if (> $k 0)
       (if (str:is-all-in digit (str:byte-slice $cat (- $k 1) $k))
           (pres:split-at $cat (- $k 1) $n)
           (pres:split-done $cat $k $n))
       (pres:split-done $cat $k $n)))
(= (pres:split-done $cat $k $n)
   (if (== $k $n) ($cat 0)
       ((str:byte-slice $cat 0 $k) (str:string->number (str:byte-slice $cat $k $n)))))

; ---------------------------------------------------------------- boxed atoms
(= (pres:leaf? $t)
   (if (== (get-metatype $t) Expression)
       (case $t (((cs:var $n) True) ($o False)))
       True))
(= (pres:items-of $box) (let ($x $items) (decons-atom $box) $items))
(= (pres:box $items) (cons-atom cs:x $items))
(= (pres:head $box) (car-atom (pres:items-of $box)))
(= (pres:nonempty-box? $t)
   (if (pres:leaf? $t) False
     (case $t (((cs:x) False) ((cs:x $h) True) ($o (== (car-atom $t) cs:x))))))

; ---------------------------------------------------------------- reading
; Keywords are soft, as the compiled readers read them: a keyword is a name
; wherever the grammar cannot take the keyword.  The run-time parser reserves
; its keywords, so where it rejects the text at a keyword, that word is read
; again as the name `word`, until the text reads or is rejected elsewhere.
; The parser rejects at the start of the first token no reading can take (a
; grammar with layout too: its rejection is reported in the source text),
; which is where the compiled reader, taking a keyword wherever the grammar
; can, finds that the keyword cannot be taken; so both routes read one
; language.
(= (pres:read $name $text)
   (let $r (langdef:parser-parse-text (pres:get-parser $name) $text)
     (case $r
       (((ParserAcceptedV1 ($cst) $span) (pres:tree $name $cst))
        ((ParserAcceptedV1 $many $span) (pres:ambiguous (size-atom $many)))
        ((ParserRejectedV1 $scalar $at $expected)
          (case (pres:soft-keyword $name $text $at)
            ((pres:none (pres:rejected $r))
             ($again (pres:read $name $again)))))
        ($other (pres:rejected $other))))))

; TEXT with the keyword that begins at byte AT written `quoted`, or pres:none
; when no keyword is a whole word there (the longest one).
(= (pres:soft-keyword $name $text $at)
   (let* (($after-name (and (> $at 0) (pres:name-char? (str:byte-slice $text (- $at 1) $at))))
          ($rest (str:byte-slice $text $at (str:byte-length $text)))
          ($k (if (or $after-name (pres:joins-before? $text $at)) ""
                  (pres:longest-keyword $rest (pres:keywords-of $name) ""))))
     (if (== $k "") pres:none (pres:quote-word $text $at $k))))
(= (pres:quote-word $text $at $k)
   (str:concat (str:byte-slice $text 0 $at)
     (str:concat "`" (str:concat $k (str:concat "`"
       (str:byte-slice $text (+ $at (str:byte-length $k)) (str:byte-length $text)))))))
; the character before START joins a longer name: $a, `a, &a, x-a, x:a
(= (pres:joins-before? $text $start)
   (if (== $start 0) False
     (let $c (str:byte-slice $text (- $start 1) $start)
       (if (or (== $c "$") (or (== $c "`") (== $c "&"))) True
         (if (or (== $c "-") (== $c ":"))
             (if (> $start 1) (pres:name-char? (str:byte-slice $text (- $start 2) (- $start 1))) False)
             False)))))
; a character that continues a name (an operator such as * or - does not)
(= (pres:name-char? $c)
   (or (str:is-all-in alpha $c) (or (str:is-all-in digit $c) (or (== $c "_") (== $c "'")))))
(= (pres:longest-keyword $rest $ks $best)
   (if (== $ks ()) $best
     (let ($k $more) (decons-atom $ks)
       (pres:longest-keyword $rest $more
         (if (pres:word-at? $rest $k)
             (if (> (str:byte-length $k) (str:byte-length $best)) $k $best)
             $best)))))
; K begins REST as a whole word: what follows cannot continue a name
(= (pres:word-at? $rest $k)
   (if (str:starts-with $rest $k)
       (not (pres:name-continues? (str:byte-slice $rest (str:byte-length $k) (str:byte-length $rest))))
       False))
(= (pres:name-continues? $after)
   (if (== $after "") False
     (let $c (str:byte-slice $after 0 1)
       (if (str:is-all-in alpha $c) True
         (if (str:is-all-in digit $c) True
           (if (or (== $c "_") (or (== $c "'") (or (== $c "!") (== $c "?")))) True
             (if (or (== $c "-") (== $c ":"))
                 (if (> (str:byte-length $after) 1) (str:is-all-in alpha (str:byte-slice $after 1 2)) False)
                 False)))))))

(= (pres:tree $name (LbnfAstNodeV1 $cat $label $span $kids))
   (if (== $label "_")
       (pres:tree $name (pres:first-kid $kids))
       (let $ts (pres:trees $name $kids)
         (pres:expand $name $label $ts))))
(= (pres:tree $name (LbnfAstListV1 $cat $span $items))
   (pres:box (pres:trees $name $items)))
(= (pres:tree $name (LbnfAstTokenV1 $tcat $kind $text $pos $span $raw))
   (pres:tok $tcat $text))
(= (pres:first-kid (LCons $k $rest)) $k)
(= (pres:trees $name LNil) ())
(= (pres:trees $name (LCons $k $rest))
   (let $t (pres:tree $name $k) (let $r (pres:trees $name $rest) (cons-atom $t $r))))

(= (pres:expand $name $label $ts)
   (case (pres:assoc $label (pres:token-forms-of $name))
     ((pres:none (pres:inst (pres:assoc $label (pres:forms-of $name)) $ts))
      ($kind (pres:leaf $kind (car-atom $ts))))))

; tables are keyed by label symbols; the parser reports labels as strings
(= (pres:assoc $k $l)
   (if (== $l ()) pres:none
     (let (($a $b) $rest) (decons-atom $l)
       (if (== (str:symbol->string $a) $k) $b (pres:assoc $k $rest)))))

; template instantiation: (cs:arg i) is child i, (cs:cons x l) prepends x to l
(= (pres:inst $tpl $ts)
   (case $tpl
     (((cs:arg $i) (pres:nth $ts $i))
      ((cs:cons $x $l)
        (let $a (pres:inst $x $ts)
          (let $b (pres:inst $l $ts)
            (pres:box (cons-atom $a (pres:items-of $b))))))
      ($other (if (== (get-metatype $tpl) Expression) (pres:inst-list $tpl $ts) $tpl)))))
(= (pres:inst-list $tpl $ts)
   (if (== $tpl ()) ()
     (let ($h $t) (decons-atom $tpl)
       (let $a (pres:inst $h $ts) (let $b (pres:inst-list $t $ts) (cons-atom $a $b))))))
(= (pres:nth $l $i) (let ($h $t) (decons-atom $l) (if (== $i 0) $h (pres:nth $t (- $i 1)))))

; ---------------------------------------------------------------- tokens
(= (pres:text $t) (str:from-codepoints (pres:text-cps $t)))
(= (pres:text-cps (bnf-v1:text-nil)) ())
(= (pres:text-cps (bnf-v1:text-cons $c $r)) (let $rest (pres:text-cps $r) (cons-atom $c $rest)))

(= (pres:leaf var (pres:tok $c (bnf-v1:text-cons 36 $r))) (cs:var (pres:text $r)))
(= (pres:leaf name (pres:tok $c $t)) (str:string->symbol (pres:text $t)))
(= (pres:leaf quoted (pres:tok $c (bnf-v1:text-cons 96 $r)))
   (str:string->symbol (str:from-codepoints (pres:unbackquote $r))))
(= (pres:leaf number (pres:tok $c $t)) (str:string->number (pres:text $t)))
(= (pres:leaf negative (pres:tok $c $t)) (* -1 (str:string->number (pres:text $t))))
(= (pres:leaf string (pres:tok $c $t)) (pres:text $t))
; a raw region r#"..."# is the text between its fences, kept byte for byte
(= (pres:leaf raw (pres:tok $c $t))
   (let $s (pres:text $t) (str:byte-slice $s 3 (- (str:byte-length $s) 2))))
(= (pres:leaf word (pres:tok $c $t)) (pres:word (pres:text $t)))

(= (pres:unbackquote $t)
   (case $t
     (((bnf-v1:text-cons 96 (bnf-v1:text-nil)) ())
      ((bnf-v1:text-cons 92 (bnf-v1:text-cons $c $r))
        (let $rest (pres:unbackquote $r) (cons-atom $c $rest)))
      ((bnf-v1:text-cons $c $r)
        (let $rest (pres:unbackquote $r) (cons-atom $c $rest))))))

; an S-expression word: a variable, a number, or a symbol (MeTTa's token rule)
(= (pres:word $s)
   (if (str:starts-with $s "$")
       (cs:var (str:byte-slice $s 1 (str:byte-length $s)))
       (case (pres:try-number $s)
         ((pres:none (str:string->symbol $s))
          ($n $n)))))
(= (pres:try-number $s)
   (if (pres:numeric-start? $s)
       (let $n (str:string->number $s)
         (if (== (get-metatype $n) Grounded) $n pres:none))
       pres:none))
(= (pres:numeric-start? $s)
   (if (str:is-initial-in digit $s) True
     (if (str:starts-with $s "-")
         (if (> (str:byte-length $s) 1)
             (str:is-initial-in digit (str:byte-slice $s 1 (str:byte-length $s)))
             False)
         False)))

; ---------------------------------------------------------------- printing
; render a tree at a category to a token list; pres:fail if no view applies
(= (pres:render $name $tree $cat)
   (let ($fam $lvl) (pres:split-cat $cat)
     (if (str:starts-with $cat "[")
         (pres:render-list $name $tree $cat)
         (if (pres:leaf? $tree)
             (if (== $fam (pres:leaf-family $name))
                 (pres:leaf-tokens (pres:root $name) $name $tree)
                 (pres:render-forms $name (pres:ordered-of $name) $tree $fam $lvl))
             (pres:render-forms $name (pres:ordered-of $name) $tree $fam $lvl)))))
(= (pres:leaf-family $name) (let $r (pres:root $name) (match &self (pres:leaf-category $r $c) $c)))

(= (pres:render-forms $name $forms $tree $fam $lvl)
   (if (== $forms ()) pres:fail
     (let ($f $rest) (decons-atom $forms)
       (let ($label $tpl $cat $ffam $flvl $items) $f
         (if (== $ffam $fam)
             (let $r (pres:try-form $name $label $tpl $items $tree)
               (if (== $r pres:fail)
                   (pres:render-forms $name $rest $tree $fam $lvl)
                   (if (< $flvl $lvl) (pres:paren $r) $r)))
             (pres:render-forms $name $rest $tree $fam $lvl))))))

(= (pres:try-form $name $label $tpl $items $tree)
   (let $env (pres:tm $tpl $tree ())
     (if (== $env pres:fail) pres:fail
       (if (pres:prefer-hook (pres:root $name) $label $tree)
           (pres:render-items $name $items $env 0 True)
           pres:fail))))

; match a ground template against a boxed tree, extending an index->tree env
(= (pres:tm $tpl $tree $env)
   (case $tpl
     (((cs:arg $i) (cons-atom ($i $tree) $env))
      ((cs:cons $x $l)
        (if (pres:nonempty-box? $tree)
            (let ($h $t) (decons-atom (pres:items-of $tree))
              (let $e1 (pres:tm $x $h $env)
                (if (== $e1 pres:fail) pres:fail (pres:tm $l (pres:box $t) $e1))))
            pres:fail))
      ($other
        (if (== (get-metatype $tpl) Expression)
            (if (pres:leaf? $tree) pres:fail
              (if (== (get-metatype $tree) Expression) (pres:tm-list $tpl $tree $env) pres:fail))
            (if (== $tpl $tree) $env pres:fail))))))
(= (pres:tm-list $tpls $trees $env)
   (if (== $tpls ())
       (if (== $trees ()) $env pres:fail)
       (if (== $trees ()) pres:fail
         (let ($a $as) (decons-atom $tpls)
           (let ($b $bs) (decons-atom $trees)
             (let $e1 (pres:tm $a $b $env)
               (if (== $e1 pres:fail) pres:fail (pres:tm-list $as $bs $e1))))))))
(= (pres:env-get $i $env)
   (let (($k $v) $rest) (decons-atom $env) (if (== $k $i) $v (pres:env-get $i $rest))))

; render a production's items: terminals verbatim, nonterminals by recursion
(= (pres:render-items $name $items $env $k $first)
   (if (== $items ()) ()
     (let ($it $rest) (decons-atom $items)
       (case $it
         (((t $s)
            (let $more (pres:render-items $name $rest $env $k False)
              (if (== $more pres:fail) pres:fail
                (if (pres:word-before-paren? $s $rest)
                    (cons-atom (pres:terminal $s $first $rest) (cons-atom (pres:glue) $more))
                    (cons-atom (pres:terminal $s $first $rest) $more)))))
          ((nt $c)
            (let $sub (pres:render $name (pres:env-get $k $env) $c)
              (if (== $sub pres:fail) pres:fail
                (let $more (pres:render-items $name $rest $env (+ $k 1) False)
                  (if (== $more pres:fail) pres:fail
                    (pres:append (pres:mark-call $sub $rest) $more)))))))))))

; token record: (tok TEXT GLUE-LEFT GLUE-RIGHT)
(= (pres:terminal $s $first $rest)
   (if (pres:closer? $s) (tok $s True False)
     (if (== $s "(") (tok $s False True)
       (if (== $s "[") (tok $s False True)
         (if (and $first (pres:starts-nt? $rest)) (pres:prefix-tok $s) (tok $s False False))))))
(= (pres:prefix-tok $s) (if (pres:word-like? $s) (tok $s False False) (tok $s False True)))
(= (pres:word-like? $s) (str:is-initial-in alpha $s))
(= (pres:closer? $s) (if (== $s ")") True (if (== $s "]") True (if (== $s ",") True (== $s ";")))))
(= (pres:starts-nt? $rest)
   (if (== $rest ()) False (case (car-atom $rest) (((nt $c) True) ($o False)))))
; an opening "(" right after a nonterminal is a postfix call: glue it left
(= (pres:mark-call $sub $rest)
   (if (== $rest ()) $sub
     (case (car-atom $rest)
       (((t "(") (pres:append $sub ((pres:glue))))
        ($o $sub)))))
(= (pres:word-before-paren? $s $rest)
   (if (pres:word-like? $s)
       (if (== $rest ()) False (case (car-atom $rest) (((t "(") True) ($o False))))
       False))

(= (pres:append $a $b)
   (if (== $a ()) $b (let ($h $t) (decons-atom $a) (let $r (pres:append $t $b) (cons-atom $h $r)))))
(= (pres:paren $toks) (cons-atom (tok "(" False True) (pres:append $toks ((tok ")" True False)))))

(= (pres:render-list $name $tree $cat)
   (if (pres:leaf? $tree) pres:fail
     (if (== (car-atom $tree) cs:x)
       (let $elems (pres:items-of $tree)
         (if (== $elems ())
             (case (collapse (pres:list-rule $name "[]" $cat))
               ((() pres:fail) ($x ())))
             (let ($h $t) (decons-atom $elems)
               (if (== $t ())
                   (case (collapse (pres:list-rule $name "(:[])" $cat))
                     ((() (pres:render-cons $name $h $t $cat))
                      (($items) (pres:render-items $name $items ((0 $h)) 0 False))))
                   (pres:render-cons $name $h $t $cat)))))
       pres:fail)))
(= (pres:render-cons $name $h $t $cat)
   (case (collapse (pres:list-rule $name "(:)" $cat))
     ((($items) (pres:render-items $name $items ((0 $h) (1 (pres:box $t))) 0 False))
      ($o pres:fail))))

; join tokens with spaces, except where either side glues
(= (pres:join $toks) (pres:join-from $toks True ""))
(= (pres:join-from $toks $nospace $acc)
   (if (== $toks ()) $acc
     (let ($tk $rest) (decons-atom $toks)
       (case $tk
         (((pres:glue) (pres:join-from $rest True $acc))
          ((tok $s $gl $gr)
            (let $sep (if (or $nospace $gl) "" " ")
              (pres:join-from $rest $gr (str:concat $acc (str:concat $sep $s))))))))))

; Layout for a statement too long for one line (a simplified Oppen layout):
; a brace group stays flat when it fits the rest of the line, otherwise it
; breaks after "{", after each comma directly inside it, and before "}"; a let
; chain breaks after "in". Layout never changes the atoms: the grammar ignores
; whitespace.
(= (pres:width) 96)
(= (pres:fits? $line) (<= (str:byte-length $line) (pres:width)))
(= (pres:layout $toks) (pres:layout-from $toks True "" 0 () False 0))
(= (pres:indent $n) (if (<= $n 0) "" (str:concat "  " (pres:indent (- $n 1)))))
(= (pres:layout-from $toks $nospace $acc $ind $stack $fresh $col)
   (if (== $toks ()) $acc
     (let ($tk $rest) (decons-atom $toks)
       (case $tk
         (((pres:glue) (pres:layout-from $rest True $acc $ind $stack $fresh $col))
          ((tok $s $gl $gr)
            (let $sep (if $fresh "" (if (or $nospace $gl) "" " "))
              (if (== $s "{")
                  (pres:layout-brace $toks $sep $acc $ind $stack $col)
                  (if (and (== $s "}") (pres:top-brace? $stack))
                      (let $ind2 (- $ind 1)
                        (pres:layout-from $rest False
                          (str:concat $acc (str:concat "\n" (str:concat (pres:indent $ind2) "}")))
                          $ind2 (pres:pop $stack) False (+ (* 2 $ind2) 1)))
                      (let $acc1 (str:concat $acc (str:concat $sep $s))
                        (let $col1 (+ $col (+ (str:byte-length $sep) (str:byte-length $s)))
                          (if (or (== $s "(") (== $s "["))
                              (pres:layout-from $rest $gr $acc1 $ind (cons-atom $s $stack) False $col1)
                              (if (or (== $s ")") (== $s "]"))
                                  (pres:layout-from $rest $gr $acc1 $ind (pres:pop $stack) False $col1)
                                  (if (or (and (== $s ",") (pres:top-brace? $stack))
                                          (and (== $s "in") (not (pres:top-paren? $stack))))
                                      (pres:newline $rest $acc1 $ind $stack)
                                      (pres:layout-from $rest $gr $acc1 $ind $stack False $col1)))))))))))))))
(= (pres:newline $rest $acc $ind $stack)
   (pres:layout-from $rest True (str:concat $acc (str:concat "\n" (pres:indent $ind)))
     $ind $stack True (* 2 $ind)))
; a brace group: flat when it fits, otherwise broken
(= (pres:layout-brace $toks $sep $acc $ind $stack $col)
   (let ($group $after) (pres:split-group $toks 0 ())
     (let $flat (pres:join $group)
       (if (<= (+ $col (+ (str:byte-length $sep) (str:byte-length $flat))) (pres:width))
           (pres:layout-from $after False (str:concat $acc (str:concat $sep $flat)) $ind $stack False
             (+ $col (+ (str:byte-length $sep) (str:byte-length $flat))))
           (let $ind1 (+ $ind 1)
             (pres:layout-from (cdr-atom $toks) True
               (str:concat $acc (str:concat $sep (str:concat "{\n" (pres:indent $ind1))))
               $ind1 (cons-atom "{" $stack) True (* 2 $ind1)))))))
; tokens from an opening "{" through its matching "}", and the rest
(= (pres:split-group $toks $depth $acc)
   (let ($tk $rest) (decons-atom $toks)
     (let $d (case $tk (((tok "{" $a $b) (+ $depth 1)) ((tok "}" $a $b) (- $depth 1)) ($o $depth)))
       (let $acc1 (pres:append $acc ($tk))
         (if (== $d 0) ($acc1 $rest) (pres:split-group $rest $d $acc1))))))
(= (pres:pop $stack) (if (== $stack ()) () (cdr-atom $stack)))
(= (pres:top-brace? $stack) (if (== $stack ()) False (== (car-atom $stack) "{")))
(= (pres:top-paren? $stack) (if (== $stack ()) False (not (== (car-atom $stack) "{"))))

; ---------------------------------------------------------------- programs
; a program tree is (cs:x statement ...)
(= (pres:print-program $name $prog)
   (pres:print-stmts $name (pres:items-of $prog) ""))
(= (pres:print-stmts $name $stmts $acc)
   (if (== $stmts ()) $acc
     (let ($s $rest) (decons-atom $stmts)
       (let $toks (pres:render $name $s (pres:statement-category $name))
         (if (== $toks pres:fail) (pres:unprintable $s)
           (let $line (pres:join $toks)
             (pres:print-stmts $name $rest
               (str:concat $acc
                 (str:concat (if (pres:fits? $line) $line (pres:layout $toks))
                             (pres:statement-end $name))))))))))
(= (pres:statement-category $name) (let $r (pres:root $name) (match &self (pres:statement $r $c $end) $c)))
(= (pres:statement-end $name) (let $r (pres:root $name) (match &self (pres:statement $r $c $end) $end)))

; canonical S-expression text of a program (the standard MeTTa presentation)
(= (pres:sexpr-program $prog) (pres:sexpr-stmts (pres:items-of $prog) ""))
(= (pres:sexpr-stmts $stmts $acc)
   (if (== $stmts ()) $acc
     (let ($s $rest) (decons-atom $stmts)
       (pres:sexpr-stmts $rest (str:concat $acc (str:concat (pres:sexpr-stmt $s) "\n"))))))
(= (pres:sexpr-stmt $s)
   (case $s (((cs:run $e) (str:concat "!" (pres:sexpr $e))) ($o (pres:sexpr $s)))))
(= (pres:sexpr $t)
   (if (pres:leaf? $t)
       (case $t (((cs:var $n) (str:concat "$" $n)) ($o (repr $t))))
       (str:concat "(" (str:concat (pres:sexpr-items (pres:items-of $t) "") ")"))))
(= (pres:sexpr-items $t $acc)
   (if (== $t ()) $acc
     (let ($h $rest) (decons-atom $t)
       (pres:sexpr-items $rest
         (if (== $acc "") (pres:sexpr $h) (str:concat $acc (str:concat " " (pres:sexpr $h))))))))

; S-expression programs read as a flat list of atoms; ! before an atom marks it run
(= (pres:runs $prog) (pres:box (pres:runs-list (pres:items-of $prog))))
(= (pres:runs-list $l)
   (if (== $l ()) ()
     (let ($h $t) (decons-atom $l)
       (if (== $h !)
           (let ($e $t2) (decons-atom $t) (let $r (pres:runs-list $t2) (cons-atom (cs:run $e) $r)))
           (let $r (pres:runs-list $t) (cons-atom $h $r))))))
