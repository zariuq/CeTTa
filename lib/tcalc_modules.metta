; A rule module read as a tcalc program, by demand.  docs/tcalc.md, section 10.
;
;   !(import! &self "lib/tcalc_modules.metta")
;   !(tcalc:module-write "Lambda.module" "Calc" "lambda.tcalc")   ; (tcalc:module-written "lambda.tcalc")
;   !(tcalc:module-text "Lambda.module" "Calc")                    ; the program as a string
;
; CeTTa's own module reader reads the module and assembles the theory, as rulec's front end does
; (tools/rulec/dump_wire.metta); this file only reads the theory's core (LanguageDefV2).  A judgment
; is a constructor heading a left side.  A rule
;     L: P1 .. Pn |- J(p) ~> r
; is read call by value, as rulec reads it: a call nested in a premise's arguments, a relation's
; arguments or the right side becomes a premise of its own with a fresh variable as target,
; innermost first and left to right, the right side's calls after the written premises.  Its tcalc
; reading, over a set store:
;     (demand J(v))              the call J(v) is asked
;     (value J(v) w)             the call J(v) has the value w
;     (L/i J(v) t1 .. ti)        rule L has matched J(v) and its premises 1..i, with targets t1..ti
; and the rules
;     L/i.ask  reads stage i-1 and makes (demand K(a))           premise i the call K(a) ~> t
;     L/i      reads stage i-1 and (value K(a) t), makes stage i
;     L/i      reads stage i-1 and the row R(a), makes stage i     premise i the relation query R(a)
;     L        reads stage n and makes (value J(p) r)
; where stage 0 is (demand J(p)).  A relation's rows are rows of the store, given with the demands.
; Every rule only reads and makes, so every run of the program reaches one store: the calls asked,
; transitively, each with every value the rules give it (a call with no value has none).
;
; Refused, with the reason: equations in the core, a binding annotation, a premise or term form other
; than those above, and a rule that uses a variable before any read binds it (in tcalc a variable only
; a make mentions is a fresh unknown, which no module rule means).

!(import! &self "lib_module.metta")
!(import! &self fs)

; ---- lists ----
(= (tcalc-module:append LNil $ys) $ys)
(= (tcalc-module:append (LCons $x $xs) $ys) (let $t (tcalc-module:append $xs $ys) (LCons $x $t)))
(= (tcalc-module:member $x LNil) False)
(= (tcalc-module:member $x (LCons $y $ys)) (if (== $x $y) True (tcalc-module:member $x $ys)))

; ---- the judgments: constructors heading a left side ----
(= (tcalc-module:judgments LNil $acc) $acc)
(= (tcalc-module:judgments (LCons $rule $rest) $acc)
   (case $rule
     (((ScopedRewriteRuleV1 $l $b $p (PApp $j $a) $r $rb)
       (if (tcalc-module:member $j $acc)
           (tcalc-module:judgments $rest $acc)
           (tcalc-module:judgments $rest (LCons $j $acc))))
      ($other (tcalc-module:judgments $rest $acc)))))

; ---- relation names: their rows are rows of the store beside the reading's own (demand, value and
; the stages L/i), so a relation named like one of those would read the reading's rows ----
(= (tcalc-module:relations LNil $acc) $acc)
(= (tcalc-module:relations (LCons $rule $rest) $acc)
   (case $rule
     (((ScopedRewriteRuleV1 $l $b $ps $lhs $rhs $rb)
       (let $acc2 (tcalc-module:premise-relations $ps $acc) (tcalc-module:relations $rest $acc2)))
      ($other (tcalc-module:relations $rest $acc)))))
(= (tcalc-module:premise-relations LNil $acc) $acc)
(= (tcalc-module:premise-relations (LCons $p $rest) $acc)
   (case $p
     (((RelationQuery $rel $args)
       (if (tcalc-module:member $rel $acc)
           (tcalc-module:premise-relations $rest $acc)
           (tcalc-module:premise-relations $rest (LCons $rel $acc))))
      ($other (tcalc-module:premise-relations $rest $acc)))))
; a stage name L/i, i in I..N, that is also a relation's name
(= (tcalc-module:stage-clash $l $i $n $rels)
   (if (> $i $n) False
       (let $s (format-args "{}/{}" ($l $i))
         (if (tcalc-module:member $s $rels) True
             (let $i2 (+ $i 1) (tcalc-module:stage-clash $l $i2 $n $rels))))))
(= (tcalc-module:length LNil) 0)
(= (tcalc-module:length (LCons $x $xs)) (let $n (tcalc-module:length $xs) (+ $n 1)))

; ---- terms as tcalc text; anything else is refused ----
(= (tcalc-module:text $t)
   (case $t
     (((FVar $x) (format-args "${}" ($x)))
      ((PApp $c LNil) (format-args "({})" ($c)))
      ((PApp $c (LCons $a $as)) (let $ts (tcalc-module:texts (LCons $a $as)) (format-args "({} {})" ($c $ts))))
      ($other (Error $other "tcalc modules: a term form other than a variable or a constructor application")))))
