; Shared-ground reflexivity semantic adversary.
; A deep ground DAG passes through a space and ordinary MeTTa substitution,
; which preserves its value but does not itself grant immutable publication
; identity.  This is the negative admission boundary.  The equal-but-distinct
; and unequal controls keep occurrence identity from becoming structural
; authority.
(= (payload) (box 71 (box 70 (box 69 (box 68 (box 67 (box 66 (box 65 (box 64 (box 63 (box 62 (box 61 (box 60 (box 59 (box 58 (box 57 (box 56 (box 55 (box 54 (box 53 (box 52 (box 51 (box 50 (box 49 (box 48 (box 47 (box 46 (box 45 (box 44 (box 43 (box 42 (box 41 (box 40 (box 39 (box 38 (box 37 (box 36 (box 35 (box 34 (box 33 (box 32 (box 31 (box 30 (box 29 (box 28 (box 27 (box 26 (box 25 (box 24 (box 23 (box 22 (box 21 (box 20 (box 19 (box 18 (box 17 (box 16 (box 15 (box 14 (box 13 (box 12 (box 11 (box 10 (box 9 (box 8 (box 7 (box 6 (box 5 (box 4 (box 3 (box 2 (box 1 (box 0 leaf)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))
(= (same $x $x) yes)
(= (shared-loop $n $space $value)
   (if (== $n 0)
       done
       (let $_ (once (match $space $value $value))
         (shared-loop (- $n 1) $space $value))))

!(let* (($built (payload))
        ($wrapped (add-atom &self (shared-ground-probe $built)))
        ($published (once (match &self (shared-ground-probe $x) $x)))
        ($added (add-atom &self $published)))
   (shared-loop 20000 &self $published))
!(same (box 71 (box 70 (box 69 (box 68 (box 67 (box 66 (box 65 (box 64 (box 63 (box 62 (box 61 (box 60 (box 59 (box 58 (box 57 (box 56 (box 55 (box 54 (box 53 (box 52 (box 51 (box 50 (box 49 (box 48 (box 47 (box 46 (box 45 (box 44 (box 43 (box 42 (box 41 (box 40 (box 39 (box 38 (box 37 (box 36 (box 35 (box 34 (box 33 (box 32 (box 31 (box 30 (box 29 (box 28 (box 27 (box 26 (box 25 (box 24 (box 23 (box 22 (box 21 (box 20 (box 19 (box 18 (box 17 (box 16 (box 15 (box 14 (box 13 (box 12 (box 11 (box 10 (box 9 (box 8 (box 7 (box 6 (box 5 (box 4 (box 3 (box 2 (box 1 (box 0 leaf)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))) (box 71 (box 70 (box 69 (box 68 (box 67 (box 66 (box 65 (box 64 (box 63 (box 62 (box 61 (box 60 (box 59 (box 58 (box 57 (box 56 (box 55 (box 54 (box 53 (box 52 (box 51 (box 50 (box 49 (box 48 (box 47 (box 46 (box 45 (box 44 (box 43 (box 42 (box 41 (box 40 (box 39 (box 38 (box 37 (box 36 (box 35 (box 34 (box 33 (box 32 (box 31 (box 30 (box 29 (box 28 (box 27 (box 26 (box 25 (box 24 (box 23 (box 22 (box 21 (box 20 (box 19 (box 18 (box 17 (box 16 (box 15 (box 14 (box 13 (box 12 (box 11 (box 10 (box 9 (box 8 (box 7 (box 6 (box 5 (box 4 (box 3 (box 2 (box 1 (box 0 leaf)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))
!(collapse (same (box 71 (box 70 (box 69 (box 68 (box 67 (box 66 (box 65 (box 64 (box 63 (box 62 (box 61 (box 60 (box 59 (box 58 (box 57 (box 56 (box 55 (box 54 (box 53 (box 52 (box 51 (box 50 (box 49 (box 48 (box 47 (box 46 (box 45 (box 44 (box 43 (box 42 (box 41 (box 40 (box 39 (box 38 (box 37 (box 36 (box 35 (box 34 (box 33 (box 32 (box 31 (box 30 (box 29 (box 28 (box 27 (box 26 (box 25 (box 24 (box 23 (box 22 (box 21 (box 20 (box 19 (box 18 (box 17 (box 16 (box 15 (box 14 (box 13 (box 12 (box 11 (box 10 (box 9 (box 8 (box 7 (box 6 (box 5 (box 4 (box 3 (box 2 (box 1 (box 0 leaf)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))) (box 71 (box 70 (box 69 (box 68 (box 67 (box 66 (box 65 (box 64 (box 63 (box 62 (box 61 (box 60 (box 59 (box 58 (box 57 (box 56 (box 55 (box 54 (box 53 (box 52 (box 51 (box 50 (box 49 (box 48 (box 47 (box 46 (box 45 (box 44 (box 43 (box 42 (box 41 (box 40 (box 39 (box 38 (box 37 (box 36 (box 35 (box 34 (box 33 (box 32 (box 31 (box 30 (box 29 (box 28 (box 27 (box 26 (box 25 (box 24 (box 23 (box 22 (box 21 (box 20 (box 19 (box 18 (box 17 (box 16 (box 15 (box 14 (box 13 (box 12 (box 11 (box 10 (box 9 (box 8 (box 7 (box 6 (box 5 (box 4 (box 3 (box 2 (box 1 (box 0 other-leaf))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))
