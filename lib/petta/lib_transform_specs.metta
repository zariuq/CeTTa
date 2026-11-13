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
; Code keeps its source variables as parameters. Matching supplies their
; values at the argument boundary, without inserting quote syntax into raw
; data arguments inside the body.
;
; The reference clears a taken atom's matching rows and restores the copies
; the firing keeps. Its local loop accepts one-copy or all-copy removal hosts.
; The restored copies follow the atoms added before the commit. Exact takes
; of open stored atoms require the native kernel; this reference counts matches.
;
; Hosts whose branches run in parallel.  "Nothing runs between validation and the
; commit" holds by itself where branches run one after another.  Where hyperpose
; runs its branches in threads (SWI-PeTTa), !(tf:atomic!) makes the commit a
; critical section of the host: a firing's reads and takes are validated, its
; takes are removed and its instances are routed into a destination space with no
; other commit in between.  The firing's other conditions (absence, counts,
; collections, lets) are as of its walk, which runs before and may run code.

;; ---- the operation ----------------------------------------------------------------

(: transform (-> Atom Atom Atom %Undefined%))
(: transform (-> Atom Atom Atom Atom %Undefined%))
(= (transform $space $sources $template) (tfs:run $space $sources $template tfs:caller))
(= (transform $space $sources $template $destination)
   (tfs:run $space $sources $template $destination))

(= (tfs:run $space $sources $template $destination)
   (let $plain (tfs:normalize (tfs:conjuncts $sources))
     (let $vars (tfs:source-vars $plain ())
       (let $conjuncts (tfs:protect-lets $vars $plain)
         (let $instance (if (== $destination tfs:caller) (tfs:protect-template $vars $template) $template)
           (let $matches (tfs:proposals $space $conjuncts $instance)
             (let (tfs:kmatch $guide) (superpose $matches)
               (let $kc (tfs:guided $conjuncts $guide)
                 (if (is-space $destination)
                     (tfs:fire-into $space $kc $instance $destination)
                     (let $comma (tfs:comma $instance)
                       (let (tfs:fired $instances) (tfs:fire $space $kc $instance)
                         (tfs:route $comma $instances $destination))))))))))))

; The kernel's matches against the store as it is when the transform is called, each
; as (tfs:kmatch GUIDE): the atoms its read, take and unique conjuncts matched, in
; conjunct order.  Each becomes one branch.
(= (tfs:proposals $space $conjuncts $template)
   (collapse (tfs:walk $space $conjuncts () $template tfs:propose)))

; A branch's sources: the original conjuncts, each kernel read or take of a pattern
; P whose proposal matched the atom A becoming that access of A with (tf:as P A), and
; a kernel unique followed by (tf:as P A).  A kernel let of a pattern P whose
; proposal answered V becomes (tf:let-first V P E): the expression is evaluated again
; and V is tried first.  Its other conditions are evaluated when it fires, each
; with its own variables free.
(= (tfs:guided $cs $guide)
   (if (== $cs ()) ()
     (let ($c $rest) (decons-atom $cs)
       (let $mode (tfs:mode $c)
         (if (is-member $mode (all once)) $cs
           (if (is-member $mode (absent collect))
               (let $more (tfs:guided $rest $guide) (cons-atom $c $more))
             (if (== $mode let)
               ; the branch stands for the answer V its proposal kept: when it fires
               ; the expression is evaluated again and V is tried first
               (let ($e $guide2) (decons-atom $guide)
                 (let ($_let $_none $value) $e
                   (let ($_l $pattern $expression) (tfs:parts $c)
                     (let $again (cons-atom tf:let-first (tfs:triple $value $pattern $expression))
                       (let $more (tfs:guided $rest $guide2) (cons-atom $again $more))))))
               (let (tfs:at $_m $_s $p) (tfs:locate-in $c)
                 (if (and (== $mode unique) (tfs:comma $p))
                     ; a unique joint match: one guide entry per part, bound as the tuple
                     (let (tfs:taken $atoms $guide2) (tfs:take-guide (- (size-atom $p) 1) $guide ())
                       (let $as (tfs:triple tf:as $p (cons-atom , $atoms))
                         (let $more (tfs:guided $rest $guide2) (cons-atom $c (cons-atom $as $more)))))
                     (let ($e $guide2) (decons-atom $guide)
                       (let ($use $s $a) $e
                         (let $as (tfs:triple tf:as $p $a)
                           (let $more (tfs:guided $rest $guide2)
                             (if (== $mode unique)
                                 (cons-atom $c (cons-atom $as $more))
                                 (let $access (tfs:triple $use $s $a)
                                   (cons-atom $access (cons-atom $as $more)))))))))))))))))

