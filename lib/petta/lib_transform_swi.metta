; One general transform over spaces, written in ordinary MeTTa.
;
;   (transform SPACE SOURCES TEMPLATE)              instances go to the caller
;   (transform SPACE SOURCES TEMPLATE DESTINATION)  instances are routed at the commit
;
; SOURCES is one conjunct, a conjunction (, c1 ... cn), or () for one empty row.
;
;   p                      read a stored atom matching p (one branch per match)
;   (read [S] p)           the same, explicitly; it also matches atoms headed by a mode word
;   (take [S] p)           consume the matched occurrence
;   (absent [S] p)         no stored atom matches p
;   (unique c)             exactly one match of c (a joint match, when c's pattern is
;                          a conjunction)
;   (collect c C)          every match of c, collected as one value matched against C;
;                          a take-mode c takes every member
;   (let P E)              E is evaluated before the commit and matched against P;
;                          no result means no row
;   (all c)                from here on every match: all of them fire in this one commit
;   (once c)               from here on the first match that holds: it alone fires
;
; S names the space of a conjunct; it defaults to SPACE.
;
; Firing.  A transform answers as `match` does: the conjuncts before the first `all`
; or `once` form the kernel, and each kernel match against the store as it is when
; the transform is called is one branch.  A branch is the match it stands for: it
; fires if the atoms its reads and takes matched are still there when it runs, and
; its other conditions are evaluated then, each with its own variables free: absence
; and collections over the store at that moment, a unique counted again with its one
; match the atom the proposal matched, and a let evaluated again, the answer its proposal
; kept first (each answer of a kernel let is a branch of its own); a branch whose atom an
; earlier branch took
; fires nothing.  With reads alone and no `once`, a transform gives exactly the
; answers `match` gives.  From the first `all` on, every extension of the branch's
; kernel match fires in the branch's one commit, so (, (all p) (all q)) is every
; pair, and a `once` after it is part of that extension.  From a `once` before any
; `all`, the first extension that holds fires alone.  (once (transform ...)) gives
; the first answer, and a firing without instances gives none, so it may fire more
; than one branch; a `once` among the sources fires one extension in each branch.
; A footprint
; validates when each taken atom is stored with at least the multiplicity the firing
; takes, plus one more copy when the firing also reads that atom.  A read-mode
; `collect` is a condition on the store before the commit and keeps nothing, so a
; rule may take what its condition counted.  The commit removes the takes and then
; routes the instances.  Nothing runs between validation and the commit.
;
; `,` is conjunction where a query is read and a constructor where data is.  A read
; or take of a pattern headed by `,` reads or takes each part, and absent, unique and
; collect of one range over its joint matches: reads may meet one occurrence twice,
; takes need distinct occurrences, and a take never makes an atom.  Templates, let
; patterns and stored atoms are data, so a stored atom headed by `,` is reached by a
; pattern that is not (a variable, say), as `match` reaches it.
;
; TEMPLATE is instantiated as data by each fired row, as add-atom stores its argument;
; a template (, t1 ... tn) gives n instances per row, by its own syntax: a value a
; source bound to a comma expression is one instance.  DESTINATION is a space, which
; receives the instances and the transform answers how many it routed, or a function,
; applied to each instance after the commit.  Without one, each instance is evaluated
; after the commit, as `match` evaluates its template.  A kernel with no match that
; holds fires nothing and answers nothing; an `all` with no extension fires with no
; instances.
;
; A `let` expression and a template evaluated by the caller are code whose variables
; the sources bind to data: each such variable is quoted before the sources run, so a
; value is never evaluated again, as in `match`.
;
; `remove-atom` removes every copy of an atom, so a take removes them all and stores
; back the copies the firing does not take.  The stored-back copies follow the atoms
; added before the commit.
;
; Hosts whose branches run in parallel.  "Nothing runs between validation and the
; commit" holds by itself where branches run one after another.  Where hyperpose
; runs its branches in threads (SWI-PeTTa), !(tf:atomic!) makes a firing a critical
; section of the host.  A firing without a `let` is one as a whole: its conditions,
; its takes and its routing into a destination space.  A firing with a `let` runs
; code, so its candidates are computed first and the critical section is the commit:
; reads and takes validated against the store as it is then, the takes, the routing.

