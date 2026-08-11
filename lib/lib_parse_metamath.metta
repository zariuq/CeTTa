; Grammar-derived Metamath source parsing for lib_parse.
;
; The grammar is ordinary data consumed by gparse's deterministic SLR backend.
; This module owns lexical classification and source grammar only; proof
; admission remains the responsibility of the generic inference checker.

!(import! &self gparse)
!(import! &self fs)
!(import! &self lib_parse_metamath_grammar_generated_v0)
!(import! &self str)
!(import! &self text_stream)

(= (lib_parse:mm-label $value) (Lex mm-label $value))
(= (lib_parse:mm-symbol $value) (Lex mm-symbol $value))
(= (lib_parse:mm-proof-label $value) (Lex mm-proof-label $value))
(= (lib_parse:mm-compressed-word $value) (Lex mm-compressed-word $value))
(= (lib_parse:mm-include-path $value) (Lex mm-include-path $value))

; ---------- Metamath lexical layer ----------------------------------------

; A source label consists only of ASCII alphanumerics plus '-', '_', and '.'.
; The proof placeholder '?' is handled separately and is not a label.
(= (lib_parse:mm-label-char-valid $char)
   (case
     (str:find
       "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_."
       $char)
     (((TextNotFound $_missing) False)
      ($index True))))

(= (lib_parse:mm-label-valid-from $label $index $length)
   (if (>= $index $length)
     True
     (if
       (lib_parse:mm-label-char-valid
         (str:slice $label $index (+ $index 1)))
       (lib_parse:mm-label-valid-from $label (+ $index 1) $length)
       False)))

(= (lib_parse:mm-label-valid $label)
   (let $length (str:length $label)
     (if (== $length 0)
       False
       (lib_parse:mm-label-valid-from $label 0 $length))))

(= (lib_parse:mm-proof-reference-valid $token)
   (or (== $token "?") (lib_parse:mm-label-valid $token)))

(= (lib_parse:mm-control-token $token)
   (if (== $token "$c") True
   (if (== $token "$v") True
   (if (== $token "$d") True
   (if (== $token "$f") True
   (if (== $token "$e") True
   (if (== $token "$a") True
   (if (== $token "$p") True
   (if (== $token "$=") True
   (if (== $token "$.") True
   (if (== $token "${") True
   (if (== $token "$}") True
   (if (== $token "$[") True
   (if (== $token "$]") True
   (if (== $token "$(") True
   (if (== $token "$") True
       False))))))))))))))))

(= (lib_parse:mm-read-token $stream)
   (case (text-stream:read-token $stream)
     ((AtEof AtEof)
      ((TokenResult $token $next)
       (lib_parse:mm-read-token-classify $token $next))
      ($error $error))))

(= (lib_parse:mm-read-token-classify $token $next)
   (if (== $token "$(")
     (lib_parse:mm-skip-comment $next)
     (TokenResult $token $next)))

(= (lib_parse:mm-skip-comment $stream)
   (case (text-stream:read-until $stream "$)")
     (((ReadUntilResult $body $next)
       (lib_parse:mm-read-token (text-stream:advance $next 2)))
      ((TextNotFound $_needle) (Rejected (mm-lex unclosed-comment)))
      ($error $error))))

(= (lib_parse:mm-emit $token $next $state)
   (MMEmit $token $next $state))

(= (lib_parse:mm-start-token $token $next)
   (if (== $token "$c") (lib_parse:mm-emit MM_C $next Symbols)
   (if (== $token "$v") (lib_parse:mm-emit MM_V $next Symbols)
   (if (== $token "$d") (lib_parse:mm-emit MM_D $next Symbols)
   (if (== $token "${") (lib_parse:mm-emit MM_SCOPE_OPEN $next Start)
   (if (== $token "$}") (lib_parse:mm-emit MM_SCOPE_CLOSE $next Start)
   (if (== $token "$[") (lib_parse:mm-emit MM_INCLUDE_OPEN $next IncludePath)
   (if (lib_parse:mm-control-token $token)
     (Rejected (mm-lex bad-start-token $token))
     (if (lib_parse:mm-label-valid $token)
       (lib_parse:mm-emit (lib_parse:mm-label $token) $next AfterLabel)
       (Rejected (mm-lex invalid-label $token)))))))))))

