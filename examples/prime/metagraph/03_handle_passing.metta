; Handle passing: a request carries the name of a private reply space.
;
; Agents are functions of the spaces they hold.  P and Q share a board.  P makes
; a fresh reply space, posts a request that carries it, and waits there.  Q is a
; service rule on the board: in one firing it takes the request, reads its own
; knowledge space, logs the work in its own space and answers into the reply
; space the request names.  C is a third agent that was never handed the reply
; space.
;
; No rights primitive is used.  A program reaches a space only through a name it
; holds: an argument, a name read out of a space it holds, or a global token.
; The reply space has no global token.  The draft prints a space as its address,
; and parsing that text gives (Error (parse "<space 0x...>") ParseFailed), not a
; space; this is not run here because the address changes from run to run.
;
; Variables are shown through get-metatype, and spaces never printed, because
; their printed forms change from run to run.
!(import! &self interaction)

; ---- the agents
(= (agent-q $board $q-own)
   (whenever (transform $board
      (, (take (request $question $reply)) (read $q-own (knows $question $value)))
      (, (add-atom $q-own (answered $question))
         (add-atom $reply (answer $question $value))))))

(= (p-post $board $reply $question)
   (transform $board () (add-atom $board (request $question $reply))))
(= (p-collect $reply $question)
   (collapse (await (transform $reply (take (answer $question $v)) (p-received $question $v)){$question})))

(= (q-log $q-own) (sort-atom (collapse (transform $q-own (every (read (answered $x))) (q-answered $x)))))
(= (board-left $board) (collapse (transform $board (read (request $x $r)) (still-posted $x))))

(= (fresh-q $board)
   (let $q-own (new-space)
     (let* (($_1 (add-atom $q-own (knows (capital france) paris)))
            ($_2 (add-atom $q-own (knows (capital peru) lima)))
            ($_3 (agent-q $board $q-own)))
       $q-own)))

; ---- 1. The intended exchange, and C's failure.
;      C holds the board and its own space.  It comes after Q has taken the
;      request: the board shows nothing, and a program that would read an
;      answer in a space C has no name for is not formed.
!(let* (($board (new-space))
        ($c-own (new-space))
        ($q-own (fresh-q $board))
        ($reply (new-space))
        ($_ (p-post $board $reply (capital france)))
        ($p (p-collect $reply (capital france)))
        ($c-board (board-left $board))
        ($c-any (collapse (transform $c-own (read $r (answer $x $v)) (c-saw $x $v))))
        ($c-own-space (collapse (transform $c-own (read (answer $x $v)) (c-saw $x $v))))
        ($c-board-space (collapse (transform $board (read (answer $x $v)) (c-saw $x $v)))))
   (exchange (p $p) (q (q-log $q-own))
             (c (board $c-board) (open-location $c-any) (own $c-own-space) (on-board $c-board-space))))

; ---- 2. A shared logic variable does not carry the answer.  P posts a request
;      with an unbound variable for the answer; Q's rule binds the variable of
;      the row it read.  Stored atoms are schematic, so each reader matches a
;      fresh copy, and P's variable stays unbound.
(= (capital-of france) paris)
!(let* (($board (new-space))
        ($q (whenever (transform $board (, (take (ask $question $answer)) (let $answer (capital-of $question)))
                         (add-atom $board (q-bound-its-copy $question $answer)))))
        ($a (new ($slot) $slot))
        ($_ (transform $board () (add-atom $board (ask france $a))))
        ($q-side (collapse (transform $board (read (q-bound-its-copy $x $y)) ($x $y)))))
   (shared-variable (p-variable-is (get-metatype $a)) (q-row $q-side)))

