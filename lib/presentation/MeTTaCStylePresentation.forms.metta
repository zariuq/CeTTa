; Interpretation of MeTTaCStylePresentation.module into ordinary MeTTa atoms.
;
; Each entry (LABEL TEMPLATE) gives the atom a production denotes. Templates
; are ground data over the boxed atom encoding the engine works on: the
; expression (h a b) is (cs:x h a b) and a variable $n is (cs:var "n"), so no
; template or program atom is ever evaluated while it is being read or printed.
; (cs:arg i) is the i-th nonterminal of the production; (cs:cons x l) is the
; expression whose first element is x and whose remaining elements are list l;
; (cs:run e) marks a statement that runs (the standard text writes it !e).
; Reading instantiates a template; printing matches it against an atom, so a
; printed view always expands back to that atom. The list order is the printing
; preference: the first form whose template matches the atom, whose look admits
; the role of its position, and whose children print, is used.

(pres:version cstyle "2.0")

(pres:forms cstyle
  ((Program (cs:arg 0))
   (SRun (cs:run (cs:arg 0)))
   (SEq (cs:x = (cs:arg 0) (cs:arg 1)))
   (SType (cs:x : (cs:arg 0) (cs:arg 1)))
   (SFact (cs:arg 0))

   (ELambda (cs:x |-> (cs:arg 0) (cs:arg 1)))
   (ELet (cs:x let (cs:arg 0) (cs:arg 1) (cs:arg 2)))
   (ELetStar (cs:x let* (cs:arg 0) (cs:arg 1)))
   (BindOne (cs:x (cs:arg 0) (cs:arg 1)))
   (EArrow (cs:cons -> (cs:cons (cs:arg 0) (cs:arg 1))))
   (ATMore (cs:cons (cs:arg 0) (cs:arg 1)))
   (ATLast (cs:x (cs:arg 0)))

   (EOr (cs:x or (cs:arg 0) (cs:arg 1)))
   (EAnd (cs:x and (cs:arg 0) (cs:arg 1)))
   (EEq (cs:x == (cs:arg 0) (cs:arg 1)))
   (ELt (cs:x < (cs:arg 0) (cs:arg 1)))
   (EGt (cs:x > (cs:arg 0) (cs:arg 1)))
   (ELe (cs:x <= (cs:arg 0) (cs:arg 1)))
   (EGe (cs:x >= (cs:arg 0) (cs:arg 1)))
   (EAdd (cs:x + (cs:arg 0) (cs:arg 1)))
   (ESub (cs:x - (cs:arg 0) (cs:arg 1)))
   (EMul (cs:x * (cs:arg 0) (cs:arg 1)))
   (EDiv (cs:x / (cs:arg 0) (cs:arg 1)))
   (EMod (cs:x % (cs:arg 0) (cs:arg 1)))
   (EQuote (cs:x quote (cs:arg 0)))
   (EUnquote (cs:x unquote (cs:arg 0)))

   (EIf (cs:x if (cs:arg 0) (cs:arg 1) (cs:arg 2)))
   (ECase (cs:x case (cs:arg 0) (cs:arg 1)))
   (ArmOne (cs:x (cs:arg 0) (cs:arg 1)))
   (EMatch (cs:x match (cs:arg 0) (cs:arg 1) (cs:arg 2)))
   (ETransformInto (cs:x transform (cs:arg 0) (cs:arg 1) (cs:arg 2) (cs:arg 3)))
   (ETransform (cs:x transform (cs:arg 0) (cs:arg 1) (cs:arg 2)))
   (SrcsMany (cs:cons , (cs:cons (cs:arg 0) (cs:arg 1))))
   (SrcsOne (cs:arg 0))
   (CTake (cs:x take (cs:arg 0)))
   (CRead (cs:x read (cs:arg 0)))
   (CAbsent (cs:x absent (cs:arg 0)))
   (CWhen (cs:x let True (cs:arg 0)))
   (CLet (cs:x let (cs:arg 0) (cs:arg 1)))
   (CAll (cs:x all (cs:arg 0)))
   (COnce (cs:x once (cs:arg 0)))
   (CUnique (cs:x unique (cs:arg 0)))
   (CCollect (cs:x collect (cs:arg 0) (cs:arg 1)))
   (CPlain (cs:arg 0))
   (TplsMany (cs:cons , (cs:cons (cs:arg 0) (cs:arg 1))))
   (TplsOne (cs:arg 0))
   (EConj (cs:cons , (cs:cons (cs:arg 0) (cs:arg 1))))

   ; imperative flows (lib/petta/lib_flow.metta states their meaning)
   (EDo (cs:x flow:then (cs:arg 0) (cs:arg 1)))
   (SeqMore (cs:x flow:then (cs:arg 0) (cs:arg 1)))
   (SeqLast (cs:arg 0))
   ; a loop over the items of a list written in place is a loop over the
   ; answers of superpose of that list: printed first, so it is preferred
   (EForOf (cs:x flow:for-each (cs:x superpose (cs:arg 1)) (cs:x |-> (cs:x (cs:arg 0)) (cs:arg 2))))
   (EFor (cs:x flow:for-each (cs:arg 1) (cs:x |-> (cs:x (cs:arg 0)) (cs:arg 2))))
   (EForFrom (cs:x flow:for-match (cs:arg 1) (cs:arg 0) (cs:arg 2)))
   (EWhile (cs:x flow:while (cs:arg 0) (cs:arg 1)))
   (ERepeat (cs:x flow:until-quiet (cs:arg 0)))

   (ECall (cs:cons (cs:arg 0) (cs:arg 1)))
   (EData (cs:cons (cs:arg 0) (cs:arg 1)))
   (ETuple (cs:arg 0))))

