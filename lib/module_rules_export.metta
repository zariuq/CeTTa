; A module theory's rewrite rules as a GSLT presentation for the direct GSLT
; compiler (langdef-compile petta-direct).
;   arguments: MODULE THEORY RELATION OUT
; Every rewrite of the theory, "N" . params | premises |- l ~> r, becomes the
; relation rule (rule N (head (RELATION l r)) (body ...)): a premise p ~> q is
; a call (RELATION p q), a premise "rel"(args) a call (rel args).  The
; theory's rewrites thus form one relation from a term to what it rewrites
; to; a premise's relation names a capability that another source declares
; and realizes (with its modes, when its call binds outputs).  The signature
; holds the theory's constructors, the relation and the capabilities.
; Variables are written ?name and a constructor without arguments (C).

!(import! &self (library lib_module))
!(import! &self (library module_files))
!(import! &self system)
!(import! &self fs)
!(import! &self str)

(= (as-string $x) (if (== (get-metatype $x) Symbol) (str:symbol->string $x) $x))

(= (rules:list LNil) ())
(= (rules:list (LCons $x $xs)) (cons-atom $x (rules:list $xs)))

(= (rules:length LNil) 0)
(= (rules:length (LCons $x $xs)) (+ 1 (rules:length $xs)))

; a core pattern as a presentation term
(= (rules:term (FVar $name)) (str:string->symbol (str:concat "?" $name)))
(= (rules:term (PApp $ctor $args))
   (cons-atom (str:string->symbol $ctor) (rules:terms $args)))
(= (rules:terms LNil) ())
(= (rules:terms (LCons $x $xs)) (cons-atom (rules:term $x) (rules:terms $xs)))

; a premise as a body goal
(= (rules:goal $relation (Congruence $l $r)) ($relation (rules:term $l) (rules:term $r)))
(= (rules:goal $relation (RelationQuery $name $args))
   (cons-atom (str:string->symbol $name) (rules:terms $args)))
(= (rules:goals $relation LNil) ())
(= (rules:goals $relation (LCons $p $ps)) (cons-atom (rules:goal $relation $p) (rules:goals $relation $ps)))

(= (rules:rule $relation (ScopedRewriteRuleV1 $name $bindings $premises $l $r $binding))
   (rule (str:string->symbol $name)
     (head ($relation (rules:term $l) (rules:term $r)))
     (cons-atom body (rules:goals $relation $premises))))

; the operators: constructors from the core's term declarations
(= (rules:operator (GrammarRuleV2 $ctor $sort $params $syntax $eval $algebra))
   (operator (str:string->symbol $ctor) (rules:length $params)))
(= (rules:operators LNil) ())
(= (rules:operators (LCons $g $gs)) (cons-atom (rules:operator $g) (rules:operators $gs)))

; the constructors the rules apply, with their arities, once each: a rule may
; use a constructor its theory does not declare (the CST a presentation reads,
; for instance), and the compiler checks that each name keeps one arity
(= (rules:pattern-ops (FVar $name)) ())
(= (rules:pattern-ops (PApp $ctor $args))
   (cons-atom (operator (str:string->symbol $ctor) (rules:length $args)) (rules:pattern-list-ops $args)))
(= (rules:pattern-list-ops LNil) ())
(= (rules:pattern-list-ops (LCons $p $ps)) (rules:union (rules:pattern-ops $p) (rules:pattern-list-ops $ps)))
(= (rules:premise-ops LNil) ())
(= (rules:premise-ops (LCons (Congruence $l $r) $ps))
   (rules:union (rules:pattern-ops $l) (rules:union (rules:pattern-ops $r) (rules:premise-ops $ps))))
(= (rules:premise-ops (LCons (RelationQuery $name $args) $ps))
   (rules:union (rules:pattern-list-ops $args) (rules:premise-ops $ps)))
(= (rules:rule-ops LNil) ())
(= (rules:rule-ops (LCons (ScopedRewriteRuleV1 $name $bindings $premises $l $r $binding) $rest))
   (rules:union (rules:pattern-ops $l)
     (rules:union (rules:pattern-ops $r) (rules:union (rules:premise-ops $premises) (rules:rule-ops $rest)))))

; the capabilities the premises name, once each
(= (rules:queries LNil) ())
(= (rules:queries (LCons (ScopedRewriteRuleV1 $name $bindings $premises $l $r $binding) $rest))
   (rules:union (rules:premise-queries $premises) (rules:queries $rest)))
(= (rules:premise-queries LNil) ())
(= (rules:premise-queries (LCons (RelationQuery $name $args) $ps))
   (cons-atom (operator (str:string->symbol $name) (rules:length $args)) (rules:premise-queries $ps)))
(= (rules:premise-queries (LCons (Congruence $l $r) $ps)) (rules:premise-queries $ps))
(= (rules:union () $ys) $ys)
(= (rules:union $xs $ys)
   (let ($x $rest) (decons-atom $xs)
     (let $tail (rules:union $rest $ys)
       (if (== (rules:member $x $tail) True) $tail (cons-atom $x $tail)))))
(= (rules:member $x ()) False)
(= (rules:member $x $ys)
   (let ($y $rest) (decons-atom $ys)
     (if (== $x $y) True (rules:member $x $rest))))

(= (rules:presentation $theory $relation (ModuleRewriteRulesV1 (LanguageDefV2 $name $types $terms $equations $rewrites) $occurrences))
   (let* (($operators (rules:union (rules:operators $terms) (rules:rule-ops $rewrites)))
          ($queries (rules:queries $rewrites))
          ($signature (cons-atom signature
                        (cons-atom (operator $relation 2) (rules:union $queries $operators))))
          ($rules (cons-atom rewrites (rules:rules $relation $rewrites))))
     (gslt-presentation-v1 (str:string->symbol $theory) $signature (equations) $rules)))
(= (rules:rules $relation LNil) ())
(= (rules:rules $relation (LCons $r $rs)) (cons-atom (rules:rule $relation $r) (rules:rules $relation $rs)))

(= (export-rules $path $theory $relation $out)
   (let* (($reader (langdef:load-baked-parser "module-core-reader"))
          ($files (module-files:read-with-imports $reader $path))
          ($closed (langdef:close-parser $reader))
          ((ModuleFilesV1 $module $catalog) $files)
          ($th (module:assemble $module $theory LNil $catalog OriginViewV1 "rules"))
          ($found (module:rewrite-rules $th $theory)))
     (case $found
       (((ModuleRewriteRulesV1 $core $occurrences)
          (fs:write $out (str:concat (repr (rules:presentation $theory (str:string->symbol $relation) $found)) "\n")))
        ($other
          (fs:write "/dev/stderr" (str:concat "export-rules: " (str:concat (repr $other) "\n"))))))))

!(let ($path $theory $relation $out) (system:args)
   (export-rules (as-string $path) (as-string $theory) (as-string $relation) (as-string $out)))
