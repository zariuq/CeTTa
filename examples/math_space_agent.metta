; Run with --lang prime --no-results. Each input line is one JSON request.
!(import! &self math_space_agent)
!(bind! &math-workspace (new-state (math:new "math")))
!(let* (($text (system:getenv-or-default "CETTA_MATH_REPLY_CHARACTERS" "16384"))
        ($characters (parse $text)))
   (if (and (math:natural? $characters) (> $characters 0))
     (math:serve &math-workspace $characters)
     (system:exit 2)))
