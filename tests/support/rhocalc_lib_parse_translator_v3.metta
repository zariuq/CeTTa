!(import! &self lib_parse)
!(import! &self fs)
!(import! &self str)
!(import! &self text_stream)

; Strict-core rho surface via live lib_parse v3.
; Surface syntax matches the current runtime surface:
;   @{P}
;   N!(P)
;   *N
;   P | Q
;   for ($x <- N) { P }
;
; The canonical internal AST is de Bruijn / locally nameless:
;   rho:nil
;   (rho:send <name> <proc>)
;   (rho:drop <name>)
;   (rho:par <proc> <proc>)
;   (rho:recv-db <name> <body>)
;   (rho:quote <proc>)
;   (rho:bound <idx>)
;   (rho:free <id>)

(= (rho-g) (mkGram (
   (VarD x rho_id)(VarD y rho_id)(VarD z rho_id)
   (VarD v rho_id)(VarD seed rho_id)
   (VarD x_rho1 rho_id)(VarD y_rho1 rho_id)(VarD z_rho1 rho_id)
   (VarD x_rho2 rho_id)(VarD y_rho2 rho_id)(VarD z_rho2 rho_id)

   (Prod pnil   proc ((Tm ZERO)))
   (Prod psend  proc ((Hl name)(Tm BANG)(Tm LP)(Hl proc)(Tm RP)))
   (Prod pdrop  proc ((Tm STAR)(Hl name)))
   (Prod ppar   proc ((Hl proc)(Tm BAR)(Hl proc)))
   (Prod precv  proc ((Tm FOR)(Tm LP)(Tm DOLLAR)(HlB rho_id proc)(Tm FROM)(Hl name)(Tm RP)(Tm LBR)(Hl proc)(Tm RBR)))
   (Prod nquot  name ((Tm AT)(Tm LBR)(Hl proc)(Tm RBR)))
   (Prod nvar   name ((Tm DOLLAR)(Hl rho_id))) )))

; Simple ambiguous lane for the negative parser tooth.
(= (rho-g-ambig) (mkGram (
   (VarD x proc)
   (Prod ppar proc ((Hl proc)(Tm BAR)(Hl proc))) )))

; Strict-core subset used to exercise the CorpusChecked-but-not-certified lane.
(= (rho-g-lite) (mkGram (
   (VarD x rho_id)(VarD z rho_id)
   (Prod pnil  proc ((Tm ZERO)))
   (Prod pdrop proc ((Tm STAR)(Hl name)))
   (Prod nquot name ((Tm AT)(Tm LBR)(Hl proc)(Tm RBR)))
   (Prod nvar  name ((Tm DOLLAR)(Hl rho_id))) )))

; --- token fixtures ---
(= (rho-send-nil-toks) (mkL (AT LBR ZERO RBR BANG LP ZERO RP)))
(= (rho-recv-drop-x-toks) (mkL (FOR LP DOLLAR x FROM AT LBR ZERO RBR RP LBR STAR DOLLAR x RBR)))
(= (rho-recv-drop-y-toks) (mkL (FOR LP DOLLAR y FROM AT LBR ZERO RBR RP LBR STAR DOLLAR y RBR)))
(= (rho-pure-surface-toks) (mkL (AT LBR ZERO RBR BANG LP ZERO RP BAR FOR LP DOLLAR x FROM AT LBR ZERO RBR RP LBR STAR DOLLAR x RBR)))
(= (rho-shadowing-surface-toks)
   (mkL (
     FOR LP DOLLAR x FROM AT LBR ZERO RBR RP LBR
       FOR LP DOLLAR x FROM DOLLAR x RP LBR STAR DOLLAR x RBR
       BAR
       DOLLAR x BANG LP ZERO RP
     RBR
     BAR
     AT LBR ZERO RBR BANG LP AT LBR ZERO RBR BANG LP ZERO RP RP)))
