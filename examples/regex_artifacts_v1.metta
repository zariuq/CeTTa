!(import! &self re)

; The source pattern and its original owner are not needed by a restored owner.
!(let* (($original (re:compile "(?<word>λ+)"))
        ($bytes (re:store $original))
        ($closed (re:close $original))
        ($restored (re:restore $bytes))
        ($answer (re:captures $restored "aλλb"))
        ($done (re:close $restored)))
   (function (return $answer)))

; Constructed patterns use the same artifact and execution boundary.
!(let* (($original (re:compile-value (re:literal 97)))
        ($bytes (re:store $original))
        ($restored (re:restore $bytes))
        ($closed (re:close $original))
        ($answer (re:find $restored "ba"))
        ($done (re:close $restored)))
   (function (return $answer)))

; Malformed artifacts are operation faults, never a completed negative match.
!(re:restore "not a compiled pattern")
