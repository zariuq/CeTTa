!(println! (neq-equal (!= term term)))
!(println! (neq-distinct (!= term other)))
!(println! (neq-false-branch (if (!= term term) bad good)))
!(println! (neq-true-branch (if (!= term other) good bad)))
