; One general transform over spaces: its specification, written in ordinary MeTTa.
;
; Each definition here says what the operation means as directly as it can.
; lib_transform computes the same answers and leaves the same store, and is the
; library to import.
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

;; ---- the operation ----------------------------------------------------------------

(: transform (-> Atom Atom Atom %Undefined%))
(: transform (-> Atom Atom Atom Atom %Undefined%))
(= (transform $space $sources $template) (tfs:run $space $sources $template tfs:caller))
(= (transform $space $sources $template $destination)
   (tfs:run $space $sources $template $destination))

(= (tfs:run $space $sources $template $destination)
   (let $plain (tfs:conjuncts $sources)
     (let $vars (tfs:source-vars $plain ())
       (let $conjuncts (tfs:protect-lets $vars $plain)
         (let $instance (if (== $destination tfs:caller) (tfs:protect $vars $template) $template)
           (if (is-space $destination)
               (tfs:fire-into $space $conjuncts $instance $destination)
               (let (tfs:fired $instances) (tfs:fire $space $conjuncts $instance)
                 (tfs:route $instances $destination))))))))

; One firing: propose every candidate, commit the first whose footprint validates and
; answer (tfs:fired INSTANCES), one instance per fired row, in row order.
(= (tfs:fire $space $conjuncts $template)
   (let $candidates (collapse (tfs:walk $space $conjuncts () $template tfs:kernel))
     (tfs:commit-first $candidates)))

; The same firing into a destination space: its instances are added after the commit
; and the answer is how many.
(= (tfs:fire-into $space $conjuncts $template $destination)
   (let (tfs:fired $instances) (tfs:fire $space $conjuncts $template)
     (tfs:add-all $destination (tfs:split $instances) 0)))

;; ---- code and data ----------------------------------------------------------------
; The variables the sources can bind: those of their patterns, collections and `let`
; patterns.  Only these are quoted in code; a variable the code binds itself, such as
; a lambda's, stays as written.

(= (tfs:source-vars $cs $acc)
   (if (== $cs ()) $acc
     (let ($c $more) (decons-atom $cs)
       (tfs:source-vars $more (tfs:conjunct-vars $c $acc)))))

(= (tfs:conjunct-vars $c $acc)
   (case (tfs:mode $c)
     ((let (let ($l $pattern $expression) (tfs:parts $c) (tfs:vars $pattern $acc)))
      (every (tfs:source-vars (tfs:conjuncts (car-atom (cdr-atom $c))) $acc))
      ($_other (tfs:vars $c $acc)))))

(= (tfs:vars $e $acc)
   (case (get-metatype $e)
     ((Variable (if (tfs:holds $acc $e) $acc (cons-atom $e $acc)))
      (Expression (tfs:vars-each $e $acc))
      ($_other $acc))))

(= (tfs:vars-each $xs $acc)
   (if (== $xs ()) $acc
     (let ($x $more) (decons-atom $xs)
       (tfs:vars-each $more (tfs:vars $x $acc)))))

(= (tfs:holds $vars $v)
   (if (== $vars ()) False
     (let ($w $more) (decons-atom $vars)
       (if (== $w $v) True (tfs:holds $more $v)))))

(= (tfs:protect $vars $e)
   (case (get-metatype $e)
     ((Variable (if (tfs:holds $vars $e) (cons-atom quote (cons-atom $e ())) $e))
      (Expression (tfs:protect-each $vars $e))
      ($_other $e))))

(= (tfs:protect-each $vars $xs)
   (if (== $xs ()) ()
     (let ($x $more) (decons-atom $xs)
       (let $px (tfs:protect $vars $x)
         (let $pmore (tfs:protect-each $vars $more) (cons-atom $px $pmore))))))

(= (tfs:protect-lets $vars $cs)
   (if (== $cs ()) ()
     (let ($c $more) (decons-atom $cs)
       (let $pc (tfs:protect-conjunct $vars $c)
         (let $pmore (tfs:protect-lets $vars $more) (cons-atom $pc $pmore))))))

(= (tfs:protect-conjunct $vars $c)
   (case (tfs:mode $c)
     ((let (let ($l $pattern $expression) (tfs:parts $c)
             (let $pe (tfs:protect $vars $expression) (tfs:triple $l $pattern $pe))))
      (every (let $inner (tfs:protect-lets $vars (tfs:conjuncts (car-atom (cdr-atom $c))))
               (let $wrapped (cons-atom , $inner) (cons-atom every (cons-atom $wrapped ())))))
      ($_other $c))))

;; ---- conjuncts --------------------------------------------------------------------