; How the firing is computed.  lib_transform_specs defines the same operation directly
; from this description; this library gives the same answers and leaves the same store.
;
; - The kernel's matches are proposed once, from the store at the call, each as the
;   atoms its reads, takes and unique conjuncts matched.  A branch fires the original
;   sources with each of those conjuncts guided to its atom ((tf:as P A) unifies the
;   pattern with it), so the match is checked again against the store when it runs.
; - Within a firing, without a `let` among the sources, nothing in the proposal runs
;   code, so the search stops at the first candidate whose footprint validates
;   (`once`).  With a `let`, every candidate is proposed before any is validated.
; - Footprints are built front first and reversed once, so a firing of n rows costs
;   its length rather than its square.
; - A footprint whose atoms are ground and pairwise distinct validates without
;   counting: each taken atom was matched in its space and is taken once.  Sorting
;   the footprint finds the distinct case; any other footprint is counted.
; - When every taken atom is distinct, the commit takes each once, in footprint
;   order, without the list of atoms already taken.

;; ---- the operation ----------------------------------------------------------------

(: transform (-> Atom Atom Atom %Undefined%))
(: transform (-> Atom Atom Atom Atom %Undefined%))
(= (transform $space $sources $template) (tf:run $space $sources $template tf:caller))
(= (transform $space $sources $template $destination)
   (tf:run $space $sources $template $destination))

(= (tf:run $space $sources $template $destination)
   (let $plain (tf:normalize (tf:conjuncts $sources))
     (let $vars (tf:source-vars $plain ())
       (let $conjuncts (tf:protect-lets $vars $plain)
         (let $instance (if (== $destination tf:caller) (tf:protect $vars $template) $template)
           (let $matches (tf:propose $space $conjuncts $instance)
             (let (tf:cand $guide) (superpose $matches)
               (let $kc (tf:inst $conjuncts $guide)
                 (if (is-space $destination)
                     (tf:fire-into $space $kc $instance $destination)
                     (let $comma (tf:comma $instance)
                       (let (tf:fired $instances) (tf:fire $space $kc $instance)
                         (tf:route $comma $instances $destination))))))))))))

; The kernel's matches against the store as it is when the transform is called, each
; as (tf:cand GUIDE): the atoms its read, take and unique conjuncts matched, in
; conjunct order.  Each becomes one branch.
(= (tf:propose $space $conjuncts $template)
   (collapse (tf:walk $space $conjuncts () $template tf:propose)))

; A branch's sources: the original conjuncts, each kernel read or take of a pattern
; P whose proposal matched the atom A becoming that access of A with (tf:as P A), and
; a kernel unique followed by (tf:as P A).  So the branch fires the match it stands
; for if those atoms are still there, and its other conditions (absence, unique's
; count and collections) are evaluated when it fires, each with its own variables
; free.  A kernel let of a pattern P whose proposal answered V becomes
; (tf:let-first V P E): the expression is evaluated again and V is tried first.
(= (tf:inst $cs $guide)
   (if (== $cs ()) ()
     (let ($c $rest) (decons-atom $cs)
       (let $mode (tf:mode $c)
         (if (is-member $mode (all once)) $cs
           (if (is-member $mode (absent collect))
               (let $more (tf:inst $rest $guide) (cons-atom $c $more))
             (if (== $mode let)
               ; the branch stands for the answer V its proposal kept: when it fires
               ; the expression is evaluated again and V is tried first
               (let ($e $guide2) (decons-atom $guide)
                 (let ($_let $_none $value) $e
                   (let ($_l $pattern $expression) (tf:parts $c)
                     (let $again (cons-atom tf:let-first (tf:triple $value $pattern $expression))
                       (let $more (tf:inst $rest $guide2) (cons-atom $again $more))))))
               (let (tf:at $_m $_s $p) (tf:locate-in $c)
                 (if (and (== $mode unique) (tf:comma $p))
                     ; a unique joint match: one guide entry per part, bound as the tuple
                     (let (tf:taken $atoms $guide2) (tf:take-guide (- (size-atom $p) 1) $guide ())
                       (let $as (tf:triple tf:as $p (cons-atom , $atoms))
                         (let $more (tf:inst $rest $guide2) (cons-atom $c (cons-atom $as $more)))))
                     (let ($e $guide2) (decons-atom $guide)
                       (let ($use $s $a) $e
                         (let $as (tf:triple tf:as $p $a)
                           (let $more (tf:inst $rest $guide2)
                             (if (== $mode unique)
                                 (cons-atom $c (cons-atom $as $more))
                                 (let $access (tf:triple $use $s $a)
                                   (cons-atom $access (cons-atom $as $more)))))))))))))))))