; The atoms of the next N guide entries, and the guide after them.
(= (tfs:take-guide $n $guide $acc)
   (if (== $n 0) (tfs:taken (tfs:reverse $acc ()) $guide)
     (let ($e $more) (decons-atom $guide)
       (let ($_use $_s $a) $e
         (tfs:take-guide (- $n 1) $more (cons-atom $a $acc))))))

(= (tfs:reverse $xs $acc)
   (if (== $xs ()) $acc
     (let ($x $more) (decons-atom $xs) (tfs:reverse $more (cons-atom $x $acc)))))

; The pattern of a read, take or unique conjunct, without resolving its space.
(= (tfs:locate-in $c)
   (let $mode (tfs:mode $c)
     (if (== $mode tfs:plain) (tfs:at read none $c)
       (if (== $mode unique) (tfs:locate-in (car-atom (cdr-atom $c)))
         (if (== (size-atom $c) 3)
             (let ($_m $_s $p) (tfs:parts $c) (tfs:at $mode none $p))
             (tfs:at $mode none (car-atom (cdr-atom $c))))))))

; One firing: propose every candidate, commit the first whose footprint validates and
; answer (tfs:fired INSTANCES), one instance per fired row, in row order.
(= (tfs:fire $space $conjuncts $template)
   (let $candidates (collapse (tfs:walk $space $conjuncts () $template tfs:kernel))
     (if (tfs:atomic?)
         (with_mutex tf:commit (tfs:commit-first $candidates))
         (tfs:commit-first $candidates))))

(= (tf:atomic!) (add-atom &self (tf:atomic)))
(= (tfs:atomic?) (not (== (collapse (match &self (tf:atomic) on)) ())))

; The same firing into a destination space: its instances are added after the commit
; and the answer is how many.
(= (tfs:fire-into $space $conjuncts $template $destination)
   (let $comma (tfs:comma $template)
     (let $candidates (collapse (tfs:walk $space $conjuncts () $template tfs:kernel))
       (if (tfs:atomic?)
           (with_mutex tf:commit (tfs:commit-into $candidates $comma $destination))
           (tfs:commit-into $candidates $comma $destination)))))

(= (tfs:commit-into $candidates $comma $destination)
   (let (tfs:fired $instances) (tfs:commit-first $candidates)
     (tfs:add-all $destination (tfs:split $comma $instances) 0)))

;; ---- code and data ----------------------------------------------------------------
; The variables the sources can bind: those of their patterns, collections and `let`
; patterns.  Only these are quoted in code; a variable the code binds itself, such as
; a lambda's, stays as written.

(= (tfs:source-vars $cs $acc)
   (if (== $cs ()) $acc
     (let ($c $more) (decons-atom $cs)
       (tfs:source-vars $more (tfs:conjunct-vars $c $acc)))))

; The kind of a term.  A value the host holds that is no atom of the data
; model, such as the partial application (+ 1) once evaluated, has no metatype;
; it is a leaf: it holds no variable to bind and no part to match.
(= (tfs:kind $term)
   (case (get-metatype $term) (($kind $kind) (Empty tfs:opaque))))

