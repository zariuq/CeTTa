; CORE syntax components — de-specialization corpus for the generic gparse engine.
;
; These are the GRAMMAR (syntax) component of a languageDef only. A full languageDef
; is grammar + equations + rewrites + typing/profile data; this file supplies the
; syntax brick that later plugs into that pipeline. It is deliberately NOT "the GSLT"
; for each system.
;
; Purpose: give the parser lane a small, faithful, multi-language set of `mkGram`
; grammars so scale/perf/correctness work is not validated on Metamath alone.
; Each grammar is a CORE subset (enough to exercise the interesting syntax:
; operator precedence + associativity, mixfix, application, and scoped binders),
; NOT a complete front end. Token streams are hand-lexed (a real lexer feeding the
; native token-file path is a separate piece). Every grammar is `lp-admit`-clean and
; parses its sample inputs on the native GLL/GLR backends (see the driver test).
; Sources are pinned per grammar below; the ITP grammars align with the concrete-syntax
; specs in Mettapedia/MettaKernel/Curriculum/Notation/ (02 precedence, 04 megalodon,
; 06 metta-self, 08/10 hol) and their commit-pinned upstreams in
; repos/itp-curriculum-sources/ (see Curriculum/ITP_EXPANSION_SOURCES.md).
;
; mkGram vocabulary recap:
;   (Prod label nt (template))   template token = (Tm const) | (Hl nt) | (HlB nt scope)
;   (VarD var  nt)               a fixed variable token of class nt
;   (LexD class nt)              an open lexer class; its tokens are (Lex class raw)
;   HlB nt scope = a binder child parsed as nt, scoping the next Hl of nonterminal scope.
; Precedence/associativity are encoded by STRATIFICATION (the model has no precedence
; declarations): lower-precedence nonterminal recurses to the next tighter one; left
; recursion = left-assoc, right recursion = right-assoc.

!(import! &self lib_parse)

; =====================================================================================
; (1) PRECEDENCE / MIXFIX STRESS GRAMMAR  (item 5 — the model-limit probe)
; Arithmetic with four precedence tiers, both associativities, prefix negation,
; parentheses, and one genuine mixfix form (if/then/else). This is the grammar that
; exposes what the mkGram model must grow (it has no precedence/layout of its own):
;   Expr   -> Expr + Term | Expr - Term            (additive,       left assoc, lowest)
;           | if Expr then Expr else Expr          (mixfix)
;           | Term
;   Term   -> Term * Factor | Term / Factor        (multiplicative, left assoc)
;           | Factor
;   Factor -> Base ^ Factor | Base                 (power,          RIGHT assoc)
;   Base   -> - Base | ( Expr ) | NUM | ID         (prefix neg, group, atoms)
; =====================================================================================

(= (gnum $x) (Lex gslt-num $x))
(= (gid  $x) (Lex gslt-id  $x))

(= (prec-arith-g) (mkGram (
  (LexD gslt-num NUM)
  (LexD gslt-id  ID)
  (Prod e-add  Expr   ((Hl Expr)(Tm PLUS)(Hl Term)))
  (Prod e-sub  Expr   ((Hl Expr)(Tm MINUS)(Hl Term)))
  (Prod e-ite  Expr   ((Tm IF)(Hl Expr)(Tm THEN)(Hl Expr)(Tm ELSE)(Hl Expr)))
  (Prod e-term Expr   ((Hl Term)))
  (Prod t-mul  Term   ((Hl Term)(Tm STAR)(Hl Factor)))
  (Prod t-div  Term   ((Hl Term)(Tm SLASH)(Hl Factor)))
  (Prod t-fac  Term   ((Hl Factor)))
  (Prod f-pow  Factor ((Hl Base)(Tm CARET)(Hl Factor)))
  (Prod f-base Factor ((Hl Base)))
  (Prod b-neg  Base   ((Tm MINUS)(Hl Base)))
  (Prod b-par  Base   ((Tm LP)(Hl Expr)(Tm RP)))
  (Prod b-num  Base   ((Hl NUM)))
  (Prod b-id   Base   ((Hl ID)))
  )))