; The atoms of the next N guide entries, and the guide after them.
(= (tf:take-guide $n $guide $acc)
   (if (== $n 0) (tf:taken (tf:rev-onto $acc ()) $guide)
     (let ($e $more) (decons-atom $guide)
       (let ($_use $_s $a) $e
         (tf:take-guide (- $n 1) $more (cons-atom $a $acc))))))

; The pattern of a read, take or unique conjunct, without resolving its space.
(= (tf:locate-in $c)
   (let $mode (tf:mode $c)
     (if (== $mode tf:plain) (tf:at read none $c)
       (if (== $mode unique) (tf:locate-in (car-atom (cdr-atom $c)))
         (if (== (size-atom $c) 3)
             (let ($_m $_s $p) (tf:parts $c) (tf:at $mode none $p))
             (tf:at $mode none (car-atom (cdr-atom $c))))))))

; One firing: commit the first candidate whose footprint validates and answer
; (tf:fired INSTANCES), one instance per fired row, in row order; nothing when none
; validates.
(= (tf:fire $space $conjuncts $template)
   (if (tf:atomic?)
       (if (tf:has-let $conjuncts)
           (let $candidates (collapse (tf:walk $space $conjuncts () $template tf:kernel))
             (with_mutex tf:commit (tf:commit-checked $candidates)))
           (with_mutex tf:commit (tf:fire-now $space $conjuncts $template)))
       (tf:fire-now $space $conjuncts $template)))

(= (tf:atomic!) (add-atom &self (tf:atomic)))
(= (tf:atomic?) (not (== (collapse (match &self (tf:atomic) on)) ())))

; The commit beside which other branches may run: the first candidate whose
; footprint validates against the store as it is now.
(= (tf:commit-checked $candidates)
   (if (== $candidates ()) (empty)
     (let ($candidate $more) (decons-atom $candidates)
       (let (tf:batch $kernel $rows) $candidate
         (let $fp (tf:footprint $rows $kernel)
           (if (tf:valid $fp $fp)
               (let $_taken (tf:apply-takes $fp $fp ())
                 (tf:fired (tf:row-instances $rows)))
               (tf:commit-checked $more)))))))

(= (tf:fire-now $space $conjuncts $template)
   (let (tf:ok $distinct $fp $rows)
        (if (tf:has-let $conjuncts)
            (tf:first-ok (collapse (tf:walk $space $conjuncts () $template tf:kernel)))
            (once (tf:try (tf:walk $space $conjuncts () $template tf:kernel))))
     (let $_taken (if $distinct (tf:take-each $fp) (tf:apply-takes $fp $fp ()))
       (tf:fired (tf:row-instances $rows)))))

; The same firing into a destination space: its instances are added after the commit
; and the answer is how many.
(= (tf:fire-into $space $conjuncts $template $destination)
   (let $comma (tf:comma $template)
     (if (tf:atomic?)
         (if (tf:has-let $conjuncts)
             (let $candidates (collapse (tf:walk $space $conjuncts () $template tf:kernel))
               (with_mutex tf:commit
                 (let (tf:fired $instances) (tf:commit-checked $candidates)
                   (tf:add-all $destination (tf:split $comma $instances) 0))))
             (with_mutex tf:commit
               (tf:route-fired $space $conjuncts $template $comma $destination)))
         (tf:route-fired $space $conjuncts $template $comma $destination))))

(= (tf:route-fired $space $conjuncts $template $comma $destination)
   (let (tf:fired $instances) (tf:fire-now $space $conjuncts $template)
     (tf:add-all $destination (tf:split $comma $instances) 0)))

(= (tf:has-let $cs)
   (if (== $cs ()) False
     (let ($c $more) (decons-atom $cs)
       (case (tf:mode $c)
         ((let True)
          (all (if (tf:has-let (tf:conjuncts (car-atom (cdr-atom $c)))) True
                   (tf:has-let $more)))
          (once (if (tf:has-let (tf:conjuncts (car-atom (cdr-atom $c)))) True
                  (tf:has-let $more)))
          ($_other (tf:has-let $more)))))))

;; ---- code and data ----------------------------------------------------------------

(= (tf:source-vars $cs $acc)
   (if (== $cs ()) $acc
     (let ($c $more) (decons-atom $cs)
       (tf:source-vars $more (tf:conjunct-vars $c $acc)))))

