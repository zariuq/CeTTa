; A token automaton: Tok reads one token after blanks, its rules and the
; relations they call unfolded into one function over byte positions.
; Blanks loop; a newline wraps what follows (Line); a word is a scan whose
; text is tested against keywords in place; "-" looks one scalar ahead for
; "->".  Digits are counted by a recursion that is not a tail call, so that
; part stays a call of its compiled mode.
(gslt-presentation-v1 NativeDirectTokenAutomatonV1
  (signature
    (operator Peek 2)
    (operator AtEnd 0)
    (operator Next 2)
    (operator Tok 2)
    (operator TokPeek 2)
    (operator TokClass 4)
    (operator Class 2)
    (operator MinusPeek 3)
    (operator Kind 2)
    (operator WordTok 4)
    (operator Digits 3)
    (operator DigitsPeek 4)
    (operator DigitsClass 5)
    (operator Letter 2)
    (operator Letters 3)
    (operator LettersPeek 4)
    (operator LettersClass 6)
    (operator Space 0)
    (operator Newline 0)
    (operator Alpha 0)
    (operator Digit 0)
    (operator Minus 0)
    (operator Other 0)
    (operator Done 0)
    (operator Line 2)
    (operator Read 2)
    (operator Num 1)
    (operator Fail 1)
    (operator Arrow 0)
    (operator Let 0)
    (operator In 0)
    (operator Name 1)
    (operator KwLet 0)
    (operator KwIn 0)
    (operator Plain 0)
    (operator Z 0)
    (operator S 1)
    (operator Yes 0)
    (operator No 0)
    (operator Cell 2)
    (operator End 0)
    (operator cons 2)
    (operator nil 0)
    (operator cp 1))
  (equations)
  (rewrites
    (rule peek-end
      (head (Peek nil AtEnd))
      (body))
    (rule peek-next
      (head (Peek (cons ?c ?r) (Next ?c ?r)))
      (body))
    (rule tok
      (head (Tok ?in ?toks))
      (body (Peek ?in ?p) (TokPeek ?p ?toks)))
    (rule tok-end
      (head (TokPeek AtEnd Done))
      (body))
    (rule tok-next
      (head (TokPeek (Next ?c ?r) ?toks))
      (body (Class ?c ?k) (TokClass ?k ?c ?r ?toks)))
    (rule class-space-32
      (head (Class (cp 32) Space))
      (body))
    (rule class-space-9
      (head (Class (cp 9) Space))
      (body))
    (rule class-newline
      (head (Class (cp 10) Newline))
      (body))
    (rule class-letter-97
      (head (Class (cp 97) Alpha))
      (body))
    (rule class-letter-98
      (head (Class (cp 98) Alpha))
      (body))
    (rule class-letter-99
      (head (Class (cp 99) Alpha))
      (body))
    (rule class-letter-100
      (head (Class (cp 100) Alpha))
      (body))
    (rule class-letter-101
      (head (Class (cp 101) Alpha))
      (body))
    (rule class-letter-102
      (head (Class (cp 102) Alpha))
      (body))
    (rule class-letter-103
      (head (Class (cp 103) Alpha))
      (body))
    (rule class-letter-104
      (head (Class (cp 104) Alpha))
      (body))
    (rule class-letter-105
      (head (Class (cp 105) Alpha))
      (body))
    (rule class-letter-106
      (head (Class (cp 106) Alpha))
      (body))
    (rule class-letter-107
      (head (Class (cp 107) Alpha))
      (body))
    (rule class-letter-108
      (head (Class (cp 108) Alpha))
      (body))
    (rule class-letter-109
      (head (Class (cp 109) Alpha))
      (body))
    (rule class-letter-110
      (head (Class (cp 110) Alpha))
      (body))
    (rule class-letter-111
      (head (Class (cp 111) Alpha))
      (body))
    (rule class-letter-112
      (head (Class (cp 112) Alpha))
      (body))
    (rule class-letter-113
      (head (Class (cp 113) Alpha))
      (body))
    (rule class-letter-114
      (head (Class (cp 114) Alpha))
      (body))
    (rule class-letter-115
      (head (Class (cp 115) Alpha))
      (body))
    (rule class-letter-116
      (head (Class (cp 116) Alpha))
      (body))
    (rule class-letter-117
      (head (Class (cp 117) Alpha))
      (body))
    (rule class-letter-118
      (head (Class (cp 118) Alpha))
      (body))
    (rule class-letter-119
      (head (Class (cp 119) Alpha))
      (body))
    (rule class-letter-120
      (head (Class (cp 120) Alpha))
      (body))
    (rule class-letter-121
      (head (Class (cp 121) Alpha))
      (body))
    (rule class-letter-122
      (head (Class (cp 122) Alpha))
      (body))
    (rule class-digit-48
      (head (Class (cp 48) Digit))
      (body))
    (rule class-digit-49
      (head (Class (cp 49) Digit))
      (body))
    (rule class-digit-50
      (head (Class (cp 50) Digit))
      (body))
    (rule class-digit-51
      (head (Class (cp 51) Digit))
      (body))
    (rule class-digit-52
      (head (Class (cp 52) Digit))
      (body))
    (rule class-digit-53
      (head (Class (cp 53) Digit))
      (body))
    (rule class-digit-54
      (head (Class (cp 54) Digit))
      (body))
    (rule class-digit-55
      (head (Class (cp 55) Digit))
      (body))
    (rule class-digit-56
      (head (Class (cp 56) Digit))
      (body))
    (rule class-digit-57
      (head (Class (cp 57) Digit))
      (body))
    (rule class-minus
      (head (Class (cp 45) Minus))
      (body))
    (rule class-other
      (head (Class ?c Other))
      (body (different ?c (cp 32)) (different ?c (cp 9)) (different ?c (cp 10)) (different ?c (cp 45)) (different ?c (cp 97)) (different ?c (cp 98)) (different ?c (cp 99)) (different ?c (cp 100)) (different ?c (cp 101)) (different ?c (cp 102)) (different ?c (cp 103)) (different ?c (cp 104)) (different ?c (cp 105)) (different ?c (cp 106)) (different ?c (cp 107)) (different ?c (cp 108)) (different ?c (cp 109)) (different ?c (cp 110)) (different ?c (cp 111)) (different ?c (cp 112)) (different ?c (cp 113)) (different ?c (cp 114)) (different ?c (cp 115)) (different ?c (cp 116)) (different ?c (cp 117)) (different ?c (cp 118)) (different ?c (cp 119)) (different ?c (cp 120)) (different ?c (cp 121)) (different ?c (cp 122)) (different ?c (cp 48)) (different ?c (cp 49)) (different ?c (cp 50)) (different ?c (cp 51)) (different ?c (cp 52)) (different ?c (cp 53)) (different ?c (cp 54)) (different ?c (cp 55)) (different ?c (cp 56)) (different ?c (cp 57))))
    (rule tok-space
      (head (TokClass Space ?c ?r ?toks))
      (body (Tok ?r ?toks)))
    (rule tok-newline
      (head (TokClass Newline ?c ?r (Line ?r ?toks)))
      (body (Tok ?r ?toks)))
    (rule tok-word
      (head (TokClass Alpha ?c ?r ?toks))
      (body (Letters ?r ?run ?after) (Kind (Cell ?c ?run) ?kind) (WordTok ?kind (Cell ?c ?run) ?after ?toks)))
    (rule tok-digit
      (head (TokClass Digit ?c ?r (Read (Num ?n) ?after)))
      (body (Digits ?r ?n ?after)))
    (rule tok-minus
      (head (TokClass Minus ?c ?r ?toks))
      (body (Peek ?r ?p) (MinusPeek ?p ?r ?toks)))
    (rule tok-other
      (head (TokClass Other ?c ?r (Fail ?r)))
      (body))
    (rule minus-arrow
      (head (MinusPeek (Next (cp 62) ?s) ?r (Read Arrow ?s)))
      (body))
    (rule minus-end
      (head (MinusPeek AtEnd ?r (Read Minus ?r)))
      (body))
    (rule minus-other
      (head (MinusPeek (Next ?d ?s) ?r (Read Minus ?r)))
      (body (different ?d (cp 62))))
    (rule kind-let
      (head (Kind (Cell (cp 108) (Cell (cp 101) (Cell (cp 116) End))) KwLet))
      (body))
    (rule kind-in
      (head (Kind (Cell (cp 105) (Cell (cp 110) End)) KwIn))
      (body))
    (rule kind-plain
      (head (Kind ?w Plain))
      (body (different ?w (Cell (cp 108) (Cell (cp 101) (Cell (cp 116) End)))) (different ?w (Cell (cp 105) (Cell (cp 110) End)))))
    (rule word-let
      (head (WordTok KwLet ?w ?after (Read Let ?after)))
      (body))
    (rule word-in
      (head (WordTok KwIn ?w ?after (Read In ?after)))
      (body))
    (rule word-plain
      (head (WordTok Plain ?w ?after (Read (Name ?w) ?after)))
      (body))
    (rule digits
      (head (Digits ?in ?n ?after))
      (body (Peek ?in ?p) (DigitsPeek ?p ?in ?n ?after)))
    (rule digits-end
      (head (DigitsPeek AtEnd ?in Z ?in))
      (body))
    (rule digits-next
      (head (DigitsPeek (Next ?c ?r) ?in ?n ?after))
      (body (Class ?c ?k) (DigitsClass ?k ?r ?in ?n ?after)))
    (rule digits-more
      (head (DigitsClass Digit ?r ?in (S ?n) ?after))
      (body (Digits ?r ?n ?after)))
    (rule digits-stop-space
      (head (DigitsClass Space ?r ?in Z ?in))
      (body))
    (rule digits-stop-newline
      (head (DigitsClass Newline ?r ?in Z ?in))
      (body))
    (rule digits-stop-letter
      (head (DigitsClass Alpha ?r ?in Z ?in))
      (body))
    (rule digits-stop-minus
      (head (DigitsClass Minus ?r ?in Z ?in))
      (body))
    (rule digits-stop-other
      (head (DigitsClass Other ?r ?in Z ?in))
      (body))
    (rule letter-97
      (head (Letter (cp 97) Yes))
      (body))
    (rule letter-98
      (head (Letter (cp 98) Yes))
      (body))
    (rule letter-99
      (head (Letter (cp 99) Yes))
      (body))
    (rule letter-100
      (head (Letter (cp 100) Yes))
      (body))
    (rule letter-101
      (head (Letter (cp 101) Yes))
      (body))
    (rule letter-102
      (head (Letter (cp 102) Yes))
      (body))
    (rule letter-103
      (head (Letter (cp 103) Yes))
      (body))
    (rule letter-104
      (head (Letter (cp 104) Yes))
      (body))
    (rule letter-105
      (head (Letter (cp 105) Yes))
      (body))
    (rule letter-106
      (head (Letter (cp 106) Yes))
      (body))
    (rule letter-107
      (head (Letter (cp 107) Yes))
      (body))
    (rule letter-108
      (head (Letter (cp 108) Yes))
      (body))
    (rule letter-109
      (head (Letter (cp 109) Yes))
      (body))
    (rule letter-110
      (head (Letter (cp 110) Yes))
      (body))
    (rule letter-111
      (head (Letter (cp 111) Yes))
      (body))
    (rule letter-112
      (head (Letter (cp 112) Yes))
      (body))
    (rule letter-113
      (head (Letter (cp 113) Yes))
      (body))
    (rule letter-114
      (head (Letter (cp 114) Yes))
      (body))
    (rule letter-115
      (head (Letter (cp 115) Yes))
      (body))
    (rule letter-116
      (head (Letter (cp 116) Yes))
      (body))
    (rule letter-117
      (head (Letter (cp 117) Yes))
      (body))
    (rule letter-118
      (head (Letter (cp 118) Yes))
      (body))
    (rule letter-119
      (head (Letter (cp 119) Yes))
      (body))
    (rule letter-120
      (head (Letter (cp 120) Yes))
      (body))
    (rule letter-121
      (head (Letter (cp 121) Yes))
      (body))
    (rule letter-122
      (head (Letter (cp 122) Yes))
      (body))
    (rule letter-no
      (head (Letter ?c No))
      (body (different ?c (cp 97)) (different ?c (cp 98)) (different ?c (cp 99)) (different ?c (cp 100)) (different ?c (cp 101)) (different ?c (cp 102)) (different ?c (cp 103)) (different ?c (cp 104)) (different ?c (cp 105)) (different ?c (cp 106)) (different ?c (cp 107)) (different ?c (cp 108)) (different ?c (cp 109)) (different ?c (cp 110)) (different ?c (cp 111)) (different ?c (cp 112)) (different ?c (cp 113)) (different ?c (cp 114)) (different ?c (cp 115)) (different ?c (cp 116)) (different ?c (cp 117)) (different ?c (cp 118)) (different ?c (cp 119)) (different ?c (cp 120)) (different ?c (cp 121)) (different ?c (cp 122))))
    (rule letters
      (head (Letters ?in ?run ?after))
      (body (Peek ?in ?p) (LettersPeek ?p ?in ?run ?after)))
    (rule letters-end
      (head (LettersPeek AtEnd ?in End ?in))
      (body))
    (rule letters-next
      (head (LettersPeek (Next ?c ?r) ?in ?run ?after))
      (body (Letter ?c ?k) (LettersClass ?k ?c ?r ?in ?run ?after)))
    (rule letters-yes
      (head (LettersClass Yes ?c ?r ?in (Cell ?c ?run) ?after))
      (body (Letters ?r ?run ?after)))
    (rule letters-no
      (head (LettersClass No ?c ?r ?in End ?in))
      (body))))