(= (tfs:comma $x)
   (if (== (get-metatype $x) Expression)
       (if (== $x ()) False (== (car-atom $x) ,))
       False))

(= (tfs:conjuncts $sources)
   (if (== $sources ()) ()
     (if (tfs:comma $sources) (cdr-atom $sources) (cons-atom $sources ()))))

(= (tfs:mode $c)
   (if (== (get-metatype $c) Expression)
       (if (== $c ()) tfs:plain
         (let $head (car-atom $c)
           (if (== (get-metatype $head) Variable) tfs:plain
             (if (is-member $head (read take absent unique all every let)) $head tfs:plain))))
       tfs:plain))

; (read|take|absent [S] p) as (tfs:at mode S p); a plain pattern reads from SPACE.
(= (tfs:locate $space $c)
   (let $mode (tfs:mode $c)
     (if (== $mode tfs:plain) (tfs:at read $space $c)
       (if (== (size-atom $c) 3)
           (let ($_m $s $p) (tfs:parts $c) (tfs:at $mode $s $p))
           (tfs:at $mode $space (car-atom (cdr-atom $c)))))))

(= (tfs:parts $c)
   (let ($head $args) (decons-atom $c)
     (let ($a $more) (decons-atom $args)
       (let $b (car-atom $more) (tfs:triple $head $a $b)))))

(= (tfs:triple $a $b $c) (cons-atom $a (cons-atom $b (cons-atom $c ()))))

(= (tfs:append $xs $ys)
   (if (== $xs ()) $ys
     (let ($x $more) (decons-atom $xs)
       (let $tail (tfs:append $more $ys) (cons-atom $x $tail)))))

(= (tfs:snoc $xs $x) (tfs:append $xs (cons-atom $x ())))

;; ---- proposing rows ---------------------------------------------------------------
; A footprint lists (read S a) and (take S a) for the matched atoms a, in conjunct
; order.  The kernel phase yields candidates (tfs:cand FOOTPRINT ROWS) and an
; extension yields rows (tfs:row FOOTPRINT INSTANCE).

(= (tfs:walk $space $cs $fp $template $phase)
   (if (== $cs ())
       (tfs:finish $fp $template $phase)
       (let ($c $rest) (decons-atom $cs)
         (tfs:conjunct $space $c $rest $fp $template $phase))))

(= (tfs:finish $fp $template $phase)
   (if (== $phase tfs:kernel)
       (let $row (tfs:row () $template) (tfs:cand $fp (cons-atom $row ())))
       (tfs:row $fp $template)))

(= (tfs:conjunct $space $c $rest $fp $template $phase)
   (let $mode (tfs:mode $c)
     (case $mode
       ((every (tfs:every $space $c $rest $fp $template $phase))
        (let (tfs:let $space $c $rest $fp $template $phase))
        (unique (tfs:unique $space $c $rest $fp $template $phase))
        (all (tfs:all $space $c $rest $fp $template $phase))
        (absent (tfs:absent $space $c $rest $fp $template $phase))
        ($_other (tfs:match $space $c $rest $fp $template $phase))))))

; `every` scopes to the right: its inner conjuncts and everything after it extend
; the kernel match.
(= (tfs:every $space $c $rest $fp $template $phase)
   (let $inner (car-atom (cdr-atom $c))
     (let $cs (tfs:append (tfs:conjuncts $inner) $rest)
       (if (== $phase tfs:kernel)
           (let $rows (collapse (tfs:walk $space $cs () $template tfs:extension))
             (tfs:cand $fp $rows))
           (tfs:walk $space $cs $fp $template $phase)))))

(= (tfs:let $space $c $rest $fp $template $phase)
   (let ($_l $pattern $expression) (tfs:parts $c)
     (let $value (eval $expression)
       (let $pattern $value (tfs:walk $space $rest $fp $template $phase)))))

(= (tfs:unique $space $c $rest $fp $template $phase)
   (let (tfs:at $mode $s $p) (tfs:locate $space (car-atom (cdr-atom $c)))
     (let $found (collapse (match $s $p $p))
       (if (== (size-atom $found) 1)
           (let $one (car-atom $found)
             (let $p $one
               (let $fp2 (tfs:snoc $fp (tfs:triple $mode $s $one))
                 (tfs:walk $space $rest $fp2 $template $phase))))
           (empty)))))

(= (tfs:all $space $c $rest $fp $template $phase)
   (let ($_a $inner $collection) (tfs:parts $c)
     (let (tfs:at $mode $s $p) (tfs:locate $space $inner)
       (let $found (collapse (match $s $p $p))
         (let $collection $found
           (let $fp2 (if (== $mode take) (tfs:append $fp (tfs:takes-of $s $found)) $fp)
             (tfs:walk $space $rest $fp2 $template $phase)))))))