; Productions whose single token child is a leaf, and how the token reads.
(pres:token-forms cstyle
  ((EVar var) (ESym name) (EQSym quoted) (EInt number) (ENegInt negative)
   (EDouble number) (ENegDouble negative) (EStr string) (ERaw raw)))

; The roles of positions (only Atom and (Data T) hold, every other position
; runs).  (pres:roles P (r ...)) gives the role of each child
; of production P, by argument: run (a computation), data (held: patterns,
; atoms put in a space, declared Atom), list (a held list whose items run),
; head (a definition's head: a call pattern whose arguments are data), args (a
; call's arguments, each by its head's declared position; an undeclared one
; runs), source (a step's sources), pattern (data among sources, the parent's
; role elsewhere), inherit (the parent's).  Inside data everything is data.  Builtin
; positions are in lib/presentation/position_roles.metta; a program's own
; (: f (-> ...)) declarations come first.
(pres:roles cstyle SRun (run))
(pres:roles cstyle SFact (data))
(pres:roles cstyle SEq (head run))
(pres:roles cstyle SType (data data))
(pres:roles cstyle ELambda (data run))
(pres:roles cstyle ELet (data run run))
(pres:roles cstyle ELetStar (inherit run))
(pres:roles cstyle BindOne (data run))
(pres:roles cstyle EArrow (data data))
(pres:roles cstyle ATMore (data data))
(pres:roles cstyle ATLast (data))
(pres:roles cstyle EQuote (data))
(pres:roles cstyle EIf (run run run))
(pres:roles cstyle ECase (run inherit))
(pres:roles cstyle ArmOne (data run))
(pres:roles cstyle EMatch (run source run))
; transform without a destination evaluates each instance, as match evaluates
; its template; routed into a destination, the instances are made as data.
(pres:roles cstyle ETransform (run source run))
(pres:roles cstyle ETransformInto (run source data run))
(pres:roles cstyle CTake (data))
(pres:roles cstyle CRead (data))
(pres:roles cstyle CAbsent (data))
(pres:roles cstyle CWhen (run))
(pres:roles cstyle CLet (data run))
(pres:roles cstyle CCollect (inherit data))
; A plain item is a pattern among a step's sources; in a conjunction written
; where it runs, (a, b), its items run.
(pres:roles cstyle CPlain (pattern))
(pres:roles cstyle TplsMany (inherit inherit))
(pres:roles cstyle TplsOne (inherit))
(pres:roles cstyle EDo (run run))
(pres:roles cstyle SeqMore (run run))
(pres:roles cstyle SeqLast (run))
(pres:roles cstyle EForOf (data list run))
(pres:roles cstyle EFor (data run run))
(pres:roles cstyle EForFrom (data run run))
(pres:roles cstyle EWhile (run run))
(pres:roles cstyle ERepeat (run))
(pres:roles cstyle ECall (run args))
(pres:roles cstyle EData (data data))
(pres:roles cstyle ETuple (list))

; The looks: a call f(a) is written where its position runs (or as a
; definition's head), f{a} where it is held (a composite literal, as C and Go
; write data), [a, b] where it is held or is a list; an expression whose head
; is a number or a string cannot be called and is written [1, 2] wherever it is.
(pres:look cstyle ECall run head)
(pres:look cstyle EData data)
(pres:look cstyle ETuple data list run head)

; The words the grammar reserves (its word-like terminals) are derived from the
; module; a symbol spelled like one of them is printed bare where the grammar
; cannot take the keyword, and `quoted` elsewhere.

; Statements are Stmt, each printed with its terminator; leaves are primaries of Exp.
(pres:statement cstyle "Stmt" ";\n")
(pres:leaf-category cstyle "Exp")

; Leaf spelling. A symbol keeps its own spelling when the CName token reads it
; back as itself and it is no keyword; otherwise it is written `quoted`.
; A builtin operation may be a grounded atom whose spelling is a name; it is
; spelled as the symbol it reads back as.
; NAME is the presentation being printed: it may extend this one with notation
; whose words are reserved too.
(= (pres:leaf-tokens cstyle $name $t)
   (case $t
     (((cs:var $n) ((tok (str:concat "$" $n) False False)))
      ($o (let $text (repr $t)
            (if (str:starts-with $text "\"")
                ((tok (cstyle:string-spelling $t) False False))
                (if (pres:numeric-start? $text)
                    ((tok (cstyle:number-spelling $text) False False))
                    ((tok (cstyle:symbol-spelling $name $text) False False)))))))))
(= (cstyle:literal-text? $text)
   (if (str:starts-with $text "\"") True (pres:numeric-start? $text)))
; String literals use C escapes for quote, backslash and control characters; a
; text of several lines is written as a raw region r#"..."# when it can be.
(= (cstyle:string-spelling $s)
   (if (and (cstyle:contains? $s "\n") (not (cstyle:contains? $s "\"#")))
       (str:concat "r#\"" (str:concat $s "\"#"))
       (str:concat "\"" (str:concat (str:escape metta $s) "\""))))
(= (cstyle:contains? $s $part)
   (case (str:find $s $part) (((TextNotFound $x) False) ($i True))))
; The Double token writes an exponent as e17 or e-17, never e+17.
(= (cstyle:number-spelling $text) (str:join "e" (str:split "e+" $text)))

(= (cstyle:symbol-spelling $name $text)
   (if (cstyle:plain-name? $text)
       (if (cstyle:keyword? $text (pres:keywords-of $name)) (cstyle:backquote $text) $text)
       (cstyle:backquote $text)))
(= (cstyle:keyword? $text $ks)
   (if (== $ks ()) False
     (let ($k $rest) (decons-atom $ks) (if (== $k $text) True (cstyle:keyword? $text $rest)))))
(= (cstyle:backquote $text)
   (str:concat "`" (str:concat (cstyle:escape-bq (pres:list-of (str:codepoints $text)) "") "`")))
(= (cstyle:escape-bq $cs $acc)
   (if (== $cs ()) $acc
     (let ($c $rest) (decons-atom $cs)
       (cstyle:escape-bq $rest
         (str:concat $acc (if (or (== $c "`") (== $c "\\")) (str:concat "\\" $c) $c))))))

; CName = (letter | _ | &) (letter | digit | _ | ' | -letter | :letter)* [!?]?
(= (cstyle:plain-name? $text)
   (let $cs (pres:list-of (str:codepoints $text))
     (if (== $cs ()) False
       (let ($c $rest) (decons-atom $cs)
         (if (or (cstyle:letter? $c) (or (== $c "_") (== $c "&")))
             (cstyle:name-rest? $rest)
             False)))))
(= (cstyle:name-rest? $cs)
   (if (== $cs ()) True
     (let ($c $rest) (decons-atom $cs)
       (if (or (cstyle:letter? $c) (or (str:is-all-in digit $c) (or (== $c "_") (== $c "'"))))
           (cstyle:name-rest? $rest)
           (if (or (== $c "-") (== $c ":"))
               (if (== $rest ()) False
                 (if (cstyle:letter? (car-atom $rest)) (cstyle:name-rest? $rest) False))
               (if (or (== $c "!") (== $c "?")) (== $rest ()) False))))))
(= (cstyle:letter? $c) (str:is-all-in alpha $c))

; View guards: a call f(a, b) needs a head that can be called (an expression,
; a symbol or an operation, not a variable or a literal); the data look f{a, b}
; needs a symbol head.  The build-time compiler reads these declarations, and
; the MeTTa engine's hook below reads them too (the run-time route reads every
; look; it does not know roles, so it prefers the call look).
(pres:view-guard cstyle ECall callable-head)
(pres:view-guard cstyle EData symbol-head)
; Loops and if keep a space before their parenthesis: for (x in items),
; while (c), if (c).
(pres:spacing cstyle "for" spaced)
(pres:spacing cstyle "while" spaced)
(pres:spacing cstyle "if" spaced)
; A brace group too long for its line breaks after each comma and after "=>",
; so a step's outputs begin a line of their own after its sources.
(pres:break-after cstyle ",")
(pres:break-after cstyle "=>")
(= (pres:prefer-hook cstyle $label $tree)
   (case (collapse (match &self (pres:view-guard cstyle $label $g) $g))
     ((() True)
      (($g) (cstyle:guard-holds $g $tree)))))
(= (cstyle:guard-holds callable-head $tree) (cstyle:callable? $tree))
(= (cstyle:guard-holds symbol-head $tree)
   (let $h (pres:head $tree)
     (if (pres:leaf? $h)
         (case $h
           (((cs:var $n) False)
            ($o (if (cstyle:literal-text? (repr $h)) False True))))
         False)))
(= (cstyle:callable? $tree)
   (let $h (pres:head $tree)
     (if (pres:leaf? $h)
         (case $h
           (((cs:var $n) False)
            ($o (if (cstyle:literal-text? (repr $h)) False True))))
         True)))