; =====================================================================================
; (2) MeTTa CORE  (self-hosting / dogfooding)
; The uniform S-expression syntax: symbol | variable | () | ( items ).
;   Expr  -> SYM | VAR | ( ) | ( Items )
;   Items -> Expr | Expr Items
; =====================================================================================

(= (msym $x) (Lex metta-sym $x))
(= (mvar $x) (Lex metta-var $x))

(= (metta-core-g) (mkGram (
  (LexD metta-sym SYM)
  (LexD metta-var VAR)
  (Prod x-sym   Expr  ((Hl SYM)))
  (Prod x-var   Expr  ((Hl VAR)))
  (Prod x-nil   Expr  ((Tm LP)(Tm RP)))
  (Prod x-app   Expr  ((Tm LP)(Hl Items)(Tm RP)))
  (Prod i-one   Items ((Hl Expr)))
  (Prod i-more  Items ((Hl Expr)(Hl Items)))
  )))

; =====================================================================================
; (3) HOL Light CORE  (classical HOL term grammar)
; Source: HOL Light `bool.ml` fixities (jrh13/hol-light@924faf0, pinned at
; repos/itp-curriculum-sources/hol_light): <=> (2,R), ==> (4,R), \/ (6,R), /\ (8,R),
; = (12,R), ~ prefix, binders ! ? \ (parse_as_binder). Stratified loosest->tightest so
; precedence/associativity are structural. Aligns with Curriculum/Notation/{08,10}_hol*.
;   Term -> Bind | Iff                 Bind -> ! x. Term | ? x. Term | \ x. Term
;   Iff  -> Imp <=> Iff | Imp          Imp  -> Disj ==> Imp | Disj
;   Disj -> Conj \/ Disj | Conj        Conj -> Eq /\ Conj | Eq
;   Eq   -> Neg = Eq | Neg             Neg  -> ~ Neg | App
;   App  -> App Atom | Atom            Atom -> ( Term ) | ID
; Binders live only at the top Term level (and inside parens), with a greedy Term body,
; so `! x. p ==> q` = `! x. (p ==> q)` uniquely. The model cannot *declare* a greedy
; binder body, so it is expressed structurally (start symbol = Term).
; =====================================================================================

(= (hid $x) (Lex hol-id $x))

(= (hol-core-g) (mkGram (
  (LexD hol-id ID)
  (Prod h-tbd  Term ((Hl Bind)))
  (Prod h-tif  Term ((Hl Iff)))
  (Prod h-all  Bind ((Tm HALL)(HlB ID Term)(Tm HDOT)(Hl Term)))
  (Prod h-ex   Bind ((Tm HEX)(HlB ID Term)(Tm HDOT)(Hl Term)))
  (Prod h-lam  Bind ((Tm HLAM)(HlB ID Term)(Tm HDOT)(Hl Term)))
  (Prod h-iff  Iff  ((Hl Imp)(Tm HIFF)(Hl Iff)))
  (Prod h-imf  Iff  ((Hl Imp)))
  (Prod h-imp  Imp  ((Hl Disj)(Tm HIMP)(Hl Imp)))
  (Prod h-djf  Imp  ((Hl Disj)))
  (Prod h-or   Disj ((Hl Conj)(Tm HOR)(Hl Disj)))
  (Prod h-cjf  Disj ((Hl Conj)))
  (Prod h-and  Conj ((Hl Eq)(Tm HAND)(Hl Conj)))
  (Prod h-eqf  Conj ((Hl Eq)))
  (Prod h-eq   Eq   ((Hl Neg)(Tm HEQ)(Hl Eq)))
  (Prod h-ngf  Eq   ((Hl Neg)))
  (Prod h-not  Neg  ((Tm HNOT)(Hl Neg)))
  (Prod h-apf  Neg  ((Hl App)))
  (Prod h-app  App  ((Hl App)(Hl Atom)))
  (Prod h-atf  App  ((Hl Atom)))
  (Prod h-par  Atom ((Tm LP)(Hl Term)(Tm RP)))
  (Prod h-id   Atom ((Hl ID)))
  )))