(= (tfs:conjunct-vars $c $acc)
   (case (tfs:mode $c)
     ((let (let ($l $pattern $expression) (tfs:parts $c) (tfs:vars $pattern $acc)))
      (all (tfs:source-vars (tfs:conjuncts (car-atom (cdr-atom $c))) $acc))
      (once (tfs:source-vars (tfs:conjuncts (car-atom (cdr-atom $c))) $acc))
      ($_other (tfs:vars $c $acc)))))

(= (tfs:vars $e $acc)
   (case (tfs:kind $e)
     ((Variable (if (tfs:holds $acc $e) $acc (cons-atom $e $acc)))
      (Expression (tfs:vars-each $e $acc))
      (Empty $acc)
      ($_other $acc))))

(= (tfs:vars-each $xs $acc)
   (if (== $xs ()) $acc
     (let ($x $more) (decons-atom $xs)
       (tfs:vars-each $more (tfs:vars $x $acc)))))

(= (tfs:holds $vars $v)
   (if (== $vars ()) False
     (let ($w $more) (decons-atom $vars)
       (if (== $w $v) True (tfs:holds $more $v)))))

(= (tfs:code-uses $vars $e)
   ; A compiled callable may have no metatype answer. It is a held value,
   ; not expression syntax whose internals the library may rename.
   (case (tfs:kind $e)
     ((Variable (tfs:holds $vars $e))
      (Expression (tfs:code-uses-each $vars $e))
      (Empty False)
      ($_other False))))

(= (tfs:code-uses-each $vars $xs)
   (if (== $xs ()) False
     (let ($x $more) (decons-atom $xs)
       (if (tfs:code-uses $vars $x) True (tfs:code-uses-each $vars $more)))))

; A private variable is created by an ordinary clause activation.
(= (tfs:fresh-variable) (tfs:fresh $private))
(= (tfs:private-mapping $vars)
   (if (== $vars ()) ()
     (let ($source $more) (decons-atom $vars)
       (let (tfs:fresh $private) (tfs:fresh-variable)
         (let $entry (tfs:triple tfs:renaming $source $private)
           (cons-atom $entry (tfs:private-mapping $more)))))))
(= (tfs:renamed-variable $mapping $variable)
   (if (== $mapping ()) $variable
     (let ($entry $more) (decons-atom $mapping)
       (let ($_tag $source $private) (tfs:parts $entry)
         (if (== $source $variable) $private
             (tfs:renamed-variable $more $variable))))))
(= (tfs:rename-code $mapping $expression)
   (case (tfs:kind $expression)
     ((Variable (tfs:renamed-variable $mapping $expression))
      (Expression (tfs:rename-code-each $mapping $expression))
      (Empty $expression)
      ($_other $expression))))
(= (tfs:rename-code-each $mapping $expressions)
   (if (== $expressions ()) ()
     (let ($one $more) (decons-atom $expressions)
       (let $renamed (tfs:rename-code $mapping $one)
         (cons-atom $renamed (tfs:rename-code-each $mapping $more))))))
(= (tfs:bind-code $mapping $body)
   (if (== $mapping ()) $body
     (let ($entry $more) (decons-atom $mapping)
       (let ($_tag $source $private) (tfs:parts $entry)
         (let $tail (tfs:bind-code $more $body)
           (let $value (cons-atom quote (cons-atom $source ()))
             (cons-atom let (cons-atom $private
               (cons-atom $value (cons-atom $tail ()))))))))))
(= (tfs:protect-code $vars $expression)
   (if (tfs:code-uses $vars $expression)
       (let $mapping (tfs:private-mapping $vars)
         (let $body (tfs:rename-code $mapping $expression)
           (tfs:bind-code $mapping $body)))
       $expression))

(= (tfs:protect-template $vars $template)
   (if (tfs:comma $template)
       (cons-atom , (tfs:protect-templates $vars (cdr-atom $template)))
       (tfs:protect-code $vars $template)))