(= (rho-free-drop-z-toks) (mkL (STAR DOLLAR z)))
(= (rho-bad-toks) (mkL (AT LBR ZERO BANG LP ZERO RP)))
(= (rho-lite-corpus) (Cons (mkL (ZERO)) (Cons (mkL (STAR DOLLAR z)) Nil)))
(= (rho-ambig-corpus) (Cons (mkL (x BAR x BAR x)) Nil))
(= (rho-corpus-files)
   (mkL (
     "tests/rhocalc/pure_surface.rho"
     "tests/rhocalc/surface_shadowing.rho"
     "tests/rhocalc/surface_name_output.rho"
     "tests/rhocalc/surface_open_name_variable.rho"
     "tests/rhocalc/surface_quote_inner_binder_restores_scope.rho"
     "tests/rhocalc/surface_quote_seals_outer_binder.rho"
     "tests/rhocalc/paper_reflection_name_generation_h4.rho")))

; --- helpers ---
(= (leaf-name (Lf $v)) $v)
(= (memb2 $x Nil) False)
(= (memb2 $x (Cons $y $r)) (if (== $x $y) True (memb2 $x $r)))
(= (idx-of (Cons $x $r) $q $i) (if (== $x $q) $i (idx-of $r $q (+ $i 1))))
(= (idx-of Nil $q $i) (Err unbound))
(= (nth (Cons $x $r) $i) (if (== $i 0) $x (nth $r (- $i 1))))
(= (nth Nil $i) (Err oob))
(= (llen Nil) 0)
(= (llen (Cons $x $r)) (+ 1 (llen $r)))
(= (fresh-name $i) (nth (mkL (x y z x_rho1 y_rho1 z_rho1 x_rho2 y_rho2 z_rho2)) $i))

; --- raw text lexer (untrusted L0 with source spans) ---
(= (rho-alpha-chars)
   (mkL ("a" "b" "c" "d" "e" "f" "g" "h" "i" "j" "k" "l" "m"
         "n" "o" "p" "q" "r" "s" "t" "u" "v" "w" "x" "y" "z"
         "A" "B" "C" "D" "E" "F" "G" "H" "I" "J" "K" "L" "M"
         "N" "O" "P" "Q" "R" "S" "T" "U" "V" "W" "X" "Y" "Z")))
(= (rho-digit-chars)
   (mkL ("0" "1" "2" "3" "4" "5" "6" "7" "8" "9")))
(= (rho-char:is-alpha $ch)
   (memb2 $ch (rho-alpha-chars)))
(= (rho-char:is-digit $ch)
   (memb2 $ch (rho-digit-chars)))
(= (rho-char:is-ident-start $ch)
   (if (== $ch "_") True (rho-char:is-alpha $ch)))
(= (rho-char:is-ident-rest $ch)
   (if (rho-char:is-ident-start $ch) True (rho-char:is-digit $ch)))

(= (rho-ident-stop $ch)
   (if (== $ch " ") True
       (if (== $ch "\t") True
           (if (== $ch "\n") True
               (if (== $ch "\r") True
                   (if (== $ch "(") True
                       (if (== $ch ")") True
                           (if (== $ch "{") True
                               (if (== $ch "}") True
                                   (if (== $ch "!") True
                                       (if (== $ch "|") True
                                           (if (== $ch "$") True
                                               (if (== $ch "*") True
                                                   (if (== $ch "@") True
                                                       (== $ch "<")))))))))))))))

(= (rho-skip-line-comment $cursor)
   (let $ch (text-stream:peek $cursor)
     (if (== $ch Empty)
         $cursor
         (if (== $ch "\n")
             (text-stream:advance $cursor 1)
             (let $next (text-stream:advance $cursor 1)
               (rho-skip-line-comment $next))))))

(= (rho-skip-junk $cursor)
   (let $cursor2 (text-stream:skip-whitespace $cursor)
     (if (text-stream:has-prefix $cursor2 "//")
         (let $after-prefix (text-stream:advance $cursor2 2)
           (let $after-comment (rho-skip-line-comment $after-prefix)
             (rho-skip-junk $after-comment)))
         $cursor2)))

(= (rho-read-ident $cursor)
   (rho-read-ident-acc $cursor ""))
(= (rho-read-ident-acc $cursor $acc)
   (let $ch (text-stream:peek $cursor)
     (if (== $ch Empty)
         (LexText $acc $cursor)
         (if (rho-ident-stop $ch)
             (LexText $acc $cursor)
             (if (rho-char:is-ident-rest $ch)
                 (let $next (text-stream:advance $cursor 1)
                   (rho-read-ident-acc
                     $next
                     (str:concat $acc $ch)))
                 (LexBad $ch $cursor))))))