; =====================================================================================
; (4) Megalodon CORE  (higher-order Tarski-Grothendieck set theory syntax)
; Source: real .mg corpus (Mettapedia/megalodon/*.mg), how-to-megalodon.md, and
; Curriculum/Notation/04_megalodon_hotg_concrete_syntax.metta; upstream pinned at
; repos/itp-curriculum-sources/megalodon_ai4reason. Binders forall/exists (`,`) and fun
; (`=>`) with optional `: type`; -> right assoc; /\ \/ ; relational = <> :e c= ; ~ prefix
; (prec 700); sorts prop/set. Type annotations are full types (Arrow), body is loosest.
;   Prop  -> forall x [:Arrow] , Prop | exists x [:Arrow] , Prop | fun x [:Arrow] => Prop | Arrow
;   Arrow -> Disj -> Prop | Disj       Disj -> Conj \/ Disj | Conj
;   Conj  -> Rel /\ Conj | Rel         Rel  -> Neg (= | <> | :e | c=) Neg | Neg
;   Neg   -> ~ Neg | App               App  -> App Atom | Atom
;   Atom  -> ( Prop ) | ID | prop | set
; =====================================================================================

(= (mid $x) (Lex mega-id $x))

(= (megalodon-core-g) (mkGram (
  (LexD mega-id ID)
  (Prod m-allT Prop  ((Tm FORALL)(HlB ID Prop)(Tm COLON)(Hl Arrow)(Tm COMMA)(Hl Prop)))
  (Prod m-all  Prop  ((Tm FORALL)(HlB ID Prop)(Tm COMMA)(Hl Prop)))
  (Prod m-exT  Prop  ((Tm EXISTS)(HlB ID Prop)(Tm COLON)(Hl Arrow)(Tm COMMA)(Hl Prop)))
  (Prod m-ex   Prop  ((Tm EXISTS)(HlB ID Prop)(Tm COMMA)(Hl Prop)))
  (Prod m-funT Prop  ((Tm FUN)(HlB ID Prop)(Tm COLON)(Hl Arrow)(Tm DARROW)(Hl Prop)))
  (Prod m-fun  Prop  ((Tm FUN)(HlB ID Prop)(Tm DARROW)(Hl Prop)))
  (Prod m-prf  Prop  ((Hl Arrow)))
  (Prod m-arr  Arrow ((Hl Disj)(Tm ARROW)(Hl Prop)))
  (Prod m-djf  Arrow ((Hl Disj)))
  (Prod m-or   Disj  ((Hl Conj)(Tm MOR)(Hl Disj)))
  (Prod m-cjf  Disj  ((Hl Conj)))
  (Prod m-and  Conj  ((Hl Rel)(Tm MAND)(Hl Conj)))
  (Prod m-rlf  Conj  ((Hl Rel)))
  (Prod m-eq   Rel   ((Hl Neg)(Tm MEQ)(Hl Neg)))
  (Prod m-ne   Rel   ((Hl Neg)(Tm MNE)(Hl Neg)))
  (Prod m-in   Rel   ((Hl Neg)(Tm MIN)(Hl Neg)))
  (Prod m-sub  Rel   ((Hl Neg)(Tm MSUB)(Hl Neg)))
  (Prod m-ngf  Rel   ((Hl Neg)))
  (Prod m-not  Neg   ((Tm MNOT)(Hl Neg)))
  (Prod m-apf  Neg   ((Hl App)))
  (Prod m-app  App   ((Hl App)(Hl Atom)))
  (Prod m-atf  App   ((Hl Atom)))
  (Prod m-par  Atom  ((Tm LP)(Hl Prop)(Tm RP)))
  (Prod m-id   Atom  ((Hl ID)))
  (Prod m-prop Atom  ((Tm PROP)))
  (Prod m-set  Atom  ((Tm SET)))
  )))