(= (tfs:protect-templates $vars $templates)
   (if (== $templates ()) ()
     (let ($one $more) (decons-atom $templates)
       (let $code (tfs:protect-code $vars $one)
         (cons-atom $code (tfs:protect-templates $vars $more))))))

(= (tfs:protect-lets $vars $cs)
   (if (== $cs ()) ()
     (let ($c $more) (decons-atom $cs)
       (let $pc (tfs:protect-conjunct $vars $c)
         (let $pmore (tfs:protect-lets $vars $more) (cons-atom $pc $pmore))))))

(= (tfs:protect-conjunct $vars $c)
   (case (tfs:mode $c)
     ((let (let ($l $pattern $expression) (tfs:parts $c)
             (let $pe (tfs:protect-code $vars $expression) (tfs:triple $l $pattern $pe))))
      (all (let $inner (tfs:protect-lets $vars (tfs:conjuncts (car-atom (cdr-atom $c))))
               (let $wrapped (cons-atom , $inner) (cons-atom all (cons-atom $wrapped ())))))
      (once (let $inner (tfs:protect-lets $vars (tfs:conjuncts (car-atom (cdr-atom $c))))
                (let $wrapped (cons-atom , $inner) (cons-atom once (cons-atom $wrapped ())))))
      ($_other $c))))

;; ---- conjuncts --------------------------------------------------------------------