(= (rho-keyword-delim $ch)
   (if (== $ch " ") True
       (if (== $ch "\t") True
           (if (== $ch "\n") True
               (if (== $ch "\r") True
                   (== $ch "("))))))

(= (rho-keyword-boundary $cursor $kw)
   (let $after-kw (text-stream:advance $cursor (str:length $kw))
     (let $next (text-stream:peek $after-kw)
       (if (== $next Empty) True (rho-keyword-delim $next)))))

(= (rho-strip-spans Nil) Nil)
(= (rho-strip-spans (Cons (STok $tok $start $end) $rest))
   (Cons $tok (rho-strip-spans $rest)))
(= (rho-lex-result->toks (Ok $spans))
   (rho-strip-spans $spans))
(= (rho-lex-result->toks (Rejected $why))
   (Rejected $why))
(= (rho-lex-string->toks $text)
   (rho-lex-result->toks (rho-lex-string $text)))
(= (rho-lex-file->toks $path)
   (rho-lex-result->toks (rho-lex-file $path)))

(= (rho-lex-string $text)
   (rho-lex-cursor-ready $text 0 Nil))
(= (rho-lex-cursor $cursor $rev)
   (case (rho-skip-junk $cursor)
     (((TextCursor $text $pos)
       (rho-lex-cursor-ready $text $pos $rev))
      ($o (Rejected (lex bad-cursor $cursor))))))
(= (rho-lex-cursor-ready $text $pos $rev)
   (case (rho-skip-junk (TextCursor $text $pos))
     (((TextCursor $text2 $pos2)
       (if (text-stream:is-done (TextCursor $text2 $pos2))
           (Ok (rev $rev))
           (rho-lex-step-ready $text2 $pos2 $rev)))
      ($o (Rejected (lex bad-cursor (TextCursor $text $pos)))))))

(= (rho-lex-step $cursor $rev)
   (case $cursor
     (((TextCursor $text $pos)
       (rho-lex-step-ready $text $pos $rev))
      ($o (Rejected (lex bad-cursor $cursor))))))
(= (rho-lex-step-ready $text $pos $rev)
   (if (text-stream:has-prefix (TextCursor $text $pos) "<-")
       (rho-lex-cursor-ready
         $text
         (+ $pos 2)
         (Cons (STok FROM $pos (+ $pos 2)) $rev))
       (if (text-stream:has-prefix (TextCursor $text $pos) "for")
           (if (rho-keyword-boundary (TextCursor $text $pos) "for")
               (rho-lex-cursor-ready
                 $text
                 (+ $pos 3)
                 (Cons (STok FOR $pos (+ $pos 3)) $rev))
               (rho-lex-char-step-ready $text $pos $rev))
           (rho-lex-char-step-ready $text $pos $rev))))

(= (rho-lex-push $cursor $rev $tok)
   (case $cursor
     (((TextCursor $text $pos)
       (rho-lex-cursor-ready
         $text
         (+ $pos 1)
         (Cons (STok $tok $pos (+ $pos 1)) $rev)))
      ($o (Rejected (lex bad-cursor $cursor))))))

(= (rho-lex-char-step $cursor $rev $pos)
   (case $cursor
     (((TextCursor $text $cursor-pos)
       (rho-lex-char-step-ready $text $cursor-pos $rev))
      ($o (Rejected (lex bad-cursor $cursor))))))