; C's eavesdropper: a rule that copies every request it reads on the board into
; C's own space.  It reads and does not take, so the row it fired on stays
; enabled; without the absence guard the rule fires on the same row again and
; again (the draft's `whenever` keeps no record of the rows it has fired on).
(= (eavesdrop-rule $board $c-own)
   (whenever (transform $board
      (, (read (request $x $r)) (absent $c-own (overheard $x $r)))
      (add-atom $c-own (overheard $x $r)))))

; ---- 3. The board is shared, so a name posted on it is handed to every holder
;      of the board.  C watches the board with a rule installed before Q's: it
;      copies the reply space's name out of the request (reading, not taking),
;      then takes P's answer first.  P finds nothing and its wait parks.  Which
;      of the two rules the agenda wakes first is the scheduler's choice; with
;      Q's first, the request is gone before C reads it.
!(let* (($board (new-space))
        ($c-own (new-space))
        ($c (eavesdrop-rule $board $c-own))
        ($q-own (fresh-q $board))
        ($reply (new-space))
        ($_ (p-post $board $reply (capital peru)))
        ($c-steal (collapse (transform $c-own (, (read (overheard $x $r)) (take $r (answer $x $v))) (c-took $x $v))))
        ($p (p-collect $reply (capital peru))))
   (eavesdrop (c $c-steal) (p $p) (q (q-log $q-own))))

; 3b. The same three agents with Q's rule installed before C's: Q takes the
;     request first, C overhears nothing, and P receives its answer.
!(let* (($board (new-space))
        ($c-own (new-space))
        ($q-own (fresh-q $board))
        ($c (eavesdrop-rule $board $c-own))
        ($reply (new-space))
        ($_ (p-post $board $reply (capital peru)))
        ($c-steal (collapse (transform $c-own (, (read (overheard $x $r)) (take $r (answer $x $v))) (c-took $x $v))))
        ($p (p-collect $reply (capital peru))))
   (q-installed-first (c $c-steal) (p $p) (q (q-log $q-own))))

; ---- 4. The boundary chosen on purpose: Q hands P the name of an inbox that
;      only P and Q hold.  P posts its request there; C's rule on the shared
;      board sees nothing, and P receives its answer.
(= (fresh-q-with-inbox)
   (let* (($inbox (new-space)) ($q-own (fresh-q $inbox))) $inbox))
!(let* (($board (new-space))
        ($c-own (new-space))
        ($c (eavesdrop-rule $board $c-own))
        ($q-inbox (fresh-q-with-inbox))
        ($reply (new-space))
        ($_ (p-post $q-inbox $reply (capital peru)))
        ($p (p-collect $reply (capital peru)))
        ($c-heard (collapse (match $c-own (overheard $x $r) (c-heard $x)))))
   (private-inbox (p $p) (c $c-heard)))

; ---- 5. Many private inboxes, one rule.  Q keeps the names of its clients'
;      inboxes in its own space.  One standing rule reads that list and takes
;      a request from whichever inbox holds one: the inbox is placed by an
;      earlier source of the same firing, so no forwarder moves requests into
;      a common space.  The rule wakes on a post into any inbox it read.
(= (multi-client-service $q-own)
   (whenever (transform $q-own
      (, (read (inbox $client $in)) (take $in (request $question $reply)) (read (knows $question $value)))
      (, (add-atom $q-own (answered $client $question)) (add-atom $reply (answer $question $value))))))
!(let* (($q-own (new-space))
        ($in1 (new-space))
        ($in2 (new-space))
        ($_k1 (add-atom $q-own (knows (capital france) paris)))
        ($_k2 (add-atom $q-own (knows (capital peru) lima)))
        ($_i1 (add-atom $q-own (inbox p1 $in1)))
        ($_i2 (add-atom $q-own (inbox p2 $in2)))
        ($_s (multi-client-service $q-own))
        ($r1 (new-space))
        ($r2 (new-space))
        ($_p2 (p-post $in2 $r2 (capital peru)))
        ($_p1 (p-post $in1 $r1 (capital france)))
        ($a1 (p-collect $r1 (capital france)))
        ($a2 (p-collect $r2 (capital peru))))
   (many-inboxes (p1 $a1) (p2 $a2) (q (sort-atom (collapse (match $q-own (answered $c $q) ($c $q)))))))