; ',' is conjunction where a query is read and a constructor where data is.  Reading
; the sources splices a conjunction nested in the conjunction, and a read or take of
; a pattern headed by ',' (an access of each part, in the conjunct's space), into the
; conjunction they sit in; all and once bodies are read the same way.  absent,
; unique and collect read a pattern headed by ',' as a query over its joint matches.
; Templates, let patterns and values, and the atoms a branch accesses exactly are data.
(= (tfs:normalize $cs)
   (if (== $cs ()) ()
     (let ($c $more) (decons-atom $cs)
       (let $head (tfs:normal-conjunct $c)
         (let $tail (tfs:normalize $more) (tfs:append $head $tail))))))

(= (tfs:normal-conjunct $c)
   (if (tfs:comma $c) (tfs:normalize (cdr-atom $c))
     (let $mode (tfs:mode $c)
       (if (is-member $mode (read take))
           (let (tfs:at $_m $_s $p) (tfs:locate-in $c)
             (if (tfs:comma $p)
                 (tfs:normalize (tfs:accesses $c (cdr-atom $p)))
                 (cons-atom $c ())))
           (if (is-member $mode (all once))
               (let $body (tfs:normalize (tfs:conjuncts (car-atom (cdr-atom $c))))
                 (let $wrapped (if (== $body ()) () (cons-atom , $body))
                   (cons-atom (cons-atom $mode (cons-atom $wrapped ())) ())))
               (cons-atom $c ()))))))

; The conjunct's access of each part: (read p1) ..., or (read S p1) ...
(= (tfs:accesses $c $parts)
   (if (== $parts ()) ()
     (let ($p $more) (decons-atom $parts)
       (let $one (if (== (size-atom $c) 3)
                     (tfs:triple (car-atom $c) (car-atom (cdr-atom $c)) $p)
                     (cons-atom (car-atom $c) (cons-atom $p ())))
         (let $tail (tfs:accesses $c $more) (cons-atom $one $tail))))))

; The atom A itself, matched as data when it is headed by ',': as ($h a1 ... an) with
; $h then ','.  BODY is the match's template.
(= (tfs:exact $s $a $body)
   (let $q (cons-atom $h (cdr-atom $a))
     (match $s $q (if (== $h ,) $body (empty)))))

(= (tfs:comma $x)
   (if (== (tfs:kind $x) Expression)
       (if (== $x ()) False (== (car-atom $x) ,))
       False))

(= (tfs:conjuncts $sources)
   (if (== $sources ()) ()
     (if (tfs:comma $sources) (cdr-atom $sources) (cons-atom $sources ()))))

(= (tfs:mode $c)
   (if (== (tfs:kind $c) Expression)
       (if (== $c ()) tfs:plain
         (let $head (car-atom $c)
           (if (== (tfs:kind $head) Variable) tfs:plain
             (if (is-member $head (read take absent unique collect all once let tf:as tf:let-first)) $head tfs:plain))))
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
   (if (== $phase tfs:propose) (tfs:kmatch $fp)
     (if (== $phase tfs:kernel)
         (let $row (tfs:row () $template) (tfs:cand $fp (cons-atom $row ())))
         (tfs:row $fp $template))))

(= (tfs:conjunct $space $c $rest $fp $template $phase)
   (let $mode (tfs:mode $c)
     (case $mode
       ((tf:as (tfs:as-conj $space $c $rest $fp $template $phase))
        (all (tfs:all $space $c $rest $fp $template $phase))
        (once (tfs:once $space $c $rest $fp $template $phase))
        (let (tfs:let $space $c $rest $fp $template $phase))
        (tf:let-first (tfs:let-first $space $c $rest $fp $template $phase))
        (unique (tfs:unique $space $c $rest $fp $template $phase))
        (collect (tfs:collect $space $c $rest $fp $template $phase))
        (absent (tfs:absent $space $c $rest $fp $template $phase))
        ($_other (tfs:match $space $c $rest $fp $template $phase))))))

; `all` scopes to the right: its inner conjuncts and everything after it extend
; the kernel match.
; `once` scopes to the right like `all`: the matches before it are branches, and
; within a branch its inner conjuncts and everything after it are plain conjuncts of
; the branch's one firing, which commits the first candidate that validates.
(= (tfs:once $space $c $rest $fp $template $phase)
   (if (== $phase tfs:propose) (tfs:kmatch $fp)
     (let $cs (tfs:append (tfs:conjuncts (car-atom (cdr-atom $c))) $rest)
       (tfs:walk $space $cs $fp $template $phase))))

(= (tfs:all $space $c $rest $fp $template $phase)
   (if (== $phase tfs:propose) (tfs:kmatch $fp)
     (let $inner (car-atom (cdr-atom $c))
       (let $cs (tfs:append (tfs:conjuncts $inner) $rest)
         (if (== $phase tfs:kernel)
             (let $rows (collapse (tfs:walk $space $cs () $template tfs:extension))
               (tfs:cand $fp $rows))
             (tfs:walk $space $cs $fp $template $phase))))))

; (tf:as P A): the branch's pattern P is the atom A its proposal matched.
(= (tfs:as-conj $space $c $rest $fp $template $phase)
   (let ($_as $p $a) (tfs:parts $c)
     (let $p $a (tfs:walk $space $rest $fp $template $phase))))

; A kernel `let` chooses: each of its answers is a match of the kernel, so each is
; a proposal of its own, and the proposal keeps the answer beside the atoms it
; matched.
(= (tfs:let $space $c $rest $fp $template $phase)
   (let ($_l $pattern $expression) (tfs:parts $c)
     (let $value (eval $expression)
       (let $pattern $value
         (let $fp2 (if (== $phase tfs:propose) (tfs:snoc $fp (tfs:triple let none $value)) $fp)
           (tfs:walk $space $rest $fp2 $template $phase))))))

; (tf:let-first V P E), a kernel let of a branch: the answers of E as they are when
; the branch fires, those equal to the answer V its proposal kept first, the others
; after them, each in the order E gives them.  The answers are collected to be put
; in that order, and a collected answer is a copy: its variables are new.  So each
; is collected beside the instance of E that gave it, and the chosen answer's
; instance is met with E again.  The answer's variables are then E's, those of the
; atoms the branch matched among them, and what E bound in giving the answer holds.
(= (tfs:let-first $space $c $rest $fp $template $phase)
   (let ($_h $first $pattern $expression) $c
     (let $answers (collapse (let $value (eval $expression) (tfs:answered $value $expression)))
       (let $ordered (tfs:append (tfs:answers-like True $first $answers)
                                  (tfs:answers-like False $first $answers))
         (let (tfs:answered $value $instance) (superpose $ordered)
           (let $expression $instance
             (let $pattern $value (tfs:walk $space $rest $fp $template $phase))))))))

(= (tfs:answers-like $wanted $first $answers)
   (if (== $answers ()) ()
     (let ($a $more) (decons-atom $answers)
       (let (tfs:answered $value $_instance) $a
         (let $tail (tfs:answers-like $wanted $first $more)
           (if (== (=alpha $value $first) $wanted) (cons-atom $a $tail) $tail))))))

(= (tfs:unique $space $c $rest $fp $template $phase)
   (let (tfs:at $mode $s $p) (tfs:locate $space (car-atom (cdr-atom $c)))
     (let $found (collapse (match $s $p $p))
       (if (== (size-atom $found) 1)
           (let $one (car-atom $found)
             (let $p $one
               (let $fp2 (tfs:append $fp (tfs:entries $mode $s $one (tfs:comma $p)))
                 (tfs:walk $space $rest $fp2 $template $phase))))
           (empty)))))

(= (tfs:collect $space $c $rest $fp $template $phase)
   (let ($_a $inner $collection) (tfs:parts $c)
     (let (tfs:at $mode $s $p) (tfs:locate $space $inner)
       (let $joint (tfs:comma $p)
         (let $all (collapse (match $s $p $p))
           (let $found (if (and (== $mode take) $joint) (tfs:distinct-tuples $s $all) $all)
             (let $collection $found
               (let $fp2 (if (and (== $mode take) (not (== $phase tfs:propose)))
                             (tfs:append $fp (tfs:takes-of $s $found $joint)) $fp)
                 (tfs:walk $space $rest $fp2 $template $phase)))))))))

; A match of a pattern headed by ',' is a joint match: its footprint is each atom of
; the tuple.
(= (tfs:entries $mode $s $match $joint)
   (if $joint (tfs:each-entry $mode $s (cdr-atom $match)) (cons-atom (tfs:triple $mode $s $match) ())))

(= (tfs:each-entry $mode $s $atoms)
   (if (== $atoms ()) ()
     (let ($a $more) (decons-atom $atoms)
       (let $tail (tfs:each-entry $mode $s $more) (cons-atom (tfs:triple $mode $s $a) $tail)))))

; A take-mode collect's footprint: for one pattern, each matched occurrence; over a
; conjunction, the occurrences that take part in some joint match, each taken once,
; that is every stored copy of each participating atom.
(= (tfs:takes-of $s $matches $joint)
   (if $joint
       (tfs:every-copy $s (tfs:participants $matches ()))
       (if (== $matches ()) ()
         (let ($a $more) (decons-atom $matches)
           (let $tail (tfs:takes-of $s $more $joint)
             (cons-atom (tfs:triple take $s $a) $tail))))))

; The distinct atoms of the tuples, in order of first appearance.
(= (tfs:participants $tuples $seen)
   (if (== $tuples ()) (tfs:reverse $seen ())
     (let ($t $more) (decons-atom $tuples)
       (tfs:participants $more (tfs:add-new (cdr-atom $t) $seen)))))

(= (tfs:add-new $atoms $seen)
   (if (== $atoms ()) $seen
     (let ($a $more) (decons-atom $atoms)
       (tfs:add-new $more (if (is-alpha-member $a $seen) $seen (cons-atom $a $seen))))))

(= (tfs:every-copy $s $atoms)
   (if (== $atoms ()) ()
     (let ($a $more) (decons-atom $atoms)
       (let $tail (tfs:every-copy $s $more)
         (tfs:append (tfs:copies take $s $a (tfs:stored $s $a)) $tail)))))

(= (tfs:copies $mode $s $a $n)
   (if (<= $n 0) ()
     (cons-atom (tfs:triple $mode $s $a) (tfs:copies $mode $s $a (- $n 1)))))

; The joint matches whose take positions are distinct occurrences: no atom more
; often in the tuple than it is stored.
(= (tfs:distinct-tuples $s $tuples)
   (if (== $tuples ()) ()
     (let ($t $more) (decons-atom $tuples)
       (let $tail (tfs:distinct-tuples $s $more)
         (if (tfs:fits $s (cdr-atom $t) (cdr-atom $t)) (cons-atom $t $tail) $tail)))))

(= (tfs:fits $s $atoms $all)
   (if (== $atoms ()) True
     (let ($a $more) (decons-atom $atoms)
       (if (<= (tfs:occurrences $a $all) (tfs:stored $s $a)) (tfs:fits $s $more $all) False))))

(= (tfs:occurrences $a $xs)
   (if (== $xs ()) 0
     (let ($x $more) (decons-atom $xs)
       (let $n (tfs:occurrences $a $more) (if (=alpha $x $a) (+ $n 1) $n)))))

(= (tfs:absent $space $c $rest $fp $template $phase)
   (let (tfs:at $_m $s $p) (tfs:locate $space $c)
     (if (== (collapse (once (match $s $p found))) ())
         (tfs:walk $space $rest $fp $template $phase)
         (empty))))

; Read sources hold no pattern headed by ',' here, so one is an atom a branch
; accesses exactly, which is data.
(= (tfs:match $space $c $rest $fp $template $phase)
   (let (tfs:at $mode $s $p) (tfs:locate $space $c)
     (if (tfs:comma $p)
         (tfs:exact $s $p
           (let $fp2 (tfs:snoc $fp (tfs:triple $mode $s $p))
             (tfs:walk $space $rest $fp2 $template $phase)))
         (match $s $p
           (let $fp2 (tfs:snoc $fp (tfs:triple $mode $s $p))
             (tfs:walk $space $rest $fp2 $template $phase))))))

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

(= (tfs:stored $s $a)
   (if (tfs:comma $a)
       (size-atom (collapse (tfs:exact $s $a found)))
       (size-atom (collapse (match $s $a found)))))

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
                    ($_r (tfs:remove-matching $s $a))
                    ($_b (tfs:store-back $s $a (- $have $n))))
               (tfs:apply-takes $all $more (cons-atom $e $done)))
             (tfs:apply-takes $all $more $done))))))

