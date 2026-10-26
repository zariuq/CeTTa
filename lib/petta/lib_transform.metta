; One general transform over spaces, written in ordinary MeTTa.
;
;   (transform SPACE SOURCES TEMPLATE)              instances go to the caller
;   (transform SPACE SOURCES TEMPLATE DESTINATION)  instances are routed at the commit
;
; SOURCES is one conjunct, a conjunction (, c1 ... cn), or () for one empty row.
;
;   p                      read one stored atom matching p (a choice among the matches)
;   (read [S] p)           the same, explicitly; it also matches atoms headed by a mode word
;   (take [S] p)           consume the matched occurrence
;   (absent [S] p)         no stored atom matches p
;   (unique c)             exactly one match of c
;   (all c C)              every match of c, collected as one value matched against C;
;                          a take-mode c takes every member
;   (let P E)              E is evaluated before the commit and matched against P;
;                          no result means no row
;   (every c)              from here on every match: all of them fire in this one commit
;
; S names the space of a conjunct; it defaults to SPACE.
;
; Firing.  The conjuncts before the first `every` form the kernel: one kernel match
; fires, the first in match order whose footprint validates.  From the first `every`
; on, every extension of that kernel match fires in the same commit, so
; (, (every p) (every q)) is every pair.  A footprint validates when each taken atom
; is stored with at least the multiplicity the firing takes, plus one more copy when
; the firing also reads that atom.  A read-mode `all` is a condition on the store
; before the commit and keeps nothing, so a rule may take what its condition counted.
; The commit removes the takes and then routes the instances.  Nothing runs between
; validation and the commit.
;
; TEMPLATE is instantiated as data by each fired row, as add-atom stores its argument;
; a template (, t1 ... tn) gives n instances per row.  DESTINATION is a space, which
; receives the instances and the transform answers how many it routed, or a function,
; applied to each instance after the commit.  Without one, each instance is evaluated
; after the commit, as `match` evaluates its template.  A kernel with no validating
; match fires nothing and answers nothing; an `every` with no extension fires with no
; instances.
;
; A `let` expression and a template evaluated by the caller are code whose variables
; the sources bind to data: each such variable is quoted before the sources run, so a
; value is never evaluated again, as in `match`.
;
; `remove-atom` removes every copy of an atom, so a take removes them all and stores
; back the copies the firing does not take.  The stored-back copies follow the atoms
; added before the commit.

; How the firing is computed.  lib_transform_specs defines the same operation directly
; from this description; this library gives the same answers and leaves the same store.
;
; - Without a `let` among the sources, nothing in the proposal runs code, so the
;   search stops at the first candidate whose footprint validates (`once`).  With a
;   `let`, every candidate is proposed before any is validated.
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
   (let $plain (tf:conjuncts $sources)
     (let $vars (tf:source-vars $plain ())
       (let $conjuncts (tf:protect-lets $vars $plain)
         (let $instance (if (== $destination tf:caller) (tf:protect $vars $template) $template)
           (if (is-space $destination)
               (tf:fire-into $space $conjuncts $instance $destination)
               (let (tf:fired $instances) (tf:fire $space $conjuncts $instance)
                 (tf:route $instances $destination))))))))

; One firing: commit the first candidate whose footprint validates and answer
; (tf:fired INSTANCES), one instance per fired row, in row order; nothing when none
; validates.
(= (tf:fire $space $conjuncts $template)
   (let (tf:ok $distinct $fp $rows)
        (if (tf:has-let $conjuncts)
            (tf:first-ok (collapse (tf:walk $space $conjuncts () $template tf:kernel)))
            (once (tf:try (tf:walk $space $conjuncts () $template tf:kernel))))
     (let $_taken (if $distinct (tf:take-each $fp) (tf:apply-takes $fp $fp ()))
       (tf:fired (tf:row-instances $rows)))))

; The same firing into a destination space: its instances are added after the commit
; and the answer is how many.
(= (tf:fire-into $space $conjuncts $template $destination)
   (let (tf:fired $instances) (tf:fire $space $conjuncts $template)
     (tf:add-all $destination (tf:split $instances) 0)))