(= (lib_parse:mm-after-label-token $token $next)
   (if (== $token "$f") (lib_parse:mm-emit MM_F $next Symbols)
   (if (== $token "$e") (lib_parse:mm-emit MM_E $next Symbols)
   (if (== $token "$a") (lib_parse:mm-emit MM_A $next Symbols)
   (if (== $token "$p") (lib_parse:mm-emit MM_P $next Theorem)
     (Rejected (mm-lex bad-label-continuation $token)))))))

(= (lib_parse:mm-symbol-token $token $next)
   (if (== $token "$.")
     (lib_parse:mm-emit MM_DOT $next Start)
     (if (lib_parse:mm-control-token $token)
       (Rejected (mm-lex bad-symbol-token $token))
       (lib_parse:mm-emit (lib_parse:mm-symbol $token) $next Symbols))))

(= (lib_parse:mm-theorem-token $token $next)
   (if (== $token "$=")
     (lib_parse:mm-emit MM_EQPROOF $next ProofStart)
     (if (lib_parse:mm-control-token $token)
       (Rejected (mm-lex bad-theorem-token $token))
       (lib_parse:mm-emit (lib_parse:mm-symbol $token) $next Theorem))))

(= (lib_parse:mm-proof-start-token $token $next)
   (if (== $token "(")
     (lib_parse:mm-emit MM_LP $next ProofHeader)
     (if (== $token "?")
       (lib_parse:mm-emit MM_UNKNOWN $next ProofNormal)
     (if (lib_parse:mm-control-token $token)
       (Rejected (mm-lex bad-proof-token $token))
       (if (lib_parse:mm-proof-reference-valid $token)
         (lib_parse:mm-emit (lib_parse:mm-proof-label $token) $next ProofNormal)
         (Rejected (mm-lex invalid-proof-reference $token)))))))

(= (lib_parse:mm-proof-normal-token $token $next)
   (if (== $token "$.")
     (lib_parse:mm-emit MM_DOT $next Start)
     (if (or (== $token "(") (== $token ")"))
       (Rejected (mm-lex bad-proof-token $token))
       (if (== $token "?")
         (lib_parse:mm-emit MM_UNKNOWN $next ProofNormal)
       (if (lib_parse:mm-control-token $token)
         (Rejected (mm-lex bad-proof-token $token))
         (if (lib_parse:mm-proof-reference-valid $token)
           (lib_parse:mm-emit
             (lib_parse:mm-proof-label $token) $next ProofNormal)
           (Rejected (mm-lex invalid-proof-reference $token))))))))

(= (lib_parse:mm-proof-header-token $token $next)
   (if (== $token ")")
     (lib_parse:mm-emit MM_RP $next ProofBody)
     (if (== $token "(")
       (Rejected (mm-lex bad-proof-header-token $token))
       (if (lib_parse:mm-control-token $token)
         (Rejected (mm-lex bad-proof-header-token $token))
         (if (lib_parse:mm-label-valid $token)
           (lib_parse:mm-emit
             (lib_parse:mm-proof-label $token) $next ProofHeader)
           (Rejected (mm-lex invalid-proof-label $token)))))))

(= (lib_parse:mm-proof-body-token $token $next)
   (if (== $token "$.")
     (lib_parse:mm-emit MM_DOT $next Start)
     (if (or (== $token "(") (== $token ")"))
       (Rejected (mm-lex bad-compressed-token $token))
       (if (lib_parse:mm-control-token $token)
         (Rejected (mm-lex bad-compressed-token $token))
         (lib_parse:mm-emit
           (lib_parse:mm-compressed-word $token) $next ProofBody)))))

(= (lib_parse:mm-include-path-token $token $next)
   (if (lib_parse:mm-control-token $token)
     (Rejected (mm-lex bad-include-path $token))
     (lib_parse:mm-emit
       (lib_parse:mm-include-path $token) $next IncludeClose)))