(= (rho-lex-char-step-ready $text $pos $rev)
   (case (text-stream:peek (TextCursor $text $pos))
     (("@" (rho-lex-cursor-ready $text (+ $pos 1) (Cons (STok AT $pos (+ $pos 1)) $rev)))
      ("{" (rho-lex-cursor-ready $text (+ $pos 1) (Cons (STok LBR $pos (+ $pos 1)) $rev)))
      ("}" (rho-lex-cursor-ready $text (+ $pos 1) (Cons (STok RBR $pos (+ $pos 1)) $rev)))
      ("!" (rho-lex-cursor-ready $text (+ $pos 1) (Cons (STok BANG $pos (+ $pos 1)) $rev)))
      ("(" (rho-lex-cursor-ready $text (+ $pos 1) (Cons (STok LP $pos (+ $pos 1)) $rev)))
      (")" (rho-lex-cursor-ready $text (+ $pos 1) (Cons (STok RP $pos (+ $pos 1)) $rev)))
      ("|" (rho-lex-cursor-ready $text (+ $pos 1) (Cons (STok BAR $pos (+ $pos 1)) $rev)))
      ("$" (rho-lex-cursor-ready $text (+ $pos 1) (Cons (STok DOLLAR $pos (+ $pos 1)) $rev)))
      ("*" (rho-lex-cursor-ready $text (+ $pos 1) (Cons (STok STAR $pos (+ $pos 1)) $rev)))
      ("0" (rho-lex-cursor-ready $text (+ $pos 1) (Cons (STok ZERO $pos (+ $pos 1)) $rev)))
      ($ch (if (rho-char:is-ident-start $ch)
               (rho-lex-ident-step (TextCursor $text $pos) $rev $pos)
               (Rejected (lex unexpected-char $pos $ch)))))))

(= (rho-lex-ident-step $cursor $rev $pos)
   (case (rho-read-ident $cursor)
     (((LexText $text (TextCursor $src $nextpos))
       (case (parse $text)
         (((Error $call $why) (Rejected (lex bad-ident $pos $text $why)))
          ($tok
           (rho-lex-cursor-ready
             $src
             $nextpos
             (Cons (STok $tok $pos $nextpos) $rev))))))
      ((LexBad $bad $badcursor)
       (Rejected (lex unexpected-char (text-stream:pos $badcursor) $bad)))
      ($o (Rejected (lex bad-ident-state $pos))))))

(= (rho-lex-file $path)
   (case (fs:read-text $path)
     (((Error $call $why) (Rejected (lex fs-read $path $why)))
      ($text (rho-lex-string $text)))))

; --- entry points through live lib_parse ---
(= (rho-parse-raw $toks) (lp-parse-raw (rho-g) proc $toks))
(= (rho-parse-tree $toks) (lp-parse (rho-g) proc $toks))
(= (rho-raw-tree $toks) (pu-tree (rho-parse-raw $toks)))
(= (rho-canon-tree $toks) (pu-tree (rho-parse-tree $toks)))

; --- canonical lowering from lib_parse's canonical tree ---
(= (rho-canon-proc (Nd pnil $k)) rho:nil)
(= (rho-canon-proc (Nd psend (Cons $N (Cons $P Nil))))
   (rho:send (rho-canon-name $N) (rho-canon-proc $P)))
(= (rho-canon-proc (Nd pdrop (Cons $N Nil)))
   (rho:drop (rho-canon-name $N)))
(= (rho-canon-proc (Nd ppar (Cons $P (Cons $Q Nil))))
   (rho:par (rho-canon-proc $P) (rho-canon-proc $Q)))
(= (rho-canon-proc (Nd precv (Cons (Bd rho_id proc) (Cons $N (Cons $P Nil)))))
   (rho:recv-db (rho-canon-name $N) (rho-canon-proc $P)))

(= (rho-canon-name (Nd nquot (Cons $P Nil)))
   (rho:quote (rho-canon-proc $P)))
(= (rho-canon-name (Nd nvar (Cons (Bv $i) Nil)))
   (rho:bound $i))
(= (rho-canon-name (Nd nvar (Cons (Lf $v) Nil)))
   (rho:free $v))

(= (rho->canon $toks)
   (case (rho-parse-tree $toks)
     (((Unique $t) (Ok (rho-canon-proc $t)))
      (Ambiguous (Rejected ambiguous))
      (NoParse (Rejected noparse))
      ($o (Rejected bad-parse-result)))))

(= (fresh-name-text $i)
   (nth (mkL ("x" "y" "z" "x_rho1" "y_rho1" "z_rho1" "x_rho2" "y_rho2" "z_rho2")) $i))

(= (rho-ok-append (Ok $lhs) (Ok $rhs))
   (Ok (str:concat $lhs $rhs)))