(= (tfs:takes-of $s $atoms)
   (if (== $atoms ()) ()
     (let ($a $more) (decons-atom $atoms)
       (let $tail (tfs:takes-of $s $more) (cons-atom (tfs:triple take $s $a) $tail)))))

(= (tfs:absent $space $c $rest $fp $template $phase)
   (let (tfs:at $_m $s $p) (tfs:locate $space $c)
     (if (== (collapse (once (match $s $p found))) ())
         (tfs:walk $space $rest $fp $template $phase)
         (empty))))

(= (tfs:match $space $c $rest $fp $template $phase)
   (let (tfs:at $mode $s $p) (tfs:locate $space $c)
     (match $s $p
       (let $fp2 (tfs:snoc $fp (tfs:triple $mode $s $p))
         (tfs:walk $space $rest $fp2 $template $phase)))))

;; ---- the commit -------------------------------------------------------------------
; The footprint of a candidate is its kernel's, then each row's in row order.

(= (tfs:commit-first $candidates)
   (if (== $candidates ()) (empty)
     (let ($candidate $more) (decons-atom $candidates)
       (let (tfs:cand $kernel $rows) $candidate
         (let $fp (tfs:footprint $rows $kernel)
           (if (tfs:valid $fp $fp)
               (let $_taken (tfs:apply-takes $fp $fp ())
                 (tfs:fired (tfs:row-instances $rows)))
               (tfs:commit-first $more)))))))

(= (tfs:footprint $rows $acc)
   (if (== $rows ()) $acc
     (let ($row $more) (decons-atom $rows)
       (let (tfs:row $fp $_i) $row
         (tfs:footprint $more (tfs:append $acc $fp))))))

(= (tfs:row-instances $rows)
   (if (== $rows ()) ()
     (let ($row $more) (decons-atom $rows)
       (let (tfs:row $_f $instance) $row
         (let $tail (tfs:row-instances $more) (cons-atom $instance $tail))))))

(= (tfs:count $entry $fp)
   (if (== $fp ()) 0
     (let ($e $more) (decons-atom $fp)
       (let $n (tfs:count $entry $more)
         (if (=alpha $e $entry) (+ $n 1) $n)))))

(= (tfs:stored $s $a) (size-atom (collapse (match $s $a found))))

(= (tfs:valid $all $todo)
   (if (== $todo ()) True
     (let ($e $more) (decons-atom $todo)
       (if (tfs:entry-valid $e $all) (tfs:valid $all $more) False))))

(= (tfs:entry-valid $e $all)
   (let ($mode $s $a) $e
     (if (== $mode take)
         (let $need (tfs:count $e $all)
           (let $read (tfs:count (tfs:triple read $s $a) $all)
             (>= (tfs:stored $s $a) (+ $need (if (> $read 0) 1 0)))))
         True)))

; Each distinct taken atom, in footprint order: remove every copy, then store back the
; copies the firing does not take.
(= (tfs:apply-takes $all $todo $done)
   (if (== $todo ()) $done
     (let ($e $more) (decons-atom $todo)
       (let ($mode $s $a) $e
         (if (and (== $mode take) (not (is-alpha-member $e $done)))
             (let* (($have (tfs:stored $s $a))
                    ($n (tfs:count $e $all))
                    ($_r (remove-atom $s $a))
                    ($_b (tfs:store-back $s $a (- $have $n))))
               (tfs:apply-takes $all $more (cons-atom $e $done)))
             (tfs:apply-takes $all $more $done))))))

(= (tfs:store-back $s $a $n)
   (if (<= $n 0) ()
     (let $_x (add-atom $s $a) (tfs:store-back $s $a (- $n 1)))))

;; ---- routing ----------------------------------------------------------------------

(= (tfs:split $instances)
   (if (== $instances ()) ()
     (let ($i $more) (decons-atom $instances)
       (let $tail (tfs:split $more)
         (if (tfs:comma $i) (tfs:append (cdr-atom $i) $tail) (cons-atom $i $tail))))))

(= (tfs:route $fired $destination)
   (let $instances (tfs:split $fired)
     (if (== $destination tfs:caller)
         (let $instance (superpose $instances) (eval $instance))
         (let $instance (superpose $instances) (reduce ($destination $instance))))))

(= (tfs:add-all $s $instances $n)
   (if (== $instances ()) $n
     (let ($i $more) (decons-atom $instances)
       (let $_x (add-atom $s $i) (tfs:add-all $s $more (+ $n 1))))))
