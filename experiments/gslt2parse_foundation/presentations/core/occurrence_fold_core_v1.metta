; A language-neutral contract language for streaming folds over parser
; occurrences.  Token roles and node operations are authored by a guest
; presentation; the generic compiler only checks and transports them.

(gslt-presentation-v1 OccurrenceFoldCoreV1
  (signature
    (operator fold-cons 2)
    (operator fold-contract-match 3)
    (operator fold-contract-valid 1)
    (operator fold-one 1)
    (operator fold-plus 1)
    (operator fold-seq 2)
    (operator fold-star 1)
    (operator source-fold-node 3)
    (operator source-fold-role 1)
    (operator source-fold-shift 2)
    (operator source-fold-token 2)
  )
  (equations)
  (rewrites
    (rule fold-contract-valid-none
      (head (fold-contract-valid fold-none))
      (body))
    (rule fold-contract-valid-one
      (head (fold-contract-valid (fold-one ?role)))
      (body (source-fold-role ?role)))
    (rule fold-contract-valid-plus
      (head (fold-contract-valid (fold-plus ?role)))
      (body (source-fold-role ?role)))
    (rule fold-contract-valid-star
      (head (fold-contract-valid (fold-star ?role)))
      (body (source-fold-role ?role)))
    (rule fold-contract-valid-seq
      (head
        (fold-contract-valid (fold-seq ?first ?second)))
      (body
        (fold-contract-valid ?first)
        (fold-contract-valid ?second)))

    (rule fold-contract-match-none
      (head (fold-contract-match fold-none ?input ?input))
      (body))
    (rule fold-contract-match-one
      (head
        (fold-contract-match
          (fold-one ?role)
          (fold-cons ?role ?rest)
          ?rest))
      (body (source-fold-role ?role)))
    (rule fold-contract-match-plus
      (head
        (fold-contract-match
          (fold-plus ?role)
          (fold-cons ?role ?input)
          ?rest))
      (body
        (source-fold-role ?role)
        (fold-contract-match (fold-star ?role) ?input ?rest)))
    (rule fold-contract-match-star-empty
      (head
        (fold-contract-match (fold-star ?role) ?input ?input))
      (body (source-fold-role ?role)))
    (rule fold-contract-match-star-step
      (head
        (fold-contract-match
          (fold-star ?role)
          (fold-cons ?role ?input)
          ?rest))
      (body
        (source-fold-role ?role)
        (fold-contract-match (fold-star ?role) ?input ?rest)))
    (rule fold-contract-match-seq
      (head
        (fold-contract-match
          (fold-seq ?first ?second) ?input ?rest))
      (body
        (fold-contract-match ?first ?input ?middle)
        (fold-contract-match ?second ?middle ?rest)))
  ))
