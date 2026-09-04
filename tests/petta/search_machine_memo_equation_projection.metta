!(import! &self (library lib_memo))

!(memoize versioned)
(= (versioned $x) old)

; A data-only mutation leaves the function's equation projection unchanged,
; so the second call replays the first answer.  Adding an equation changes
; that projection, so the following call observes both definitions.
!(collapse (versioned 1))
!(let $_data
     (collapse (add-atom &self (data-only fact)))
     (collapse (versioned 1)))
!(let $_equation
     (collapse (add-atom &self (= (versioned $x) new)))
     (collapse (versioned 1)))
!(get-memoize-stats)