(= (tcalc-module:texts LNil) "")
(= (tcalc-module:texts (LCons $a LNil)) (tcalc-module:text $a))
(= (tcalc-module:texts (LCons $a (LCons $b $bs)))
   (let* (($x (tcalc-module:text $a)) ($y (tcalc-module:texts (LCons $b $bs)))) (format-args "{} {}" ($x $y))))

; ---- call by value: nested calls become premises, (Flat TERM PREMISES NEXT) ----
(= (tcalc-module:flat $t $js $n)
   (case $t
     (((FVar $x) (Flat (FVar $x) LNil $n))
      ((PApp $c $args)
       (let (Flat $args2 $ps $n2) (tcalc-module:flats $args $js $n)
         (if (tcalc-module:member $c $js)
             (let* (($v (format-args "call.{}" ($n2)))
                    ($n3 (+ $n2 1))
                    ($all (tcalc-module:append $ps (LCons (Call (PApp $c $args2) (FVar $v)) LNil))))
               (Flat (FVar $v) $all $n3))
             (Flat (PApp $c $args2) $ps $n2))))
      ($other (Flat (Unread $other) LNil $n)))))
(= (tcalc-module:flats LNil $js $n) (Flat LNil LNil $n))
(= (tcalc-module:flats (LCons $a $as) $js $n)
   (let (Flat $a2 $p1 $n1) (tcalc-module:flat $a $js $n)
     (let (Flat $as2 $p2 $n2) (tcalc-module:flats $as $js $n1)
       (let $ps (tcalc-module:append $p1 $p2) (Flat (LCons $a2 $as2) $ps $n2)))))

; the written premises, each after the calls of its arguments: (Prems PREMISES NEXT)
(= (tcalc-module:prems LNil $js $n) (Prems LNil $n))
(= (tcalc-module:prems (LCons $p $rest) $js $n)
   (case $p
     (((Congruence (PApp $k $args) $tgt)
       (let (Flat $args2 $ps $n1) (tcalc-module:flats $args $js $n)
         (let (Prems $more $n2) (tcalc-module:prems $rest $js $n1)
           (let $all (tcalc-module:append $ps (LCons (Call (PApp $k $args2) $tgt) $more)) (Prems $all $n2)))))
      ((RelationQuery $rel $args)
       (let (Flat $args2 $ps $n1) (tcalc-module:flats $args $js $n)
         (let (Prems $more $n2) (tcalc-module:prems $rest $js $n1)
           (let $all (tcalc-module:append $ps (LCons (Query (PApp $rel $args2)) $more)) (Prems $all $n2)))))
      ($other (Prems (LCons (Unread $other) LNil) $n)))))

; ---- every variable a rule makes is bound by what it reads ----
(= (tcalc-module:vars $t $acc)
   (case $t
     (((FVar $x) (if (tcalc-module:member $x $acc) $acc (LCons $x $acc)))
      ((PApp $c LNil) $acc)
      ((PApp $c (LCons $a $as)) (let $a2 (tcalc-module:vars $a $acc) (tcalc-module:vars (PApp $c $as) $a2)))
      ($other $acc))))
(= (tcalc-module:within LNil $bound) True)
(= (tcalc-module:within (LCons $x $xs) $bound)
   (if (tcalc-module:member $x $bound) (tcalc-module:within $xs $bound) False))
(= (tcalc-module:bound-ok LNil $bound $r) (tcalc-module:within (tcalc-module:vars $r LNil) $bound))
(= (tcalc-module:bound-ok (LCons $p $rest) $bound $r)
   (case $p
     (((Call $src $t)
       (if (tcalc-module:within (tcalc-module:vars $src LNil) $bound)
           (let $b2 (tcalc-module:vars $t $bound) (tcalc-module:bound-ok $rest $b2 $r))
           False))
      ((Query $q) (let $b2 (tcalc-module:vars $q $bound) (tcalc-module:bound-ok $rest $b2 $r)))
      ($other False))))

; a form the reading does not have, anywhere in a rule's premises or right side
(= (tcalc-module:unread-term $t)
   (case $t
     (((Unread $x) True)
      ((PApp $c LNil) False)
      ((PApp $c (LCons $a $as)) (if (tcalc-module:unread-term $a) True (tcalc-module:unread-term (PApp $c $as))))
      ($other False))))
(= (tcalc-module:unread LNil $r) (tcalc-module:unread-term $r))
(= (tcalc-module:unread (LCons $p $rest) $r)
   (case $p
     (((Unread $x) True)
      ((Call $src $t) (if (tcalc-module:unread-term $src) True (tcalc-module:unread $rest $r)))
      ((Query $q) (if (tcalc-module:unread-term $q) True (tcalc-module:unread $rest $r)))
      ($other True))))

; ---- stages ----
(= (tcalc-module:stage $l $i $d $ts) (format-args "({}/{} {} {})" ($l $i $d $ts)))
(= (tcalc-module:join $a $b) (if (== $a "") $b (format-args "{} {}" ($a $b))))