(= (rho-ok-append (Err $msg) $rhs) (Err $msg))
(= (rho-ok-append (Ok $lhs) (Err $msg)) (Err $msg))

(= (rho-canon-render-name (rho:quote $proc) $env)
   (rho-ok-append
     (rho-ok-append (Ok "(rho:quote ") (rho-canon-render-proc $proc $env))
     (Ok ")")))
(= (rho-canon-render-name (rho:bound $i) $env)
   (if (and (>= $i 0) (< $i (llen $env)))
       (Ok (str:concat "$" (nth $env $i)))
       (Err "rho:bound index out of scope")))
(= (rho-canon-render-name (rho:free $v) $env)
   (Ok (str:concat "$" (repr $v))))

(= (rho-canon-render-proc rho:nil $env)
   (Ok "rho:nil"))
(= (rho-canon-render-proc (rho:send $name $proc) $env)
   (rho-ok-append
     (rho-ok-append
       (rho-ok-append
         (rho-ok-append (Ok "(rho:send ") (rho-canon-render-name $name $env))
         (Ok " "))
       (rho-canon-render-proc $proc $env))
     (Ok ")")))
(= (rho-canon-render-proc (rho:drop $name) $env)
   (rho-ok-append
     (rho-ok-append (Ok "(rho:drop ") (rho-canon-render-name $name $env))
     (Ok ")")))
(= (rho-canon-render-proc (rho:par $lhs $rhs) $env)
   (rho-ok-append
     (rho-ok-append
       (rho-ok-append
         (rho-ok-append (Ok "(rho:par ") (rho-canon-render-proc $lhs $env))
         (Ok " "))
       (rho-canon-render-proc $rhs $env))
     (Ok ")")))
(= (rho-canon-render-proc (rho:recv-db $name $body) $env)
   (let $binder (fresh-name-text (llen $env))
     (rho-ok-append
       (rho-ok-append
         (rho-ok-append
           (rho-ok-append
             (rho-ok-append (Ok "(rho:recv ") (rho-canon-render-name $name $env))
             (Ok (str:concat " $" $binder)))
           (Ok " "))
         (rho-canon-render-proc $body (Cons $binder $env)))
       (Ok ")"))))

(= (rho-native-canon->mrho-text $ast)
   (rho-canon-render-proc $ast Nil))

(= (rho-canon-result->mrho-text (Ok $ast))
   (case (rho-native-canon->mrho-text $ast)
     (((Ok $text) $text)
      ($o $o))))
(= (rho-canon-result->mrho-text (Rejected $e)) (Rejected $e))

(= (rho-canon->mrho-text $ast)
   (case (rho-native-canon->mrho-text $ast)
     (((Ok $text) $text)
      ($o $o))))

(= (rho-text->canon $text)
   (case (rho-lex-string $text)
     (((Ok $spans) (rho->canon (rho-strip-spans $spans)))
      ($o $o))))

(= (rho-file->canon $path)
   (case (rho-lex-file $path)
     (((Ok $spans) (rho->canon (rho-strip-spans $spans)))
      ($o $o))))

(= (rho-text->mrho-text $text)
   (rho-canon-result->mrho-text (rho-text->canon $text)))

(= (rho-file->mrho-text $path)
   (rho-canon-result->mrho-text (rho-file->canon $path)))

; --- unambiguity-status lane (outside the TCB) ---
(= (rho-file-corpus-unique Nil) True)
(= (rho-file-corpus-unique (Cons $path $rest))
   (case (rho-lex-file $path)
     (((Ok $spans)
       (if (== (lp-parse-class (rho-g) proc (rho-strip-spans $spans)) Unique)
           (rho-file-corpus-unique $rest)
           False))
      ($o False))))

(= (rho-pnil-shape $tpl)
   (and
     (== (tpl-len $tpl) 1)
     (and
       (tpl-nth-is-tm $tpl 0 ZERO)
       (and
         (== (tpl-count-tm $tpl ZERO) 1)
         (and
           (== (tpl-count-hl $tpl proc) 0)
           (== (tpl-count-hlb $tpl proc proc) 0))))))

