; Class runs: a mode taking the longest stretch of a class of scalars from
; the source list, as its text and the list after it, compiles to the
; runtime's scan, its text a span (Word, Greek: non-ASCII scalars by range).
; Not class runs, compiled as written: a run building something else of each
; scalar (Boxed), one taking the scalar it stops at (Eats), and one looking
; past its first scalar (Pair).  A span then meets every way a list is read:
; literal texts compared by bytes, but one whose bytes would read back
; otherwise scalar by scalar (Kw, Lex), a cell built in front of a span
; (Lex2), and a span taken apart (Second).  A table of literal texts
; dispatches on a text's key, and its default's guards are one keyed test
; (Key); a rule taking any other value, placed before the literals, is
; still tried first (Early).
(gslt-presentation-v1 NativeDirectClassRunsV1
  (signature
    (operator Peek 2)
    (operator AtEnd 0)
    (operator Next 2)
    (operator WordChar 2)
    (operator GreekChar 2)
    (operator Kw 2)
    (operator Lex 2)
    (operator Lex2 2)
    (operator Lex2Peek 2)
    (operator Second 2)
    (operator SecondOf 2)
    (operator Pair 3)
    (operator PairPeek 4)
    (operator PairAfter 5)
    (operator Word 3)
    (operator WordPeek 4)
    (operator WordClass 6)
    (operator Greek 3)
    (operator GreekPeek 4)
    (operator GreekClass 6)
    (operator Boxed 3)
    (operator BoxedPeek 4)
    (operator BoxedClass 6)
    (operator Eats 3)
    (operator EatsPeek 4)
    (operator EatsClass 6)
    (operator Cell 2)
    (operator End 0)
    (operator Box 1)
    (operator Yes 0)
    (operator No 0)
    (operator Let 0)
    (operator In 0)
    (operator Odd 0)
    (operator Name 1)
    (operator cons 2)
    (operator nil 0)
    (operator cp 1)
    (operator Key 2)
    (operator Early 2)
    (operator Keyed 2)
    (operator EarlyKeyed 2)
    (operator KwTok 1)
    (operator Letters 3)
    (operator LettersPeek 4)
    (operator LettersClass 6)
    (operator Letter 2)
    (operator First 0))
  (equations)
  (rewrites
    (rule peek-end
      (head (Peek nil AtEnd))
      (body))
    (rule peek-next
      (head (Peek (cons ?c ?r) (Next ?c ?r)))
      (body))
    (rule word-char-97
      (head (WordChar (cp 97) Yes))
      (body))
    (rule word-char-98
      (head (WordChar (cp 98) Yes))
      (body))
    (rule word-char-99
      (head (WordChar (cp 99) Yes))
      (body))
    (rule word-char-101
      (head (WordChar (cp 101) Yes))
      (body))
    (rule word-char-108
      (head (WordChar (cp 108) Yes))
      (body))
    (rule word-char-116
      (head (WordChar (cp 116) Yes))
      (body))
    (rule word-char-48
      (head (WordChar (cp 48) Yes))
      (body))
    (rule word-char-233
      (head (WordChar (cp 233) Yes))
      (body))
    (rule word-char-56515
      (head (WordChar (cp 56515) Yes))
      (body))
    (rule word-char-no
      (head (WordChar ?c No))
      (body (different ?c (cp 97)) (different ?c (cp 98)) (different ?c (cp 99)) (different ?c (cp 101)) (different ?c (cp 108)) (different ?c (cp 116)) (different ?c (cp 48)) (different ?c (cp 233)) (different ?c (cp 56515))))
    (rule greek-char-945
      (head (GreekChar (cp 945) Yes))
      (body))
    (rule greek-char-946
      (head (GreekChar (cp 946) Yes))
      (body))
    (rule greek-char-947
      (head (GreekChar (cp 947) Yes))
      (body))
    (rule greek-char-no
      (head (GreekChar ?c No))
      (body (different ?c (cp 945)) (different ?c (cp 946)) (different ?c (cp 947))))
    (rule word
      (head (Word ?in ?run ?after))
      (body (Peek ?in ?p) (WordPeek ?p ?in ?run ?after)))
    (rule word-end
      (head (WordPeek AtEnd ?in End ?in))
      (body))
    (rule word-next
      (head (WordPeek (Next ?c ?r) ?in ?run ?after))
      (body (WordChar ?c ?k) (WordClass ?k ?c ?r ?in ?run ?after)))
    (rule word-yes
      (head (WordClass Yes ?c ?r ?in (Cell ?c ?run) ?after))
      (body (Word ?r ?run ?after)))
    (rule word-no
      (head (WordClass No ?c ?r ?in End ?in))
      (body))
    (rule greek
      (head (Greek ?in ?run ?after))
      (body (Peek ?in ?p) (GreekPeek ?p ?in ?run ?after)))
    (rule greek-end
      (head (GreekPeek AtEnd ?in End ?in))
      (body))
    (rule greek-next
      (head (GreekPeek (Next ?c ?r) ?in ?run ?after))
      (body (GreekChar ?c ?k) (GreekClass ?k ?c ?r ?in ?run ?after)))
    (rule greek-yes
      (head (GreekClass Yes ?c ?r ?in (Cell ?c ?run) ?after))
      (body (Greek ?r ?run ?after)))
    (rule greek-no
      (head (GreekClass No ?c ?r ?in End ?in))
      (body))
    (rule boxed
      (head (Boxed ?in ?run ?after))
      (body (Peek ?in ?p) (BoxedPeek ?p ?in ?run ?after)))
    (rule boxed-end
      (head (BoxedPeek AtEnd ?in End ?in))
      (body))
    (rule boxed-next
      (head (BoxedPeek (Next ?c ?r) ?in ?run ?after))
      (body (WordChar ?c ?k) (BoxedClass ?k ?c ?r ?in ?run ?after)))
    (rule boxed-yes
      (head (BoxedClass Yes ?c ?r ?in (Cell (Box ?c) ?run) ?after))
      (body (Boxed ?r ?run ?after)))
    (rule boxed-no
      (head (BoxedClass No ?c ?r ?in End ?in))
      (body))
    (rule eats
      (head (Eats ?in ?run ?after))
      (body (Peek ?in ?p) (EatsPeek ?p ?in ?run ?after)))
    (rule eats-end
      (head (EatsPeek AtEnd ?in End ?in))
      (body))
    (rule eats-next
      (head (EatsPeek (Next ?c ?r) ?in ?run ?after))
      (body (WordChar ?c ?k) (EatsClass ?k ?c ?r ?in ?run ?after)))
    (rule eats-yes
      (head (EatsClass Yes ?c ?r ?in (Cell ?c ?run) ?after))
      (body (Eats ?r ?run ?after)))
    (rule eats-no
      (head (EatsClass No ?c ?r ?in End ?r))
      (body))
    (rule pair
      (head (Pair ?in ?run ?after))
      (body (Peek ?in ?p) (PairPeek ?p ?in ?run ?after)))
    (rule pair-end
      (head (PairPeek AtEnd ?in End ?in))
      (body))
    (rule pair-next
      (head (PairPeek (Next ?c ?r) ?in ?run ?after))
      (body (Peek ?r ?q) (PairAfter ?q ?c ?in ?run ?after)))
    (rule pair-after-end
      (head (PairAfter AtEnd ?c ?in End ?in))
      (body))
    (rule pair-after-next
      (head (PairAfter (Next ?d ?s) ?c ?in (Cell ?c ?run) ?after))
      (body (Pair ?s ?run ?after)))
    (rule kw-let
      (head (Kw (Cell (cp 108) (Cell (cp 101) (Cell (cp 116) End))) Let))
      (body))
    (rule kw-in
      (head (Kw (Cell (cp 105) (Cell (cp 110) End)) In))
      (body))
    (rule kw-odd
      (head (Kw (Cell (cp 56515) (Cell (cp 56489) End)) Odd))
      (body))
    (rule kw-name
      (head (Kw ?w (Name ?w)))
      (body (different ?w (Cell (cp 108) (Cell (cp 101) (Cell (cp 116) End)))) (different ?w (Cell (cp 105) (Cell (cp 110) End))) (different ?w (Cell (cp 56515) (Cell (cp 56489) End)))))
    (rule lex
      (head (Lex ?in ?tok))
      (body (Word ?in ?w ?after) (Kw ?w ?tok)))
    (rule lex2
      (head (Lex2 ?in ?tok))
      (body (Peek ?in ?p) (Lex2Peek ?p ?tok)))
    (rule lex2-next
      (head (Lex2Peek (Next ?c ?r) ?tok))
      (body (Word ?r ?run ?after) (Kw (Cell ?c ?run) ?tok)))
    (rule second
      (head (Second ?in ?c))
      (body (Word ?in ?w ?after) (SecondOf ?w ?c)))
    (rule second-of
      (head (SecondOf (Cell ?a (Cell ?b ?rest)) ?b))
      (body))
    (rule key-let
      (head (Key (Cell (cp 108) (Cell (cp 101) (Cell (cp 116) End))) (KwTok (Cell (cp 108) (Cell (cp 101) (Cell (cp 116) End))))))
      (body))
    (rule key-in
      (head (Key (Cell (cp 105) (Cell (cp 110) End)) (KwTok (Cell (cp 105) (Cell (cp 110) End)))))
      (body))
    (rule key-at
      (head (Key (Cell (cp 97) (Cell (cp 116) End)) (KwTok (Cell (cp 97) (Cell (cp 116) End)))))
      (body))
    (rule key-if
      (head (Key (Cell (cp 105) (Cell (cp 102) End)) (KwTok (Cell (cp 105) (Cell (cp 102) End)))))
      (body))
    (rule key-then
      (head (Key (Cell (cp 116) (Cell (cp 104) (Cell (cp 101) (Cell (cp 110) End)))) (KwTok (Cell (cp 116) (Cell (cp 104) (Cell (cp 101) (Cell (cp 110) End)))))))
      (body))
    (rule key-else
      (head (Key (Cell (cp 101) (Cell (cp 108) (Cell (cp 115) (Cell (cp 101) End)))) (KwTok (Cell (cp 101) (Cell (cp 108) (Cell (cp 115) (Cell (cp 101) End)))))))
      (body))
    (rule key-name
      (head (Key ?w (Name ?w)))
      (body (different ?w (Cell (cp 108) (Cell (cp 101) (Cell (cp 116) End)))) (different ?w (Cell (cp 105) (Cell (cp 110) End))) (different ?w (Cell (cp 97) (Cell (cp 116) End))) (different ?w (Cell (cp 105) (Cell (cp 102) End))) (different ?w (Cell (cp 116) (Cell (cp 104) (Cell (cp 101) (Cell (cp 110) End))))) (different ?w (Cell (cp 101) (Cell (cp 108) (Cell (cp 115) (Cell (cp 101) End)))))))
    (rule early-any
      (head (Early ?w First))
      (body (different ?w (Cell (cp 108) (Cell (cp 101) (Cell (cp 116) End)))) (different ?w (Cell (cp 105) (Cell (cp 110) End))) (different ?w (Cell (cp 97) (Cell (cp 116) End))) (different ?w (Cell (cp 105) (Cell (cp 102) End)))))
    (rule early-let
      (head (Early (Cell (cp 108) (Cell (cp 101) (Cell (cp 116) End))) (KwTok (Cell (cp 108) (Cell (cp 101) (Cell (cp 116) End))))))
      (body))
    (rule early-in
      (head (Early (Cell (cp 105) (Cell (cp 110) End)) (KwTok (Cell (cp 105) (Cell (cp 110) End)))))
      (body))
    (rule early-at
      (head (Early (Cell (cp 97) (Cell (cp 116) End)) (KwTok (Cell (cp 97) (Cell (cp 116) End)))))
      (body))
    (rule early-if
      (head (Early (Cell (cp 105) (Cell (cp 102) End)) (KwTok (Cell (cp 105) (Cell (cp 102) End)))))
      (body))
    (rule keyed
      (head (Keyed ?in ?tok))
      (body (Letters ?in ?w ?after) (Key ?w ?tok)))
    (rule early-keyed
      (head (EarlyKeyed ?in ?tok))
      (body (Letters ?in ?w ?after) (Early ?w ?tok)))
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
