; A region's identity is seen only by comparison: same-region says whether
; two handles name one region, whatever their rights.  A handle with no
; rights names its region and grants nothing, so a reader can pin a region
; and refuse a substitute.
!(import! &self interaction)

; comparisons
!(let $b (new-region board) (same-region $b $b))
!(let $b (new-region board) (same-region $b (facet $b (peek))))
!(let $b (new-region board) (same-region (facet $b (peek)) (facet $b ())))
!(let* (($b (new-region board)) ($c (new-region board))) (same-region $b $c))
!(let* (($b (new-region board)) ($c (new-region board))) (same-region (facet $b ()) (facet $c ())))
!(let $b (new-region board) (same-region $b (parse (repr $b))))
!(let $b (new-region board) (same-region $b 7))
!(bind! &p (new-space))
!(bind! &q (new-space))
!(same-region &p &p)
!(same-region &p &q)
!(let $b (new-region space) (same-region $b &p))

; a handle with no rights is a bare name: it reads nothing and makes nothing
!(let $b (new-region board) (rights-of (facet $b ())))
!(let* (($b (new-region board)) ($_ (transform $b () (add-atom $b (verdict even 4)))))
   (transform (facet $b ()) (read (verdict even $n)) (saw $n)))
!(let* (($b (new-region board)) ($n (facet $b ())))
   (transform $n () (add-atom $n (verdict even 7))))

; An LCF board behind a public directory.  Only the checker makes verdicts,
; and only for claims it has checked; readers find the board through a
; directory anyone may write.
(= (even? $n) (== (% $n 2) 0))
(= (checker $claims $board)
   (whenever (transform $claims (, (take (claim $n)) (let True (even? $n)))
               (add-atom $board (verdict even $n)))))
; A reader that trusts the directory.
(= (naive-read)
   (let $r (transform &pub (read (readers $h)) $h)
     (transform $r (every (read (verdict even $n))) (accepted $n))))
; A reader that pins the board it was given at setup and takes verdicts only
; from that region.
(= (careful-read $pin)
   (let $r (transform &pub (read (readers $h)) $h)
     (if (same-region $r $pin)
         (transform $r (every (read (verdict even $n))) (accepted $n))
         (refused untrusted-board))))
; A forger makes its own board, mints a verdict there, and puts the board's
; reader handle in the directory in place of the checker's.
(= (forge)
   (let* (($fake (new-region board)) ($fake-readers (facet $fake (peek)))
          ($_ (transform $fake () (add-atom $fake (verdict even 7)))))
     (transform &pub (take (readers $old)) (add-atom &pub (readers $fake-readers)))))
!(bind! &pub (new-space))
!(let* (($board (new-region board)) ($readers (facet $board (peek)))
        ($pin (facet $board ()))
        ($claims (new-region mailbox)) ($submit (facet $claims (send)))
        ($_ (checker $claims $board))
        ($_2 (transform &pub () (add-atom &pub (readers $readers))))
        ($_3 (transform $submit () (, (add-atom $submit (claim 4)) (add-atom $submit (claim 7)))))
        ($before (collapse (careful-read $pin)))
        ($_4 (forge))
        ($naive (collapse (naive-read)))
        ($careful (collapse (careful-read $pin))))
   (before $before naive-after $naive careful-after $careful))
; the forger still cannot mint on the checker's board
!(let $r (transform &pub (read (readers $h)) $h)
   (transform $r () (add-atom $r (verdict even 9))))

; A directory that is itself a board: only its holder lists boards, so a
; forger with the readers' handle cannot replace an entry.
!(let* (($board (new-region board)) ($dir (new-region board))
        ($dir-readers (facet $dir (peek)))
        ($_ (transform $dir () (add-atom $dir (readers (facet $board (peek))))))
        ($fake (new-region board)))
   (transform $dir-readers (take (readers $old)) (add-atom $dir-readers (readers $fake))))