(= (rho-psend-shape $tpl)
   (and
     (== (tpl-len $tpl) 5)
     (and
       (tpl-nth-is-hl $tpl 0 name)
       (and
         (tpl-nth-is-tm $tpl 1 BANG)
         (and
           (tpl-nth-is-tm $tpl 2 LP)
           (and
             (tpl-nth-is-hl $tpl 3 proc)
             (and
               (tpl-nth-is-tm $tpl 4 RP)
               (and
                 (== (tpl-count-hl $tpl name) 1)
                 (and
                   (== (tpl-count-hl $tpl proc) 1)
                   (and
                     (== (tpl-count-hlb $tpl proc proc) 0)
                     (and
                       (== (tpl-count-tm $tpl BANG) 1)
                       (and
                         (== (tpl-count-tm $tpl LP) 1)
                         (== (tpl-count-tm $tpl RP) 1)))))))))))))

(= (rho-pdrop-shape $tpl)
   (and
     (== (tpl-len $tpl) 2)
     (and
       (tpl-nth-is-tm $tpl 0 STAR)
       (and
         (tpl-nth-is-hl $tpl 1 name)
         (and
           (== (tpl-count-hl $tpl name) 1)
           (and
             (== (tpl-count-hl $tpl proc) 0)
             (and
               (== (tpl-count-hlb $tpl proc proc) 0)
               (== (tpl-count-tm $tpl STAR) 1))))))))

(= (rho-ppar-shape $tpl)
   (and
     (== (tpl-len $tpl) 3)
     (and
       (tpl-nth-is-hl $tpl 0 proc)
       (and
         (tpl-nth-is-tm $tpl 1 BAR)
         (and
           (tpl-nth-is-hl $tpl 2 proc)
           (and
             (== (tpl-count-hl $tpl proc) 2)
             (and
               (== (tpl-count-hlb $tpl proc proc) 0)
               (== (tpl-count-tm $tpl BAR) 1))))))))

(= (rho-precv-shape $tpl)
   (and
     (== (tpl-len $tpl) 10)
     (and
       (tpl-nth-is-tm $tpl 0 FOR)
       (and
         (tpl-nth-is-tm $tpl 1 LP)
         (and
           (tpl-nth-is-tm $tpl 2 DOLLAR)
           (and
             (tpl-nth-is-hlb $tpl 3 rho_id proc)
             (and
               (tpl-nth-is-tm $tpl 4 FROM)
               (and
                 (tpl-nth-is-hl $tpl 5 name)
                 (and
                   (tpl-nth-is-tm $tpl 6 RP)
                   (and
                     (tpl-nth-is-tm $tpl 7 LBR)
                     (and
                       (tpl-nth-is-hl $tpl 8 proc)
                       (and
                         (tpl-nth-is-tm $tpl 9 RBR)
                         (and
                           (== (tpl-count-hl $tpl name) 1)
                           (and
                             (== (tpl-count-hl $tpl proc) 1)
                             (and
                               (== (tpl-count-hlb $tpl rho_id proc) 1)
                               (and
                                 (== (tpl-count-tm $tpl FOR) 1)
                                 (and
                                   (== (tpl-count-tm $tpl LP) 1)
                                   (and
                                     (== (tpl-count-tm $tpl DOLLAR) 1)
                                     (and
                                       (== (tpl-count-tm $tpl FROM) 1)
                                       (and
                                         (== (tpl-count-tm $tpl RP) 1)
                                         (and
                                           (== (tpl-count-tm $tpl LBR) 1)
                                           (== (tpl-count-tm $tpl RBR) 1))))))))))))))))))))))

(= (rho-nquot-shape $tpl)
   (and
     (== (tpl-len $tpl) 4)
     (and
       (tpl-nth-is-tm $tpl 0 AT)
       (and
         (tpl-nth-is-tm $tpl 1 LBR)
         (and
           (tpl-nth-is-hl $tpl 2 proc)
           (and
             (tpl-nth-is-tm $tpl 3 RBR)
             (and
               (== (tpl-count-hl $tpl proc) 1)
               (and
                 (== (tpl-count-hlb $tpl proc proc) 0)
                 (and
                   (== (tpl-count-tm $tpl AT) 1)
                   (and
                     (== (tpl-count-tm $tpl LBR) 1)
                     (== (tpl-count-tm $tpl RBR) 1)))))))))))