(= (lib_parse:mm-include-close-token $token $next)
   (if (== $token "$]")
     (lib_parse:mm-emit MM_INCLUDE_CLOSE $next Start)
     (Rejected (mm-lex bad-include-close $token))))

(= (lib_parse:mm-lex-finish Start $reverse) (Ok (rev $reverse)))
(= (lib_parse:mm-lex-finish AfterLabel $reverse)
   (Rejected (mm-lex unexpected-eof AfterLabel)))
(= (lib_parse:mm-lex-finish Symbols $reverse)
   (Rejected (mm-lex unexpected-eof Symbols)))
(= (lib_parse:mm-lex-finish Theorem $reverse)
   (Rejected (mm-lex unexpected-eof Theorem)))
(= (lib_parse:mm-lex-finish ProofStart $reverse)
   (Rejected (mm-lex unexpected-eof ProofStart)))
(= (lib_parse:mm-lex-finish ProofNormal $reverse)
   (Rejected (mm-lex unexpected-eof ProofNormal)))
(= (lib_parse:mm-lex-finish ProofHeader $reverse)
   (Rejected (mm-lex unexpected-eof ProofHeader)))
(= (lib_parse:mm-lex-finish ProofBody $reverse)
   (Rejected (mm-lex unexpected-eof ProofBody)))
(= (lib_parse:mm-lex-finish IncludePath $reverse)
   (Rejected (mm-lex unexpected-eof IncludePath)))
(= (lib_parse:mm-lex-finish IncludeClose $reverse)
   (Rejected (mm-lex unexpected-eof IncludeClose)))

(= (lib_parse:mm-lex-step Start $token $next)
   (lib_parse:mm-start-token $token $next))
(= (lib_parse:mm-lex-step AfterLabel $token $next)
   (lib_parse:mm-after-label-token $token $next))
(= (lib_parse:mm-lex-step Symbols $token $next)
   (lib_parse:mm-symbol-token $token $next))
(= (lib_parse:mm-lex-step Theorem $token $next)
   (lib_parse:mm-theorem-token $token $next))
(= (lib_parse:mm-lex-step ProofStart $token $next)
   (lib_parse:mm-proof-start-token $token $next))
(= (lib_parse:mm-lex-step ProofNormal $token $next)
   (lib_parse:mm-proof-normal-token $token $next))
(= (lib_parse:mm-lex-step ProofHeader $token $next)
   (lib_parse:mm-proof-header-token $token $next))
(= (lib_parse:mm-lex-step ProofBody $token $next)
   (lib_parse:mm-proof-body-token $token $next))
(= (lib_parse:mm-lex-step IncludePath $token $next)
   (lib_parse:mm-include-path-token $token $next))
(= (lib_parse:mm-lex-step IncludeClose $token $next)
   (lib_parse:mm-include-close-token $token $next))

(= (lib_parse:mm-lex-go $stream $state $reverse)
   (case (lib_parse:mm-read-token $stream)
     ((AtEof (lib_parse:mm-lex-finish $state $reverse))
      ((TokenResult $token $next)
       (case (lib_parse:mm-lex-step $state $token $next)
         (((MMEmit $output $next2 $state2)
           (lib_parse:mm-lex-go
             $next2 $state2 (Cons $output $reverse)))
          ($error $error))))
      ($error $error))))

(= (lib_parse:mm-lex-string $text)
   (lib_parse:mm-lex-go (text-stream:new $text) Start Nil))

(= (lib_parse:mm-lex-file $path)
   (case (fs:read-text $path)
     (((Error $call $reason)
       (Rejected (mm-lex file-read $path $reason)))
      ($text (lib_parse:mm-lex-string $text)))))

; The caller supplies the source file containing the grammar definition.  This
; keeps import resolution explicit and permits identity hashing at admission.
(= (lib_parse:mm-parse-database-shared $grammar-path $tokens)
   (gparse:slr-parse-shared
     $grammar-path lib_parse:mm-source-grammar outer_database $tokens))

(= (lib_parse:mm-parse-statement-shared $grammar-path $tokens)
   (gparse:slr-parse-shared
     $grammar-path lib_parse:mm-source-grammar outer_statement $tokens))