(= (tfs:store-back $s $a $n)
   (if (<= $n 0) ()
     (let $_x (add-atom $s $a) (tfs:store-back $s $a (- $n 1)))))

;; ---- routing ----------------------------------------------------------------------

; The instances of a firing: a template (, t1 ... tn) gives the n parts of each, any
; other template its instances whole.  The template's own syntax decides, read before
; the firing binds its variables; a value a source bound to a comma expression stays
; one instance.  COMMA is that reading.
(= (tfs:split $comma $instances)
   (if $comma (tfs:parts-of $instances) $instances))

(= (tfs:parts-of $instances)
   (if (== $instances ()) ()
     (let ($i $more) (decons-atom $instances)
       (let $tail (tfs:parts-of $more) (tfs:append (cdr-atom $i) $tail)))))

(= (tfs:route $comma $fired $destination)
   (let $instances (tfs:split $comma $fired)
     (if (== $destination tfs:caller)
         (let $instance (superpose $instances) (eval $instance))
         (let $instance (superpose $instances) (reduce ($destination $instance))))))

(= (tfs:add-all $s $instances $n)
   (if (== $instances ()) $n
     (let ($i $more) (decons-atom $instances)
       (let $_x (add-atom $s $i) (tfs:add-all $s $more (+ $n 1))))))

; Finish the selection before the reference stores back its retained copies.
(= (tfs:remove-matching $s $a)
   (if (remove-atom $s $a) (tfs:remove-matching $s $a) ()))