(= (rho-nvar-shape $tpl)
   (and
     (== (tpl-len $tpl) 2)
     (and
       (tpl-nth-is-tm $tpl 0 DOLLAR)
       (and
         (tpl-nth-is-hl $tpl 1 rho_id)
         (and
           (== (tpl-count-hl $tpl rho_id) 1)
           (and
             (== (tpl-count-hlb $tpl rho_id proc) 0)
             (== (tpl-count-tm $tpl DOLLAR) 1)))))))

(= (rho-proc-family-shape $g)
   (and
     (rho-pnil-shape (template-of $g pnil))
     (and
       (rho-psend-shape (template-of $g psend))
       (and
         (rho-pdrop-shape (template-of $g pdrop))
         (and
           (rho-ppar-shape (template-of $g ppar))
           (rho-precv-shape (template-of $g precv)))))))

(= (rho-name-family-shape $g)
   (and
     (rho-nquot-shape (template-of $g nquot))
     (rho-nvar-shape (template-of $g nvar))))

(= (rho-certified-unambiguous $g)
   (and
     (== (lp-admit $g) Admitted)
     (and
       (== (llen (prods-for $g proc)) 5)
       (and
         (== (llen (prods-for $g name)) 2)
         (and
           (rho-proc-family-shape $g)
           (rho-name-family-shape $g))))))

(= (rho-unambiguity-status $g $nt $corpus)
   (lp-unambiguity-status (rho-certified-unambiguous $g) $g $nt $corpus))

(= (rho-file-unambiguity-status $paths)
   (if (rho-certified-unambiguous (rho-g))
       CertifiedUnambiguous
       (if (rho-file-corpus-unique $paths)
           CorpusChecked
           Unchecked)))

; --- canonical AST back to raw named parse tree / surface tokens ---
(= (rho-raise-proc $ctx rho:nil)
   (Nd pnil Nil))
(= (rho-raise-proc $ctx (rho:send $N $P))
   (Nd psend (Cons (rho-raise-name $ctx $N) (Cons (rho-raise-proc $ctx $P) Nil))))
(= (rho-raise-proc $ctx (rho:drop $N))
   (Nd pdrop (Cons (rho-raise-name $ctx $N) Nil)))
(= (rho-raise-proc $ctx (rho:par $P $Q))
   (Nd ppar (Cons (rho-raise-proc $ctx $P) (Cons (rho-raise-proc $ctx $Q) Nil))))
(= (rho-raise-proc $ctx (rho:recv-db $N $body))
   (rho-raise-recv (fresh-name (llen $ctx)) $ctx $N $body))

(= (rho-raise-recv $fv $ctx $N $body)
   (Nd precv
     (Cons (Lf $fv)
       (Cons (rho-raise-name $ctx $N)
         (Cons (rho-raise-proc (Cons $fv $ctx) $body) Nil)))))

(= (rho-raise-name $ctx (rho:quote $P))
   (Nd nquot (Cons (rho-raise-proc $ctx $P) Nil)))
(= (rho-raise-name $ctx (rho:bound $i))
   (Nd nvar (Cons (Lf (nth $ctx $i)) Nil)))
(= (rho-raise-name $ctx (rho:free $v))
   (Nd nvar (Cons (Lf $v) Nil)))

(= (rho-canon->surface-tree $ast) (rho-raise-proc Nil $ast))
(= (rho-canon->surface-toks $ast) (lp-print (rho-g) (rho-canon->surface-tree $ast)))

(= (rho-surface-roundtrip $toks)
   (case (rho->canon $toks)
     (((Ok $ast) (rho-canon->surface-toks $ast))
      ($o $o))))

(= (rho-text-surface-roundtrip $text)
   (case (rho-text->canon $text)
     (((Ok $ast) (rho-canon->surface-toks $ast))
      ($o $o))))

(= (rho-file-surface-roundtrip $path)
   (case (rho-file->canon $path)
     (((Ok $ast) (rho-canon->surface-toks $ast))
      ($o $o))))

(= (rho-canon-roundtrip $toks)
   (case (rho-surface-roundtrip $toks)
     (((Ok $surf) (rho->canon $surf))
      ($o $o))))