(= (tf:has-let $cs)
   (if (== $cs ()) False
     (let ($c $more) (decons-atom $cs)
       (case (tf:mode $c)
         ((let True)
          (every (if (tf:has-let (tf:conjuncts (car-atom (cdr-atom $c)))) True
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
      (every (tf:source-vars (tf:conjuncts (car-atom (cdr-atom $c))) $acc))
      ($_other (tf:vars $c $acc)))))

(= (tf:vars $e $acc)
   (case (get-metatype $e)
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
   (case (get-metatype $e)
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
      (every (let $inner (tf:protect-lets $vars (tf:conjuncts (car-atom (cdr-atom $c))))
               (let $wrapped (cons-atom , $inner) (cons-atom every (cons-atom $wrapped ())))))
      ($_other $c))))

;; ---- conjuncts --------------------------------------------------------------------

(= (tf:comma $x)
   (if (== (get-metatype $x) Expression)
       (if (== $x ()) False (== (car-atom $x) ,))
       False))

(= (tf:conjuncts $sources)
   (if (== $sources ()) ()
     (if (tf:comma $sources) (cdr-atom $sources) (cons-atom $sources ()))))

(= (tf:mode $c)
   (if (== (get-metatype $c) Expression)
       (if (== $c ()) tf:plain
         (let $head (car-atom $c)
           (if (== (get-metatype $head) Variable) tf:plain
             (if (is-member $head (read take absent unique all every let)) $head tf:plain))))
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
; candidates (tf:cand KERNEL-FOOTPRINT ROWS) and an extension yields rows
; (tf:row FOOTPRINT INSTANCE).

(= (tf:walk $space $cs $fp $template $phase)
   (if (== $cs ())
       (tf:finish $fp $template $phase)
       (let ($c $rest) (decons-atom $cs)
         (tf:conjunct $space $c $rest $fp $template $phase))))

(= (tf:finish $fp $template $phase)
   (if (== $phase tf:kernel)
       (let $row (tf:row () $template) (tf:cand $fp (cons-atom $row ())))
       (tf:row $fp $template)))

(= (tf:conjunct $space $c $rest $fp $template $phase)
   (let $mode (tf:mode $c)
     (case $mode
       ((every (tf:every $space $c $rest $fp $template $phase))
        (let (tf:let $space $c $rest $fp $template $phase))
        (unique (tf:unique $space $c $rest $fp $template $phase))
        (all (tf:all $space $c $rest $fp $template $phase))
        (absent (tf:absent $space $c $rest $fp $template $phase))
        ($_other (tf:match $space $c $rest $fp $template $phase))))))

(= (tf:every $space $c $rest $fp $template $phase)
   (let $inner (car-atom (cdr-atom $c))
     (let $cs (tf:append (tf:conjuncts $inner) $rest)
       (if (== $phase tf:kernel)
           (let $rows (collapse (tf:walk $space $cs () $template tf:extension))
             (tf:cand $fp $rows))
           (tf:walk $space $cs $fp $template $phase)))))

(= (tf:let $space $c $rest $fp $template $phase)
   (let ($_l $pattern $expression) (tf:parts $c)
     (let $value (eval $expression)
       (let $pattern $value (tf:walk $space $rest $fp $template $phase)))))

(= (tf:unique $space $c $rest $fp $template $phase)
   (let (tf:at $mode $s $p) (tf:locate $space (car-atom (cdr-atom $c)))
     (let $found (collapse (match $s $p $p))
       (if (== (size-atom $found) 1)
           (let $one (car-atom $found)
             (let $p $one
               (let $fp2 (cons-atom (tf:triple $mode $s $one) $fp)
                 (tf:walk $space $rest $fp2 $template $phase))))
           (empty)))))

(= (tf:all $space $c $rest $fp $template $phase)
   (let ($_a $inner $collection) (tf:parts $c)
     (let (tf:at $mode $s $p) (tf:locate $space $inner)
       (let $found (collapse (match $s $p $p))
         (let $collection $found
           (let $fp2 (if (== $mode take) (tf:takes-onto $s $found $fp) $fp)
             (tf:walk $space $rest $fp2 $template $phase)))))))

(= (tf:takes-onto $s $atoms $fp)
   (if (== $atoms ()) $fp
     (let ($a $more) (decons-atom $atoms)
       (tf:takes-onto $s $more (cons-atom (tf:triple take $s $a) $fp)))))

(= (tf:absent $space $c $rest $fp $template $phase)
   (let (tf:at $_m $s $p) (tf:locate $space $c)
     (if (== (collapse (once (match $s $p found))) ())
         (tf:walk $space $rest $fp $template $phase)
         (empty))))

(= (tf:match $space $c $rest $fp $template $phase)
   (let (tf:at $mode $s $p) (tf:locate $space $c)
     (match $s $p
       (let $fp2 (cons-atom (tf:triple $mode $s $p) $fp)
         (tf:walk $space $rest $fp2 $template $phase)))))

;; ---- validation -------------------------------------------------------------------
; The footprint of a candidate is its kernel's, then each row's in row order.

(= (tf:footprint $rows $acc)
   (if (== $rows ()) (tf:rev-onto $acc ())
     (let ($row $more) (decons-atom $rows)
       (let (tf:row $fp $_i) $row
         (tf:footprint $more (tf:append $fp $acc))))))

; (tf:ok DISTINCT FOOTPRINT ROWS) when the candidate's footprint validates.
(= (tf:try $candidate)
   (let (tf:cand $kernel $rows) $candidate
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

(= (tf:stored $s $a) (size-atom (collapse (match $s $a found))))

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

(= (tf:split $instances)
   (if (== $instances ()) ()
     (let ($i $more) (decons-atom $instances)
       (let $tail (tf:split $more)
         (if (tf:comma $i) (tf:append (cdr-atom $i) $tail) (cons-atom $i $tail))))))

(= (tf:route $instances $destination)
   (let $all (tf:split $instances)
     (if (== $destination tf:caller)
         (let $instance (superpose $all) (eval $instance))
         (let $instance (superpose $all) (reduce ($destination $instance))))))

(= (tf:add-all $s $instances $n)
   (if (== $instances ()) $n
     (let ($i $more) (decons-atom $instances)
       (let $_x (add-atom $s $i) (tf:add-all $s $more (+ $n 1))))))