; the rules of premises I.. given stage I-1's row text PREV and its targets text TS
(= (tcalc-module:steps $l $i $d LNil $prev $ts $r)
   (let $rt (tcalc-module:text $r)
     (format-args "(rule {} (read {}) (make (value {} {})))\n" ($l $prev $d $rt))))
(= (tcalc-module:steps $l $i $d (LCons $p $rest) $prev $ts $r)
   (case $p
     (((Call $call $t)
       (let* (($ct (tcalc-module:text $call))
              ($tt (tcalc-module:text $t))
              ($ts2 (tcalc-module:join $ts $tt))
              ($next (tcalc-module:stage $l $i $d $ts2))
              ($ask (format-args "(rule {}/{}.ask (read {}) (make (demand {})))\n" ($l $i $prev $ct)))
              ($got (format-args "(rule {}/{} (read {} (value {} {})) (make {}))\n" ($l $i $prev $ct $tt $next)))
              ($i2 (+ $i 1))
              ($tail (tcalc-module:steps $l $i2 $d $rest $next $ts2 $r)))
         (format-args "{}{}{}" ($ask $got $tail))))
      ((Query $q)
       (let* (($qt (tcalc-module:text $q))
              ($ts2 (tcalc-module:join $ts $qt))
              ($next (tcalc-module:stage $l $i $d $ts2))
              ($got (format-args "(rule {}/{} (read {} {}) (make {}))\n" ($l $i $prev $qt $next)))
              ($i2 (+ $i 1))
              ($tail (tcalc-module:steps $l $i2 $d $rest $next $ts2 $r)))
         (format-args "{}{}" ($got $tail)))))))

; one rule: (Read TEXT) or (Refused LABEL REASON)
(= (tcalc-module:rule $js $rels $x)
   (case $x
     (((ScopedRewriteRuleV1 $l $b $ps $lhs $rhs RuleBindingNone)
       (let* (((Prems $prems $n1) (tcalc-module:prems $ps $js 0))
              ((Flat $r $rprems $n2) (tcalc-module:flat $rhs $js $n1))
              ($all (tcalc-module:append $prems $rprems))
              ($stages (tcalc-module:length $all)))
         (if (tcalc-module:unread $all $r)
             (Refused $l "a premise or term form the reading does not have")
             (if (tcalc-module:stage-clash $l 1 $stages $rels)
                 (Refused $l "a relation named as one of this rule's stages")
                 (if (tcalc-module:bound-ok $all (tcalc-module:vars $lhs LNil) $r)
                     (let* (($d (tcalc-module:text $lhs)) ($first (format-args "(demand {})" ($d))))
                       (Read (tcalc-module:steps $l 1 $d $all $first "" $r)))
                     (Refused $l "a variable used before any read binds it"))))))
      ((ScopedRewriteRuleV1 $l $b $ps $lhs $rhs $other) (Refused $l "a binding annotation"))
      ($other (Refused "?" "a row other than a rewrite rule")))))

; (Rules TEXT REFUSED): the program's rules, and (LABEL REASON) per rule refused
(= (tcalc-module:rules $js $rels LNil) (Rules "" LNil))
(= (tcalc-module:rules $js $rels (LCons $x $rest))
   (let* (($one (tcalc-module:rule $js $rels $x)) ((Rules $text $refused) (tcalc-module:rules $js $rels $rest)))
     (case $one
       (((Read $a) (let $t (format-args "{}{}" ($a $text)) (Rules $t $refused)))
        ((Refused $l $why) (Rules $text (LCons ($l $why) $refused)))))))

; The program as a string, or (tcalc:module-refused REASON DETAIL).
(= (tcalc:module-text $path $theory)
   (let* (($reader (langdef:load-baked-parser "module-core-reader"))
          ($module (module:read-file $reader "module" $path))
          ($closed (langdef:close-parser $reader))
          ($assembled (module:assemble $module $theory LNil LNil OriginViewV1 "tcalc:module"))
          ($core (module:core $assembled $theory)))
     (case $core
       (((LanguageDefV2 $name $types $terms LNil $rws)
         (let* (($js (tcalc-module:judgments $rws LNil))
                ($rels (tcalc-module:relations $rws LNil))
                ((Rules $body $refused) (tcalc-module:rules $js $rels $rws)))
           (if (or (tcalc-module:member "demand" $rels) (tcalc-module:member "value" $rels))
             (tcalc:module-refused "a relation named demand or value would read the reading's own rows" $rels)
           (case $refused
             ((LNil (format-args "; module {}, theory {}, read by demand\n(store set)\n(step every)\n{}" ($name $theory $body)))
              ($some (tcalc:module-refused "rules the reading does not have" $some)))))))
        ((LanguageDefV2 $name $types $terms $eqs $rws) (tcalc:module-refused "the core has equations" $eqs))
        ($other (tcalc:module-refused "the module reader gave no theory core" $theory))))))

; The program written to OUT: (tcalc:module-written OUT), or the refusal.
(= (tcalc:module-write $path $theory $out)
   (let $text (tcalc:module-text $path $theory)
     (case $text
       (((tcalc:module-refused $why $detail) $text)
        ($s (let $w (fs:write $out $s) (tcalc:module-written $out)))))))
