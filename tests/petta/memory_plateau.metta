;; A long-running agent's turn, repeated: text that is new every turn (a stamp
;; and a prompt built from it), a reply and a fresh word read from text, calls
;; that select among more than four clauses by a string key, named state kept
;; current under a bounded set of computed names, a bounded memory space
;; written, read and rotated, a captured closure, and walks over paths and
;; lists through the libraries.
;;
;; Usage: cetta --profile extended --lang petta memory_plateau.metta N
;; Four windows of N turns run as top-level forms. After the first (warm-up)
;; window, the live storage gauges must stay where they are: the symbol table,
;; managed spellings and counted names exactly, and the bytes the allocator
;; holds within 512 KiB over the remaining three windows (each reading of the
;; gauges itself adds a few KiB of terms).
;;
;; Shapes that do not plateau yet are left out: facts that differ in every
;; turn and are removed again (the term universe keeps removed terms), a new
;; space per turn (spaces live until teardown), and a function added at run
;; time whose fresh closures cross into Prolog (their code stays registered).
!(import! &self (library path))
!(import! &self (library list))
!(bind! &memory (new-space))
!(change-state! &history "")
(= (route "remember") remember)
(= (route "query") query)
(= (route "send") send)
(= (route "pin") pin)
(= (route "search") search)
(= (route "shell") shell)
(= (slot $k) (atom_concat memory_slot_ (% $k 8)))
(= (recent) (collapse (match &memory (note $i $v) $v)))
(= (turn $k)
   (let* (($stamp (format-args "turn {} at {}" ($k (* $k 7919))))
          ($prompt (format-args "{} | {} | {}" ($stamp (get-state &history) (repr (recent)))))
          ($reply (sread (format-args "(remember \"reply {} {}\")" ($k $stamp))))
          ($word (sread (format-args "fresh_word_{}" ($k))))
          ($tool (collapse (route (repr (car-atom $reply)))))
          ($fresh (collapse (route $stamp)))
          ($h (change-state! &history $stamp))
          ($s (change-state! (slot $k) $prompt))
          ($i (% $k 16))
          ($v (% $k 5))
          ($a (add-atom &memory (note $i $v)))
          ($seen (collapse (match &memory (note $i $w) $w)))
          ($r (remove-atom &memory (note $i $v)))
          ($f (|-> ($x) (+ $x $k)))
          ($y ($f 1))
          ($dir (path:dirname (format-args "/agent/{}/notes" ((% $k 4)))))
          ($rev (list:reverse-acc ($k a b) ())))
     done))
(= (turns $k $end) (if (>= $k $end) done (let $t (turn $k) (turns (+ $k 1) $end))))
(= (gauge $name) (match (runtime-stats!) (runtime-counter $name $n) $n))
(= (counts)
   ((gauge symbol-owned-count) (gauge symbol-data-count)
    (gauge managed-symbol-retained-count) (gauge registered-name-count)))
;; The first reading also names every runtime counter; read once to settle.
;; A reading of the heap is the first runtime-stats! of its own form, so the
;; inventory spaces earlier readings made (released when a form ends) are not
;; in it.
!(let $c (counts) settled)
!(turns 0 (argv 1))
!(change-state! &heap-1 (gauge heap-in-use-bytes))
!(change-state! &counts-1 (counts))
!(turns (argv 1) (* 2 (argv 1)))
!(turns (* 2 (argv 1)) (* 3 (argv 1)))
!(turns (* 3 (argv 1)) (* 4 (argv 1)))
!(change-state! &heap-2 (gauge heap-in-use-bytes))
!(change-state! &counts-2 (counts))
!(let* (($growth (- (get-state &heap-2) (get-state &heap-1))))
   ((counts-flat (== (get-state &counts-2) (get-state &counts-1)))
    (heap-within-512-KiB (<= $growth 524288))))