(= (tf:conjunct-vars $c $acc)
   (case (tf:mode $c)
     ((let (let ($l $pattern $expression) (tf:parts $c) (tf:vars $pattern $acc)))
      (all (tf:source-vars (tf:conjuncts (car-atom (cdr-atom $c))) $acc))
      (once (tf:source-vars (tf:conjuncts (car-atom (cdr-atom $c))) $acc))
      ($_other (tf:vars $c $acc)))))

; The kind of a term.  A value the host holds that is no atom of the data
; model, such as the partial application (+ 1) once evaluated, has no metatype;
; it is a leaf: it holds no variable to bind and no part to match.
(= (tf:kind $term)
   (case (get-metatype $term) (($kind $kind) (Empty tf:opaque))))

(= (tf:vars $e $acc)
   (case (tf:kind $e)
     ((Variable (if (tf:holds $acc $e) $acc (cons-atom $e $acc)))
      (Expression (tf:vars-each $e $acc))
      ($_other $acc))))

(= (tf:vars-each $xs $acc)
   (if (== $xs ()) $acc
     (let ($x $more) (decons-atom $xs)
       (tf:vars-each $more (tf:vars $x $acc)))))

(= (tf:holds $vars $v)
   (if (== $vars ()) False
     (let ($w $more) (decons-atom $vars)
       (if (== $w $v) True (tf:holds $more $v)))))

(= (tf:protect $vars $e)
   (case (tf:kind $e)
     ((Variable (if (tf:holds $vars $e) (cons-atom quote (cons-atom $e ())) $e))
      (Expression (tf:protect-each $vars $e))
      ($_other $e))))

(= (tf:protect-each $vars $xs)
   (if (== $xs ()) ()
     (let ($x $more) (decons-atom $xs)
       (let $px (tf:protect $vars $x)
         (let $pmore (tf:protect-each $vars $more) (cons-atom $px $pmore))))))

(= (tf:protect-lets $vars $cs)
   (if (== $cs ()) ()
     (let ($c $more) (decons-atom $cs)
       (let $pc (tf:protect-conjunct $vars $c)
         (let $pmore (tf:protect-lets $vars $more) (cons-atom $pc $pmore))))))

(= (tf:protect-conjunct $vars $c)
   (case (tf:mode $c)
     ((let (let ($l $pattern $expression) (tf:parts $c)
             (let $pe (tf:protect $vars $expression) (tf:triple $l $pattern $pe))))
      (all (let $inner (tf:protect-lets $vars (tf:conjuncts (car-atom (cdr-atom $c))))
               (let $wrapped (cons-atom , $inner) (cons-atom all (cons-atom $wrapped ())))))
      (once (let $inner (tf:protect-lets $vars (tf:conjuncts (car-atom (cdr-atom $c))))
                (let $wrapped (cons-atom , $inner) (cons-atom once (cons-atom $wrapped ())))))
      ($_other $c))))

;; ---- conjuncts --------------------------------------------------------------------

