; Interaction: one commit over spaces and channels.  Import with
; (import! &self interaction).
;
; One interaction is one commit.  Its sources observe the store, its
; template says what the commit makes and what runs after it:
;
;   (transform R SOURCES TEMPLATE)        fire now, or no answer
;   (transform R SOURCES TEMPLATE DEST)   the instances are made in DEST
;
; SOURCES is one source, a conjunction (, c1 ... cn), or () for one empty
; row.  R is the region a source names when it names none.
;
;   p | (read [R'] p)      keep one occurrence matching p (a choice of rows)
;   (take [R'] p)          take one occurrence matching p
;   (absent [R'] p)        a predicate query that saw nothing; also (absent SOURCES)
;   (unique [take] c)      a predicate query that saw exactly one occurrence
;   (all [take] c COLL)    a predicate query: the region's matches, as one
;                          collection, matched against COLL; with take, all
;                          of them are taken
;   (let P E)              E runs once its variables are bound; its values
;                          are matched against P; no value, no row
;   (every c)              from here on every row, all in this one commit
;
; The names a source pattern holds are the transform's own: each is bound
; by the matching, in force in the sources and the template, as a pattern
; of match binds its names.  A name of the scope around reaches a pattern
; only through a crossing set, (transform R SOURCES TEMPLATE){$x ...}.  A
; location and the E of (let P E) are read where they stand: a name there
; is a source's, or the scope around's.
;
; A matched read picks an occurrence and keeps or takes it.  A predicate
; query (an answer from every match of its pattern in the region) disposes
; of nothing; it holds only while its region is unchanged.  A row
; never takes an occurrence twice or both takes and keeps one.  `every`
; refuses rows that take a common occurrence or where one keeps what
; another takes.
;
; TEMPLATE is (, t1 ... tn) or one item.  An item (add-atom R'' a) is a
; make of the commit, into any region; every other item runs after the
; commit, as the continuation.  Values read from the store reach the
; continuation as data: reading or taking stored code never runs it, and
; (eval $c) does.
;
; Firing at once, later or always:
;
;   (await X)          X now if it can fire, else when it can; once
;   (whenever X)       X as a rule: it fires at every enabling, and stays
;                      (both answer () when they park, as add-atom does)
;   (choose X1 ... Xn) one alternative commits; (await (choose ...)) waits
;                      for the first that can
;   (else X K)         X now if it can fire, otherwise K
;
; X is a transform or a choose.  Rholang's receipts are the same firings:
;
;   (for (p <- c) P)    = (await (transform c (take p) P))
;   (for (p <= c) P)    = (whenever (transform c (take p) P))
;   (for (p <<- c) P)   = (await (transform c (read p) P))
;   (for (p <- a & q <- b) P) joins the binds in one commit
;   (contract c p P)    = (for (p <= c) P)
;
; Processes and names:
;
;   (par P1 ... Pn)     run the processes; the agenda interleaves their steps
;   (seq E $v K)        run E as one step, then K with $v bound to each answer
;
; A fresh name is a fresh region: (let $c (new-region channel) P).  Prime's
; own (new ($x ...) P) gives fresh private holes, a different thing.
;
; Regions and rights.  A region is a space with a profile, kept by the
; region authority: which facets its holder has, and which the holder may
; hand to others.  Facets: receive (take), peek (keep), send (make) and
; observe (predicate query).
;
;   (new-region PROFILE)  a fresh region; the answer is its holder's handle
;   (facet H RIGHTS)      a handle with RIGHTS, within H's and the profile's
;   (rights-of H) (profile-of H) (strategy-of H) (is-handle X)
;   (same-region H1 H2)   whether two handles name one region, whatever
;                         their rights
;
;   profile   holder                       handed out
;   space     receive peek send observe    all four
;   channel   receive peek send            receive peek send
;   mailbox   receive peek send observe    send
;   log       peek send observe            peek send
;   board     send peek observe            peek
;
; A region's space is reached only through its handles: no name gives a
; right over it.  Its identity is seen only by comparison: a handle with no
; rights, (facet H ()), names the region and grants nothing, so a reader
; can pin a region and refuse a substitute.  A plain space has every right.
;
; A location written as a variable that no earlier source binds is not
; formed: it would read every region.  Written locations are checked when
; the transform is formed; locations that data supplies, when the walk
; reaches them.
;
; Answers are bags.  Which row commits when several can is a choice of the
; scheduler, as is the order of the agenda (agenda-policy fifo, lifo or
; (seeded N); the CETTA_INTERACTION_SEED environment variable seeds it).

;; ---- regions and rights -------------------------------------------------------

(= (new-region $profile) (__cetta_lib_interaction_new-region $profile))
(= (facet $h $rights) (__cetta_lib_interaction_facet $h $rights))
(= (rights-of $h) (__cetta_lib_interaction_rights $h))
(= (profile-of $h) (__cetta_lib_interaction_profile $h))
(= (strategy-of $h) (__cetta_lib_interaction_strategy $h))
(= (is-handle $x) (__cetta_lib_interaction_is-handle $x))
(= (same-region $x $y) (__cetta_lib_interaction_same-region $x $y))
(= (agenda-policy $p) (__cetta_lib_interaction_agenda-policy $p))
(= (agenda-stats) (__cetta_lib_interaction_agenda-stats ()))

;; ---- running the kernel ---------------------------------------------------------

; The kernel stops only to have code run. (ia:need M E) identifies M's stored
; request; compute evaluates it while retaining its reported Space reads.
; (ia:done OUTCOME): the firing is over.
(= (ia:run $i)
   (case $i
     (((ia:need $m $e)
       (ia:run (__cetta_lib_interaction_compute $m)))
      ($done $done))))

; The answers of a firing: the continuation's, after the agenda has run.
(= (ia:finish $i)
   (case $i
     (((ia:done (ia:fired $x))
       (let $_ (ia:drive) (eval $x)))
      ((ia:done ia:parked) (let $_ (ia:drive) ()))
      ((ia:done ia:none) (empty))
      ((ia:done (ia:stuck $s)) (Error (ia:stuck $s) NotInterpretedHere))
      ((ia:done $error) $error))))

(: ia:finish-else (-> %Undefined% Atom %Undefined%))
(= (ia:finish-else $i $k)
   (case $i
     (((ia:done ia:none) (eval $k))
      ($other (ia:finish $other)))))

;; ---- the commit ---------------------------------------------------------------

; The binding structure the reader gives a transform: R and DEST are read
; in the scope around, the sources are patterns, the template is in their
; scope; inside the sources, these are the source forms.
(scope:binder (transform value source body))
(scope:binder (transform value source body value))
(scope:source transform (, source ...))
(scope:source transform (every source))
(scope:source transform (absent source))
(scope:source transform (absent value pattern))
(scope:source transform (unique source))
(scope:source transform (unique take source))
(scope:source transform (all source pattern))
(scope:source transform (all take source pattern))
(scope:source transform (read value pattern))
(scope:source transform (take value pattern))
(scope:source transform (let pattern value))

(: transform (-> %Undefined% Atom Atom %Undefined%))
(: transform (-> %Undefined% Atom Atom %Undefined% %Undefined%))
(= (transform $r $sources $template)
   (let $i (__cetta_lib_interaction_fire $r $sources $template) (ia:finish (ia:run $i))))
(= (transform $r $sources $template $dest)
   (let $i (__cetta_lib_interaction_fire $r $sources $template $dest) (ia:finish (ia:run $i))))

;; ---- firing at once, later or always --------------------------------------------

(: await (-> Atom %Undefined%))
(= (await $x) (let $i (__cetta_lib_interaction_await $x) (ia:finish (ia:run $i))))

(: whenever (-> Atom %Undefined%))
(= (whenever $x) (let $i (__cetta_lib_interaction_whenever $x) (ia:finish (ia:run $i))))

(: choose (-> Atom Atom %Undefined%))
(: choose (-> Atom Atom Atom %Undefined%))
(: choose (-> Atom Atom Atom Atom %Undefined%))
(= (choose $x $y)
   (let $i (__cetta_lib_interaction_now-choose $x $y) (ia:finish (ia:run $i))))
(= (choose $x $y $z)
   (let $i (__cetta_lib_interaction_now-choose $x $y $z) (ia:finish (ia:run $i))))
(= (choose $x $y $z $w)
   (let $i (__cetta_lib_interaction_now-choose $x $y $z $w) (ia:finish (ia:run $i))))

(: else (-> Atom Atom %Undefined%))
(= (else $x $k)
   (let $i (__cetta_lib_interaction_now $x) (ia:finish-else (ia:run $i) $k)))

;; ---- the agenda ------------------------------------------------------------------

; Run ready processes and woken firings until nothing is left to run, when
; this is the outermost driver; otherwise the outermost driver will.
(= (ia:drive)
   (if (__cetta_lib_interaction_agenda-enter ())
       (let $r (ia:drain 1000000)
         (let $_ (__cetta_lib_interaction_agenda-leave ()) $r))
       ()))

(= (ia:drain $budget)
   (if (<= $budget 0)
       ia:budget-exhausted
       (ia:drain-on (ia:run (__cetta_lib_interaction_agenda-step ())) $budget)))

(= (ia:drain-on $i $budget)
   (case $i
     (((ia:done ia:quiescent) ia:quiescent)
      ((ia:done (ia:process $p))
       (let $_ (collapse (eval $p)) (ia:drain (- $budget 1))))
      ((ia:done (ia:fired $x))
       (let $_ (collapse (eval $x)) (ia:drain (- $budget 1))))
      ($other (ia:drain (- $budget 1))))))

;; ---- processes and names ---------------------------------------------------------

(: par (-> Atom %Undefined%))
(: par (-> Atom Atom %Undefined%))
(: par (-> Atom Atom Atom %Undefined%))
(: par (-> Atom Atom Atom Atom %Undefined%))
(: par (-> Atom Atom Atom Atom Atom %Undefined%))
(: par (-> Atom Atom Atom Atom Atom Atom %Undefined%))
(: par (-> Atom Atom Atom Atom Atom Atom Atom %Undefined%))
(: par (-> Atom Atom Atom Atom Atom Atom Atom Atom %Undefined%))
(= (par $a) (let $s (__cetta_lib_interaction_spawn $a) (ia:spawned $s)))
(= (par $a $b) (let $s (__cetta_lib_interaction_spawn $a $b) (ia:spawned $s)))
(= (par $a $b $c) (let $s (__cetta_lib_interaction_spawn $a $b $c) (ia:spawned $s)))
(= (par $a $b $c $d) (let $s (__cetta_lib_interaction_spawn $a $b $c $d) (ia:spawned $s)))
(= (par $a $b $c $d $e) (let $s (__cetta_lib_interaction_spawn $a $b $c $d $e) (ia:spawned $s)))
(= (par $a $b $c $d $e $f) (let $s (__cetta_lib_interaction_spawn $a $b $c $d $e $f) (ia:spawned $s)))
(= (par $a $b $c $d $e $f $g) (let $s (__cetta_lib_interaction_spawn $a $b $c $d $e $f $g) (ia:spawned $s)))
(= (par $a $b $c $d $e $f $g $h) (let $s (__cetta_lib_interaction_spawn $a $b $c $d $e $f $g $h) (ia:spawned $s)))
(= (ia:spawned $_) (let $d (ia:drive) ()))

(: seq (-> Atom Atom Atom %Undefined%))
(= (seq $e $v $k)
   (let $answers (collapse (eval $e))
     (__cetta_lib_interaction_seq $v $answers $k)))

;; ---- Rholang's receipts ------------------------------------------------------------

; A receipt's patterns bind as a transform's do; its channels are read in the
; scope around.  So does the name seq binds to each answer.
(scope:binder (for source body))
(scope:source for (pattern _ value _ ...))
(scope:binder (contract value pattern body))
(scope:binder (seq value pattern body))

(: for (-> Atom Atom %Undefined%))
(= (for $binds $body) (let $i (__cetta_lib_interaction_for $binds $body) (ia:finish (ia:run $i))))

(: contract (-> %Undefined% Atom Atom %Undefined%))
(= (contract $c $p $body)
   (let $i (__cetta_lib_interaction_contract $c $p $body) (ia:finish (ia:run $i))))
