; MM2's step, written with a set space, a priority space and transform.
;
; An MM2 program is a store that holds each atom once.  Some of its atoms are
; execs, (exec LOCATION (, p1 ... pn) OUTPUTS), and a step runs one of them:
;
;   1. the least exec is chosen;
;   2. its patterns are matched, as one conjunction, against the store as it
;      stands with the exec still in it (an exec can read itself);
;   3. the exec is removed: it runs once;
;   4. for every match its outputs are made.  Under (, t1 ... tm) each
;      instance is added.  Under (O s1 ... sk) a sink (+ t) adds t and a
;      sink (- t) removes it, the removals after every addition.
;
; Nothing is tested for absence: an output the store already holds lands on
; the atom that is there, because the store is a set.  A step's outputs are
; collected before any is written, so every match reads the store the step
; began with.  An exec is one job, consumed by running; a program runs to a
; fixed point only by writing execs.
;
; The execs wait in a priority space, so the least is at hand and none is
; searched for.  Its order is the standard order of terms.  MORK's order is
; that of its byte encoding, which differs where a location is a number
; (MORK reads 10 before 9, by spelling), where symbols differ in length (the
; shorter first), and where an expression meets a symbol (the expression
; first); a program meant to run in MORK's order names its locations so the
; two agree, or keys its execs by their encoding.

!(bind! &mm2 (new-space set))
!(bind! &todo (new-space priority))
!(bind! &stage (new-space set))

; Programs are data: + and - in them are sinks, not sums.
(: mm2:exec? (-> Atom Bool))
(= (mm2:exec? $a)
   (if (== (get-metatype $a) Expression)
       (if (== (size-atom $a) 4) (== (repr (car-atom $a)) "exec") False)
       False))

; An atom enters the store; an exec the store did not hold joins the queue.
(: mm2:add (-> Atom %Undefined%))
(= (mm2:add $a)
   (if (== (collapse (match &mm2 $a held)) ())
       (let $_ (add-atom &mm2 $a)
          (if (mm2:exec? $a) (add-atom &todo $a) ()))
       ()))
(: mm2:remove (-> Atom %Undefined%))
(= (mm2:remove $a)
   (let $_ (remove-atom &mm2 $a)
      (if (mm2:exec? $a) (remove-atom &todo $a) ())))

; The outputs of one match: adds first, over every staged output; removals
; when all are added.
(: mm2:sign (-> Atom %Undefined%))
(= (mm2:sign $sink) (repr (car-atom $sink)))
(: mm2:write (-> Atom Atom %Undefined%))
(= (mm2:write $pass $out)
   (if (== (repr (car-atom $out)) "O")
       (let $sinks (cdr-atom $out) (mm2:sinks $pass $sinks))
       (if (== $pass add) (mm2:add $out) ())))
(: mm2:sinks (-> Atom Atom %Undefined%))
(= (mm2:sinks $pass $sinks)
   (if (== $sinks ()) ()
       (let* ((($s $rest) (decons-atom $sinks))
              ($t (car-atom (cdr-atom $s)))
              ($_ (if (== $pass add)
                      (if (== (mm2:sign $s) "+") (mm2:add $t) ())
                      (if (== (mm2:sign $s) "-") (mm2:remove $t) ()))))
          (mm2:sinks $pass $rest))))
(: mm2:write-all (-> Atom Atom %Undefined%))
(= (mm2:write-all $pass $outs)
   (if (== $outs ()) ()
       (let* ((($o $rest) (decons-atom $outs))
              ($_ (mm2:write $pass $o)))
          (mm2:write-all $pass $rest))))

; One step.  The transform reads the exec's patterns over the store and puts
; each match's outputs, as data, into the staging set.
(: mm2:fire (-> Atom %Undefined%))
(= (mm2:fire (exec $loc $query $outputs))
   (let* (($_1 (collapse (transform &mm2 $query $outputs &stage)))
          ($staged (collapse (get-atoms &stage)))
          ($_2 (collapse (match &stage $s (remove-atom &stage $s))))
          ($_3 (space:priority:pop &todo))
          ($_4 (remove-atom &mm2 (exec $loc $query $outputs)))
          ($_5 (mm2:write-all add $staged))
          ($_6 (mm2:write-all remove $staged)))
      $loc))
(= (mm2:step)
   (if (== (space-len &todo) 0) none
       (let $e (space:priority:peek &todo) (mm2:fire $e))))
(= (mm2:run $steps)
   (let $ran (mm2:step)
      (if (== $ran none) $steps (mm2:run (+ $steps 1)))))
(: mm2:load (-> Atom %Undefined%))
(= (mm2:load $atoms)
   (if (== $atoms ()) ()
       (let* ((($a $rest) (decons-atom $atoms)) ($_ (mm2:add $a)))
          (mm2:load $rest))))

; Two execs wait at one location and read one trigger.  The lesser runs,
; writes (ran a) and removes the trigger; the other then matches nothing.
!(mm2:load ((trigger)
            (exec same (, (trigger)) (O (+ (ran b)) (- (trigger))))
            (exec same (, (trigger)) (O (+ (ran a)) (- (trigger))))))
!(steps (mm2:run 0))
!(store (collapse (get-atoms &mm2)))

; Execs that write execs: a clock that counts three ticks, each tick one job.
!(bind! &mm2 (new-space set))
!(bind! &todo (new-space priority))
!(mm2:load ((tick z)
            (exec 0 (, (tick $n)) (, (seen $n)))
            (exec 1 (, (tick z)) (, (tick (s z)) (exec 2 (, (tick (s z))) (, (seen (s z)) (done)))))))
!(steps (mm2:run 0))
!(store (collapse (get-atoms &mm2)))