; ',' is conjunction where a query is read and a constructor where data is.  Reading
; the sources splices a conjunction nested in the conjunction, and a read or take of
; a pattern headed by ',' (an access of each part, in the conjunct's space), into the
; conjunction they sit in; all and once bodies are read the same way.  absent,
; unique and collect read a pattern headed by ',' as a query over its joint matches.
; Templates, let patterns and values, and the atoms a branch accesses exactly are data.
(= (tf:normalize $cs)
   (if (== $cs ()) ()
     (let ($c $more) (decons-atom $cs)
       (let $head (tf:normal-conjunct $c)
         (let $tail (tf:normalize $more) (tf:append $head $tail))))))

(= (tf:normal-conjunct $c)
   (if (tf:comma $c) (tf:normalize (cdr-atom $c))
     (let $mode (tf:mode $c)
       (if (is-member $mode (read take))
           (let (tf:at $_m $_s $p) (tf:locate-in $c)
             (if (tf:comma $p)
                 (tf:normalize (tf:accesses $c (cdr-atom $p)))
                 (cons-atom $c ())))
           (if (is-member $mode (all once))
               (let $body (tf:normalize (tf:conjuncts (car-atom (cdr-atom $c))))
                 (let $wrapped (if (== $body ()) () (cons-atom , $body))
                   (cons-atom (cons-atom $mode (cons-atom $wrapped ())) ())))
               (cons-atom $c ()))))))

; The conjunct's access of each part: (read p1) ..., or (read S p1) ...
(= (tf:accesses $c $parts)
   (if (== $parts ()) ()
     (let ($p $more) (decons-atom $parts)
       (let $one (if (== (size-atom $c) 3)
                     (tf:triple (car-atom $c) (car-atom (cdr-atom $c)) $p)
                     (cons-atom (car-atom $c) (cons-atom $p ())))
         (let $tail (tf:accesses $c $more) (cons-atom $one $tail))))))

; The atom A itself, matched as data when it is headed by ',': as ($h a1 ... an) with
; $h then ','.  BODY is the match's template.
(= (tf:exact $s $a $body)
   (let $q (cons-atom $h (cdr-atom $a))
     (match $s $q (if (== $h ,) $body (empty)))))

(= (tf:comma $x)
   (if (== (tf:kind $x) Expression)
       (if (== $x ()) False (== (car-atom $x) ,))
       False))

(= (tf:conjuncts $sources)
   (if (== $sources ()) ()
     (if (tf:comma $sources) (cdr-atom $sources) (cons-atom $sources ()))))

(= (tf:mode $c)
   (if (== (tf:kind $c) Expression)
       (if (== $c ()) tf:plain
         (let $head (car-atom $c)
           (if (== (tf:kind $head) Variable) tf:plain
             (if (is-member $head (read take absent unique collect all once let tf:as tf:let-first)) $head tf:plain))))
       tf:plain))

(= (tf:locate $space $c)
   (let $mode (tf:mode $c)
     (if (== $mode tf:plain) (tf:at read $space $c)
       (if (== (size-atom $c) 3)
           (let ($_m $s $p) (tf:parts $c) (tf:at $mode $s $p))
           (tf:at $mode $space (car-atom (cdr-atom $c)))))))

(= (tf:parts $c)
   (let ($head $args) (decons-atom $c)
     (let ($a $more) (decons-atom $args)
       (let $b (car-atom $more) (tf:triple $head $a $b)))))

(= (tf:triple $a $b $c) (cons-atom $a (cons-atom $b (cons-atom $c ()))))

(= (tf:append $xs $ys)
   (if (== $xs ()) $ys
     (let ($x $more) (decons-atom $xs)
       (let $tail (tf:append $more $ys) (cons-atom $x $tail)))))

; $xs reversed onto $acc.
(= (tf:rev-onto $xs $acc)
   (if (== $xs ()) $acc
     (let ($x $more) (decons-atom $xs)
       (tf:rev-onto $more (cons-atom $x $acc)))))

;; ---- proposing rows ---------------------------------------------------------------
; Footprints are kept newest first while a row is proposed.  The kernel phase yields
; candidates (tf:batch KERNEL-FOOTPRINT ROWS) and an extension yields rows
; (tf:row FOOTPRINT INSTANCE).

(= (tf:walk $space $cs $fp $template $phase)
   (if (== $cs ())
       (tf:finish $fp $template $phase)
       (let ($c $rest) (decons-atom $cs)
         (tf:conjunct $space $c $rest $fp $template $phase))))

(= (tf:finish $fp $template $phase)
   (if (== $phase tf:propose) (tf:proposal $fp)
     (if (== $phase tf:kernel)
         (let $row (tf:row () $template) (tf:batch $fp (cons-atom $row ())))
         (tf:row $fp $template))))

(= (tf:conjunct $space $c $rest $fp $template $phase)
   (let $mode (tf:mode $c)
     (case $mode
       ((tf:as (tf:as-conj $space $c $rest $fp $template $phase))
        (all (tf:all $space $c $rest $fp $template $phase))
        (once (tf:once $space $c $rest $fp $template $phase))
        (let (tf:let $space $c $rest $fp $template $phase))
        (tf:let-first (tf:let-first $space $c $rest $fp $template $phase))
        (unique (tf:unique $space $c $rest $fp $template $phase))
        (collect (tf:collect $space $c $rest $fp $template $phase))
        (absent (tf:absent $space $c $rest $fp $template $phase))
        ($_other (tf:match $space $c $rest $fp $template $phase))))))

; A proposal: its kernel footprint in conjunct order.
(= (tf:proposal $fp) (let $guide (tf:rev-onto $fp ()) (tf:cand $guide)))

; `once` scopes to the right like `all`: the matches before it are branches, and
; within a branch its inner conjuncts and everything after it are plain conjuncts of
; the branch's one firing, which commits the first candidate that validates.
(= (tf:once $space $c $rest $fp $template $phase)
   (if (== $phase tf:propose) (tf:proposal $fp)
     (let $cs (tf:append (tf:conjuncts (car-atom (cdr-atom $c))) $rest)
       (tf:walk $space $cs $fp $template $phase))))

(= (tf:all $space $c $rest $fp $template $phase)
   (if (== $phase tf:propose) (tf:proposal $fp)
     (let $inner (car-atom (cdr-atom $c))
       (let $cs (tf:append (tf:conjuncts $inner) $rest)
         (if (== $phase tf:kernel)
             (let $rows (collapse (tf:walk $space $cs () $template tf:extension))
               (tf:batch $fp $rows))
             (tf:walk $space $cs $fp $template $phase))))))

; (tf:as P A): the branch's pattern P is the atom A its proposal matched.
(= (tf:as-conj $space $c $rest $fp $template $phase)
   (let ($_as $p $a) (tf:parts $c)
     (let $p $a (tf:walk $space $rest $fp $template $phase))))

; A kernel `let` chooses: each of its answers is a match of the kernel, so each is
; a proposal of its own, and the proposal keeps the answer beside the atoms it
; matched.
(= (tf:let $space $c $rest $fp $template $phase)
   (let ($_l $pattern $expression) (tf:parts $c)
     (let $value (eval $expression)
       (let $pattern $value
         (let $fp2 (if (== $phase tf:propose) (cons-atom (tf:triple let none $value) $fp) $fp)
           (tf:walk $space $rest $fp2 $template $phase))))))

; (tf:let-first V P E), a kernel let of a branch: the answers of E as they are when
; the branch fires, those equal to the answer V its proposal kept first, the others
; after them, each in the order E gives them.  The answers are collected to be put
; in that order, and a collected answer is a copy: its variables are new.  So each
; is collected beside the instance of E that gave it, and the chosen answer's
; instance is met with E again.  The answer's variables are then E's, those of the
; atoms the branch matched among them, and what E bound in giving the answer holds.
(= (tf:let-first $space $c $rest $fp $template $phase)
   (let ($_h $first $pattern $expression) $c
     (let $answers (collapse (let $value (eval $expression) (tf:answered $value $expression)))
       (let $ordered (tf:append (tf:answers-like True $first $answers)
                                  (tf:answers-like False $first $answers))
         (let (tf:answered $value $instance) (superpose $ordered)
           (let $expression $instance
             (let $pattern $value (tf:walk $space $rest $fp $template $phase))))))))

(= (tf:answers-like $wanted $first $answers)
   (if (== $answers ()) ()
     (let ($a $more) (decons-atom $answers)
       (let (tf:answered $value $_instance) $a
         (let $tail (tf:answers-like $wanted $first $more)
           (if (== (=alpha $value $first) $wanted) (cons-atom $a $tail) $tail))))))

(= (tf:unique $space $c $rest $fp $template $phase)
   (let (tf:at $mode $s $p) (tf:locate $space (car-atom (cdr-atom $c)))
     (let $found (collapse (match $s $p $p))
       (if (== (size-atom $found) 1)
           (let $one (car-atom $found)
             (let $p $one
               (let $fp2 (tf:entries-onto $mode $s $one (tf:comma $p) $fp)
                 (tf:walk $space $rest $fp2 $template $phase))))
           (empty)))))

(= (tf:collect $space $c $rest $fp $template $phase)
   (let ($_a $inner $collection) (tf:parts $c)
     (let (tf:at $mode $s $p) (tf:locate $space $inner)
       (let $joint (tf:comma $p)
         (let $all (collapse (match $s $p $p))
           (let $found (if (and (== $mode take) $joint) (tf:distinct-tuples $s $all) $all)
             (let $collection $found
               (let $fp2 (if (and (== $mode take) (not (== $phase tf:propose)))
                             (tf:takes-onto $s $found $joint $fp) $fp)
                 (tf:walk $space $rest $fp2 $template $phase)))))))))

; A match of a pattern headed by ',' is a joint match: its footprint is each atom of
; the tuple, pushed in order onto the (reversed) footprint.
(= (tf:entries-onto $mode $s $match $joint $fp)
   (if $joint (tf:each-onto $mode $s (cdr-atom $match) $fp) (cons-atom (tf:triple $mode $s $match) $fp)))

(= (tf:each-onto $mode $s $atoms $fp)
   (if (== $atoms ()) $fp
     (let ($a $more) (decons-atom $atoms)
       (tf:each-onto $mode $s $more (cons-atom (tf:triple $mode $s $a) $fp)))))

; A take-mode collect's footprint: for one pattern, each matched occurrence; over a
; conjunction, the occurrences that take part in some joint match, each taken once,
; that is every stored copy of each participating atom.
(= (tf:takes-onto $s $matches $joint $fp)
   (if $joint
       (tf:every-copy-onto $s (tf:participants $matches ()) $fp)
       (if (== $matches ()) $fp
         (let ($a $more) (decons-atom $matches)
           (tf:takes-onto $s $more $joint (cons-atom (tf:triple take $s $a) $fp))))))

; The distinct atoms of the tuples, in order of first appearance.
(= (tf:participants $tuples $seen)
   (if (== $tuples ()) (tf:rev-onto $seen ())
     (let ($t $more) (decons-atom $tuples)
       (tf:participants $more (tf:add-new (cdr-atom $t) $seen)))))

(= (tf:add-new $atoms $seen)
   (if (== $atoms ()) $seen
     (let ($a $more) (decons-atom $atoms)
       (tf:add-new $more (if (is-alpha-member $a $seen) $seen (cons-atom $a $seen))))))

(= (tf:every-copy-onto $s $atoms $fp)
   (if (== $atoms ()) $fp
     (let ($a $more) (decons-atom $atoms)
       (tf:every-copy-onto $s $more (tf:copies-onto $s $a (tf:stored $s $a) $fp)))))

(= (tf:copies-onto $s $a $n $fp)
   (if (<= $n 0) $fp
     (tf:copies-onto $s $a (- $n 1) (cons-atom (tf:triple take $s $a) $fp))))

; The joint matches whose take positions are distinct occurrences: no atom more
; often in the tuple than it is stored.
(= (tf:distinct-tuples $s $tuples)
   (if (== $tuples ()) ()
     (let ($t $more) (decons-atom $tuples)
       (let $tail (tf:distinct-tuples $s $more)
         (if (tf:fits $s (cdr-atom $t) (cdr-atom $t)) (cons-atom $t $tail) $tail)))))

(= (tf:fits $s $atoms $all)
   (if (== $atoms ()) True
     (let ($a $more) (decons-atom $atoms)
       (if (<= (tf:occurrences $a $all) (tf:stored $s $a)) (tf:fits $s $more $all) False))))

(= (tf:occurrences $a $xs)
   (if (== $xs ()) 0
     (let ($x $more) (decons-atom $xs)
       (let $n (tf:occurrences $a $more) (if (=alpha $x $a) (+ $n 1) $n)))))

(= (tf:absent $space $c $rest $fp $template $phase)
   (let (tf:at $_m $s $p) (tf:locate $space $c)
     (if (== (collapse (once (match $s $p found))) ())
         (tf:walk $space $rest $fp $template $phase)
         (empty))))

; Read sources hold no pattern headed by ',' here, so one is an atom a branch
; accesses exactly, which is data.
(= (tf:match $space $c $rest $fp $template $phase)
   (let (tf:at $mode $s $p) (tf:locate $space $c)
     (if (tf:comma $p)
         (tf:exact $s $p
           (let $fp2 (cons-atom (tf:triple $mode $s $p) $fp)
             (tf:walk $space $rest $fp2 $template $phase)))
         (match $s $p
           (let $fp2 (cons-atom (tf:triple $mode $s $p) $fp)
             (tf:walk $space $rest $fp2 $template $phase))))))

;; ---- validation -------------------------------------------------------------------
; The footprint of a candidate is its kernel's, then each row's in row order.

(= (tf:footprint $rows $acc)
   (if (== $rows ()) (tf:rev-onto $acc ())
     (let ($row $more) (decons-atom $rows)
       (let (tf:row $fp $_i) $row
         (tf:footprint $more (tf:append $fp $acc))))))

; (tf:ok DISTINCT FOOTPRINT ROWS) when the candidate's footprint validates.
(= (tf:try $candidate)
   (let (tf:batch $kernel $rows) $candidate
     (let $fp (tf:footprint $rows $kernel)
       (if (tf:distinct-ground $fp)
           (tf:ok True $fp $rows)
           (if (tf:valid $fp $fp) (tf:ok False $fp $rows) (empty))))))

(= (tf:first-ok $candidates)
   (if (== $candidates ()) (empty)
     (let ($candidate $more) (decons-atom $candidates)
       (let $ok (collapse (tf:try $candidate))
         (if (== $ok ()) (tf:first-ok $more) (car-atom $ok))))))

; Ground footprint atoms that are pairwise distinct, each in its space.
(= (tf:distinct-ground $fp)
   (if (== (tf:vars $fp ()) ())
       (tf:no-adjacent-equal (sort-atom (tf:keys $fp)))
       False))

(= (tf:keys $fp)
   (if (== $fp ()) ()
     (let ($e $more) (decons-atom $fp)
       (let ($_mode $s $a) $e
         (let $tail (tf:keys $more) (cons-atom ($a $s) $tail))))))

(= (tf:no-adjacent-equal $xs)
   (if (== $xs ()) True
     (let ($x $more) (decons-atom $xs) (tf:no-adjacent-from $x $more))))

(= (tf:no-adjacent-from $x $xs)
   (if (== $xs ()) True
     (let ($y $more) (decons-atom $xs)
       (if (== $x $y) False (tf:no-adjacent-from $y $more)))))

; The count, for footprints that repeat an atom or hold variables.
(= (tf:count $entry $fp)
   (if (== $fp ()) 0
     (let ($e $more) (decons-atom $fp)
       (let $n (tf:count $entry $more)
         (if (=alpha $e $entry) (+ $n 1) $n)))))

(= (tf:stored $s $a)
   (if (tf:comma $a)
       (size-atom (collapse (tf:exact $s $a found)))
       (size-atom (collapse (match $s $a found)))))

(= (tf:valid $all $todo)
   (if (== $todo ()) True
     (let ($e $more) (decons-atom $todo)
       (if (tf:entry-valid $e $all) (tf:valid $all $more) False))))

(= (tf:entry-valid $e $all)
   (let ($mode $s $a) $e
     (if (== $mode take)
         (let $need (tf:count $e $all)
           (let $read (tf:count (tf:triple read $s $a) $all)
             (>= (tf:stored $s $a) (+ $need (if (> $read 0) 1 0)))))
         True)))

;; ---- the commit -------------------------------------------------------------------

; Each taken atom of a distinct footprint, in footprint order: remove every copy, then
; store back all but the one taken.
(= (tf:take-each $fp)
   (if (== $fp ()) ()
     (let ($e $more) (decons-atom $fp)
       (let ($mode $s $a) $e
         (if (== $mode take)
             (let* (($have (tf:stored $s $a))
                    ($_r (remove-atom $s $a))
                    ($_b (tf:store-back $s $a (- $have 1))))
               (tf:take-each $more))
             (tf:take-each $more))))))

; The commit of any other footprint: each distinct taken atom, in footprint order.
(= (tf:apply-takes $all $todo $done)
   (if (== $todo ()) $done
     (let ($e $more) (decons-atom $todo)
       (let ($mode $s $a) $e
         (if (and (== $mode take) (not (is-alpha-member $e $done)))
             (let* (($have (tf:stored $s $a))
                    ($n (tf:count $e $all))
                    ($_r (remove-atom $s $a))
                    ($_b (tf:store-back $s $a (- $have $n))))
               (tf:apply-takes $all $more (cons-atom $e $done)))
             (tf:apply-takes $all $more $done))))))

(= (tf:store-back $s $a $n)
   (if (<= $n 0) ()
     (let $_x (add-atom $s $a) (tf:store-back $s $a (- $n 1)))))

(= (tf:row-instances $rows)
   (if (== $rows ()) ()
     (let ($row $more) (decons-atom $rows)
       (let (tf:row $_f $instance) $row
         (let $tail (tf:row-instances $more) (cons-atom $instance $tail))))))

;; ---- routing ----------------------------------------------------------------------

; The instances of a firing: a template (, t1 ... tn) gives the n parts of each, any
; other template its instances whole.  The template's own syntax decides, read before
; the firing binds its variables; a value a source bound to a comma expression stays
; one instance.  COMMA is that reading.
(= (tf:split $comma $instances)
   (if $comma (tf:parts-of $instances) $instances))

(= (tf:parts-of $instances)
   (if (== $instances ()) ()
     (let ($i $more) (decons-atom $instances)
       (let $tail (tf:parts-of $more) (tf:append (cdr-atom $i) $tail)))))

(= (tf:route $comma $instances $destination)
   (let $all (tf:split $comma $instances)
     (if (== $destination tf:caller)
         (let $instance (superpose $all) (eval $instance))
         (let $instance (superpose $all) (reduce ($destination $instance))))))

(= (tf:add-all $s $instances $n)
   (if (== $instances ()) $n
     (let ($i $more) (decons-atom $instances)
       (let $_x (add-atom $s $i) (tf:add-all $s $more (+ $n 1))))))
