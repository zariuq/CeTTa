; One general transform over spaces, written in ordinary MeTTa (HE).  It is the
; operation of lib/petta/lib_transform.metta, with HE's own evaluation:
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
; or `once` form the kernel, and each kernel match against the store as it is when the
; transform is called is one branch.  A branch fires its match when it runs, if the
; atoms its reads and takes matched are still there: its absences and collections are
; those of the store at that moment, a unique is counted again with its one match the
; atom the proposal matched, and a branch whose atom an earlier branch took fires
; nothing.  With
; reads alone a transform gives exactly the answers `match` gives.  From the first
; `all` on, every extension of the branch's kernel match fires in the branch's one
; commit, so (, (all p) (all q)) is every pair; from the first `once` on, the first
; extension that holds fires alone.  A footprint validates when each taken atom is
; stored with at least the multiplicity the firing takes, plus one more copy when the
; firing also reads that atom.  A read-mode `collect` is a condition on the store before
; the commit and keeps nothing, so a rule may take what its condition counted.  The
; commit removes the takes and then routes the instances.  Nothing runs between
; validation and the commit.
;
; `,` is conjunction where a query is read and a constructor where data is.  A read
; or take of a pattern headed by `,` reads or takes each part, and absent, unique and
; collect of one range over its joint matches: reads may meet one occurrence twice,
; takes need distinct occurrences, and a take never makes an atom.  Templates, let
; patterns and stored atoms are data, so a stored atom headed by `,` is reached by a
; pattern that is not (a variable, say), as `match` reaches it.
;
; TEMPLATE is instantiated by each fired row; a template (, t1 ... tn) gives n
; instances per row.  DESTINATION is a space, which receives the instances and the
; transform answers how many it routed, or a function, applied to each instance after
; the commit.  Without one, each instance is the answer, evaluated as `match`
; evaluates its template: the values the sources bound are evaluated as HE evaluates
; them in a match template.  A kernel with no match that holds fires nothing and
; answers nothing; an `all` with no extension fires with no instances.
;
; HE's `remove-atom` removes one copy of an atom, so a firing removes exactly the copies
; it takes.

;; ---- the operation ----------------------------------------------------------------

(: transform (-> Atom Atom Atom %Undefined%))
(: transform (-> Atom Atom Atom Atom %Undefined%))
(= (transform $space $sources $template) (tf:run $space $sources $template tf:caller))
(= (transform $space $sources $template $destination)
   (tf:run $space $sources $template $destination))

; Sources, templates and the conjuncts taken from them are syntax: HE evaluates the
; arguments of a function without a declaration, so the functions they pass through
; take them as atoms.
; The library's own data: proposals, candidates, rows, footprint entries and fired
; instances hold syntax and instances that run only once routed.
(: tf:kmatch (-> Atom TfData))
(: tf:guided (-> Atom Atom %Undefined%))
(: tf:locate-in (-> Atom %Undefined%))
(: tf:as-conj (-> Atom Atom Atom Atom Atom Atom %Undefined%))
(: tf:split (-> Atom Atom %Undefined%))
(: tf:route (-> Atom Atom Atom %Undefined%))
(: tf:cand (-> Atom Atom TfData))
(: tf:row (-> Atom Atom TfData))
(: tf:ok (-> Atom Atom Atom TfData))
(: tf:fired (-> Atom TfData))
(: tf:at (-> Atom Atom Atom TfData))
(: tf:p3 (-> Atom Atom Atom TfData))
; A list of conjuncts: HE evaluates a returned list element by element, and a `let`
; conjunct is a call.
(: tf:cs (-> Atom TfData))
(: tf:splice (-> Atom Atom %Undefined%))
(: tf:normalize (-> Atom %Undefined%))
(: tf:normal-conjunct (-> Atom %Undefined%))
(: tf:accesses (-> Atom Atom %Undefined%))
(: tf:take-guide (-> Atom Atom Atom %Undefined%))
(: tf:taken (-> Atom Atom TfData))
(: tf:entries-onto (-> Atom Atom Atom Atom Atom %Undefined%))
(: tf:each-onto (-> Atom Atom Atom Atom %Undefined%))
(: tf:takes-onto (-> Atom Atom Atom Atom %Undefined%))
(: tf:participants (-> Atom Atom %Undefined%))
(: tf:add-new (-> Atom Atom %Undefined%))
(: tf:every-copy-onto (-> Atom Atom Atom %Undefined%))
(: tf:copies-onto (-> Atom Atom Atom Atom %Undefined%))
(: tf:distinct-tuples (-> Atom Atom %Undefined%))
(: tf:fits (-> Atom Atom Atom %Undefined%))
(: tf:occurrences (-> Atom Atom %Undefined%))
(: tf:run (-> Atom Atom Atom Atom %Undefined%))
(: tf:proposals (-> Atom Atom Atom %Undefined%))
(: tf:fire (-> Atom Atom Atom %Undefined%))
(: tf:fire-into (-> Atom Atom Atom Atom %Undefined%))
(: tf:has-let (-> Atom %Undefined%))
(: tf:comma (-> Atom %Undefined%))
(: tf:conjuncts (-> Atom %Undefined%))
(: tf:inner (-> Atom %Undefined%))
(: tf:mode (-> Atom %Undefined%))
(: tf:mode-word (-> Atom %Undefined%))
(: tf:locate (-> Atom Atom %Undefined%))
(: tf:parts (-> Atom %Undefined%))
(: tf:triple (-> Atom Atom Atom %Undefined%))
(: tf:append (-> Atom Atom %Undefined%))
(: tf:walk (-> Atom Atom Atom Atom Atom %Undefined%))
(: tf:finish (-> Atom Atom Atom %Undefined%))
(: tf:conjunct (-> Atom Atom Atom Atom Atom Atom %Undefined%))
(: tf:once (-> Atom Atom Atom Atom Atom Atom %Undefined%))
(: tf:all (-> Atom Atom Atom Atom Atom Atom %Undefined%))
(: tf:let (-> Atom Atom Atom Atom Atom Atom %Undefined%))
(: tf:unique (-> Atom Atom Atom Atom Atom Atom %Undefined%))
(: tf:collect (-> Atom Atom Atom Atom Atom Atom %Undefined%))
(: tf:absent (-> Atom Atom Atom Atom Atom Atom %Undefined%))
(: tf:match (-> Atom Atom Atom Atom Atom Atom %Undefined%))

(= (tf:run $space $sources $template $destination)
   (let (tf:cs $raw) (tf:conjuncts $sources)
    (let (tf:cs $conjuncts) (tf:normalize $raw)
     (let $matches (tf:proposals $space $conjuncts $template)
       (let (tf:kmatch $guide) (superpose $matches)
         (let (tf:cs $kc) (tf:guided $conjuncts $guide)
           (if (tf:is-space $destination)
               (tf:fire-into $space $kc $template $destination)
               (let $comma (tf:comma $template)
                 (let (tf:fired $instances) (tf:fire $space $kc $template)
                   (tf:route $comma $instances $destination))))))))))

; The kernel's matches against the store as it is when the transform is called, each
; as (tf:kmatch GUIDE): the atoms its read, take and unique conjuncts matched, in
; conjunct order.  Each becomes one branch.
(= (tf:proposals $space $conjuncts $template)
   (collapse (tf:walk $space $conjuncts () $template tf:propose)))

; A proposal: its kernel footprint in conjunct order.
(= (tf:proposal $fp) (let $guide (tf:rev-onto $fp ()) (tf:kmatch $guide)))

; A branch's sources: the original conjuncts, each kernel read or take of a pattern
; P whose proposal matched the atom A becoming that access of A with (tf:as P A), and
; a kernel unique followed by (tf:as P A).  So the branch fires the match it stands
; for if those atoms are still there, and its other conditions are evaluated when it
; fires, each with its own variables free.
(= (tf:guided $cs $guide)
   (if (tf:nil $cs) (tf:cs ())
     (let ($c $rest) (decons-atom $cs)
       (let $mode (tf:mode $c)
         (if (if (== $mode tf:m-all) True (== $mode tf:m-once)) (tf:cs $cs)
           (if (if (== $mode tf:m-absent) True (if (== $mode tf:m-collect) True (== $mode tf:m-let)))
               (let (tf:cs $more) (tf:guided $rest $guide)
                 (let $cs2 (cons-atom $c $more) (tf:cs $cs2)))
               (let (tf:at $_m $_s $p) (tf:locate-in $c)
                 (if (if (== $mode tf:m-unique) (tf:comma $p) False)
                     ; a unique joint match: one guide entry per part, bound as the tuple
                     (let $n (- (size-atom $p) 1)
                       (let (tf:taken $atoms $guide2) (tf:take-guide $n $guide ())
                         (let $tuple (cons-atom , $atoms)
                           (let $as (tf:triple tf:as $p $tuple)
                             (let (tf:cs $more) (tf:guided $rest $guide2)
                               (let $tail (cons-atom $as $more)
                                 (let $cs2 (cons-atom $c $tail) (tf:cs $cs2))))))))
                     (let ($e $guide2) (decons-atom $guide)
                       (let ($use $s $a) $e
                         (let $as (tf:triple tf:as $p $a)
                           (let (tf:cs $more) (tf:guided $rest $guide2)
                             (let $tail (cons-atom $as $more)
                               (if (== $mode tf:m-unique)
                                   (let $cs2 (cons-atom $c $tail) (tf:cs $cs2))
                                   (let $access (tf:triple $use $s $a)
                                     (let $cs2 (cons-atom $access $tail) (tf:cs $cs2)))))))))))))))))

; The atoms of the next N guide entries, and the guide after them.
(= (tf:take-guide $n $guide $acc)
   (if (== $n 0)
       (let $atoms (tf:rev-onto $acc ()) (tf:taken $atoms $guide))
       (let ($e $more) (decons-atom $guide)
         (let ($_use $_s $a) $e
           (let $acc2 (cons-atom $a $acc) (tf:take-guide (- $n 1) $more $acc2))))))

; The pattern of a read, take or unique conjunct, without resolving its space.
(= (tf:locate-in $c)
   (let $tag (tf:mode $c)
     (if (== $tag tf:plain) (tf:at read none $c)
       (let ($_m $args) (decons-atom $c)
         (if (== $tag tf:m-unique)
             (let ($inner $_none) (decons-atom $args) (tf:locate-in $inner))
             (let $mode (tf:access $tag)
               (if (== (size-atom $c) 3)
                   (let ($_s $rest) (decons-atom $args)
                     (let ($p $_none) (decons-atom $rest) (tf:at $mode none $p)))
                   (let ($p $_none) (decons-atom $args) (tf:at $mode none $p)))))))))

; One firing: commit the first candidate whose footprint validates and answer
; (tf:fired INSTANCES), one instance per fired row, in row order; nothing when none
; validates.
(= (tf:fire $space $conjuncts $template)
   (let (tf:ok $distinct $fp $rows)
        (if (tf:has-let $conjuncts)
            (let $candidates (collapse (tf:walk $space $conjuncts () $template tf:kernel))
              (tf:first-ok $candidates))
            (tf:first (collapse (tf:try (tf:walk $space $conjuncts () $template tf:kernel)))))
     (let $_taken (if $distinct (tf:take-each $fp) (tf:apply-takes $fp $fp ()))
       (let $instances (tf:row-instances $rows) (tf:fired $instances)))))

; The same firing into a destination space: its instances are added after the commit
; and the answer is how many.
(= (tf:fire-into $space $conjuncts $template $destination)
   (let $comma (tf:comma $template)
     (let (tf:fired $instances) (tf:fire $space $conjuncts $template)
       (let $split (tf:split $comma $instances) (tf:add-all $destination $split 0)))))

(= (tf:has-let $cs)
   (if (tf:nil $cs) False
     (let ($c $more) (decons-atom $cs)
       (let $mode (tf:mode $c)
         (if (== $mode tf:m-let) True
           (if (if (== $mode tf:m-all) True (== $mode tf:m-once))
               (let (tf:cs $inner) (tf:inner $c)
                 (if (tf:has-let $inner) True (tf:has-let $more)))
               (tf:has-let $more)))))))

;; ---- helpers -----------------------------------------------------------------------

; Emptiness and groundness by inspection: HE's `==` evaluates its arguments, and
; syntax or data must not run here.
(: tf:nil (-> Atom Bool))
(= (tf:nil $x) (=alpha $x ()))

(: tf:ground (-> Atom Bool))
(= (tf:ground $e)
   (let $meta (get-metatype $e)
     (if (== $meta Variable) False
       (if (== $meta Expression) (tf:ground-each $e) True))))

(: tf:ground-each (-> Atom Bool))
(= (tf:ground-each $xs)
   (if (tf:nil $xs) True
     (let ($x $more) (decons-atom $xs)
       (if (tf:ground $x) (tf:ground-each $more) False))))

; The first result, or none.
(= (tf:first $results)
   (if (tf:nil $results) (empty) (let ($first $_more) (decons-atom $results) $first)))

(= (tf:alpha-member $x $xs)
   (if (tf:nil $xs) False
     (let ($y $more) (decons-atom $xs)
       (if (=alpha $x $y) True (tf:alpha-member $x $more)))))

; A destination that is a space; a function destination is a symbol.
(= (tf:is-space $d) (== (get-metatype $d) Grounded))

;; ---- conjuncts --------------------------------------------------------------------

; ',' is conjunction where a query is read and a constructor where data is.  Reading
; the sources splices a conjunction nested in the conjunction, and a read or take of
; a pattern headed by ',' (an access of each part, in the conjunct's space), into the
; conjunction they sit in; all and once bodies are read the same way.  absent,
; unique and collect read a pattern headed by ',' as a query over its joint matches.
; Templates, let patterns and values, and the atoms a branch accesses exactly are data.
(= (tf:normalize $cs)
   (if (tf:nil $cs) (tf:cs ())
     (let ($c $more) (decons-atom $cs)
       (let (tf:cs $head) (tf:normal-conjunct $c)
         (let (tf:cs $tail) (tf:normalize $more)
           (tf:splice $head $tail))))))

(= (tf:normal-conjunct $c)
   (if (tf:comma $c)
       (let ($_comma $parts) (decons-atom $c) (tf:normalize $parts))
       (let $mode (tf:mode $c)
         (if (if (== $mode tf:m-read) True (== $mode tf:m-take))
             (let (tf:at $_m $_s $p) (tf:locate-in $c)
               (if (tf:comma $p)
                   (let ($_comma $parts) (decons-atom $p)
                     (let (tf:cs $accesses) (tf:accesses $c $parts) (tf:normalize $accesses)))
                   (let $one (cons-atom $c ()) (tf:cs $one))))
             (if (if (== $mode tf:m-all) True (== $mode tf:m-once))
                 (let (tf:cs $inner) (tf:inner $c)
                   (let (tf:cs $body) (tf:normalize $inner)
                     (let ($word $_args) (decons-atom $c)
                       (let $wrapped (if (tf:nil $body) () (cons-atom , $body))
                         (let $t1 (cons-atom $wrapped ())
                           (let $again (cons-atom $word $t1)
                             (let $one (cons-atom $again ()) (tf:cs $one))))))))
                 (let $one (cons-atom $c ()) (tf:cs $one)))))))

; The conjunct's access of each part: (read p1) ..., or (read S p1) ...
(= (tf:accesses $c $parts)
   (if (tf:nil $parts) (tf:cs ())
     (let ($p $more) (decons-atom $parts)
       (let ($word $args) (decons-atom $c)
         (let $one (if (== (size-atom $c) 3)
                       (let ($sp $_rest) (decons-atom $args) (tf:triple $word $sp $p))
                       (let $t (cons-atom $p ()) (cons-atom $word $t)))
           (let (tf:cs $tail) (tf:accesses $c $more)
             (let $all (cons-atom $one $tail) (tf:cs $all))))))))

(= (tf:comma $x)
   (if (== (get-metatype $x) Expression)
       (if (tf:nil $x) False (let ($head $_args) (decons-atom $x) (=alpha $head ,)))
       False))

(= (tf:conjuncts $sources)
   (if (tf:nil $sources) (tf:cs ())
     (if (tf:comma $sources)
         (let ($_comma $cs) (decons-atom $sources) (tf:cs $cs))
         (let $cs (cons-atom $sources ()) (tf:cs $cs)))))

; Two conjunct lists joined.
(= (tf:splice $xs $ys)
   (if (tf:nil $xs) (tf:cs $ys)
     (let ($x $more) (decons-atom $xs)
       (let (tf:cs $tail) (tf:splice $more $ys)
         (let $cs (cons-atom $x $tail) (tf:cs $cs))))))

; The conjuncts of an (all c) or (once c).
(= (tf:inner $c)
   (let ($_mode $args) (decons-atom $c)
     (let ($inner $_none) (decons-atom $args) (tf:conjuncts $inner))))

(= (tf:mode $c)
   (if (== (get-metatype $c) Expression)
       (if (tf:nil $c) tf:plain
         (let ($head $_args) (decons-atom $c)
           (if (== (get-metatype $head) Variable) tf:plain
             (tf:mode-word $head))))
       tf:plain))

; The mode words, by pattern, as the library's own tags: `unique` and `let` are
; operations, which HE's typed comparison refuses to compare with a symbol.
(= (tf:mode-word $head)
   (case $head
     ((read tf:m-read) (take tf:m-take) (absent tf:m-absent) (unique tf:m-unique)
      (collect tf:m-collect) (all tf:m-all) (once tf:m-once) (let tf:m-let)
      (tf:as tf:m-as)
      ($_other tf:plain))))

; The use of a matched atom in a footprint entry: read or take.
(= (tf:access $tag) (if (== $tag tf:m-take) take read))

(= (tf:locate $space $c)
   (let $tag (tf:mode $c)
     (if (== $tag tf:plain) (tf:at read $space $c)
       (let $mode (tf:access $tag)
         (if (== (size-atom $c) 3)
             (let (tf:p3 $_m $s $p) (tf:parts $c) (tf:at $mode $s $p))
             (let ($_m $args) (decons-atom $c)
               (let ($p $_none) (decons-atom $args) (tf:at $mode $space $p))))))))

; A three-part conjunct's parts, as data: the bare triple of a `let` conjunct would
; be a call.
(= (tf:parts $c)
   (let ($head $args) (decons-atom $c)
     (let ($a $more) (decons-atom $args)
       (let ($b $_none) (decons-atom $more) (tf:p3 $head $a $b)))))

(= (tf:triple $a $b $c)
   (let $t1 (cons-atom $c ())
     (let $t2 (cons-atom $b $t1) (cons-atom $a $t2))))

(= (tf:append $xs $ys)
   (if (tf:nil $xs) $ys
     (let ($x $more) (decons-atom $xs)
       (let $tail (tf:append $more $ys) (cons-atom $x $tail)))))

; $xs reversed onto $acc.
(= (tf:rev-onto $xs $acc)
   (if (tf:nil $xs) $acc
     (let ($x $more) (decons-atom $xs)
       (let $acc2 (cons-atom $x $acc) (tf:rev-onto $more $acc2)))))

;; ---- proposing rows ---------------------------------------------------------------
; Footprints are kept newest first while a row is proposed.  The kernel phase yields
; candidates (tf:cand KERNEL-FOOTPRINT ROWS) and an extension yields rows
; (tf:row FOOTPRINT INSTANCE).

(= (tf:walk $space $cs $fp $template $phase)
   (if (tf:nil $cs)
       (tf:finish $fp $template $phase)
       (let ($c $rest) (decons-atom $cs)
         (tf:conjunct $space $c $rest $fp $template $phase))))

(= (tf:finish $fp $template $phase)
   (if (== $phase tf:propose) (tf:proposal $fp)
     (if (== $phase tf:kernel)
         (let $row (tf:row () $template)
           (let $rows (cons-atom $row ()) (tf:cand $fp $rows)))
         (tf:row $fp $template))))

(= (tf:conjunct $space $c $rest $fp $template $phase)
   (let $mode (tf:mode $c)
     (case $mode
       ((tf:m-as (tf:as-conj $space $c $rest $fp $template $phase))
        (tf:m-all (tf:all $space $c $rest $fp $template $phase))
        (tf:m-once (tf:once $space $c $rest $fp $template $phase))
        (tf:m-let (tf:let $space $c $rest $fp $template $phase))
        (tf:m-unique (tf:unique $space $c $rest $fp $template $phase))
        (tf:m-collect (tf:collect $space $c $rest $fp $template $phase))
        (tf:m-absent (tf:absent $space $c $rest $fp $template $phase))
        ($_other (tf:match $space $c $rest $fp $template $phase))))))

; `once` scopes to the right like `all`: the matches before it are branches, and
; within a branch its inner conjuncts and everything after it are plain conjuncts of
; the branch's one firing, which commits the first candidate that validates.
(= (tf:once $space $c $rest $fp $template $phase)
   (if (== $phase tf:propose) (tf:proposal $fp)
     (let (tf:cs $inner) (tf:inner $c)
       (let (tf:cs $cs) (tf:splice $inner $rest)
         (tf:walk $space $cs $fp $template $phase)))))

(= (tf:all $space $c $rest $fp $template $phase)
   (if (== $phase tf:propose) (tf:proposal $fp)
     (let (tf:cs $inner) (tf:inner $c)
       (let (tf:cs $cs) (tf:splice $inner $rest)
         (if (== $phase tf:kernel)
             (let $rows (collapse (tf:walk $space $cs () $template tf:extension))
               (tf:cand $fp $rows))
             (tf:walk $space $cs $fp $template $phase))))))

; (tf:as P A): the branch's pattern P is the atom A its proposal matched.
(= (tf:as-conj $space $c $rest $fp $template $phase)
   (let (tf:p3 $_as $p $a) (tf:parts $c)
     (let $p $a (tf:walk $space $rest $fp $template $phase))))

(= (tf:let $space $c $rest $fp $template $phase)
   (let (tf:p3 $_l $pattern $expression) (tf:parts $c)
     (let $value $expression
       (let $pattern $value (tf:walk $space $rest $fp $template $phase)))))

(= (tf:unique $space $c $rest $fp $template $phase)
   (let ($_u $args) (decons-atom $c)
     (let ($inner $_none) (decons-atom $args)
       (let (tf:at $mode $s $p) (tf:locate $space $inner)
         (let $joint (tf:comma $p)
           (let $found (collapse (match $s $p $p))
             (if (== (size-atom $found) 1)
                 (let ($one $_none) (decons-atom $found)
                   (let $p $one
                     (let $fp2 (tf:entries-onto $mode $s $one $joint $fp)
                       (tf:walk $space $rest $fp2 $template $phase))))
                 (empty))))))))

; A match of a pattern headed by ',' is a joint match: its footprint is each atom of
; the tuple, pushed in order onto the (newest-first) footprint.
(= (tf:entries-onto $mode $s $match $joint $fp)
   (if $joint
       (let ($_comma $atoms) (decons-atom $match) (tf:each-onto $mode $s $atoms $fp))
       (let $entry (tf:triple $mode $s $match) (cons-atom $entry $fp))))

(= (tf:each-onto $mode $s $atoms $fp)
   (if (tf:nil $atoms) $fp
     (let ($a $more) (decons-atom $atoms)
       (let $entry (tf:triple $mode $s $a)
         (let $fp2 (cons-atom $entry $fp) (tf:each-onto $mode $s $more $fp2))))))

(= (tf:collect $space $c $rest $fp $template $phase)
   (let (tf:p3 $_a $inner $collection) (tf:parts $c)
     (let (tf:at $mode $s $p) (tf:locate $space $inner)
       (let $joint (tf:comma $p)
         (let $all (collapse (match $s $p $p))
           (let $found (if (if (== $mode take) $joint False) (tf:distinct-tuples $s $all) $all)
             (let $collection $found
               (let $fp2 (if (if (== $mode take) (not (== $phase tf:propose)) False)
                             (tf:takes-onto $s $found $joint $fp) $fp)
                 (tf:walk $space $rest $fp2 $template $phase)))))))))

; A take-mode collect's footprint: for one pattern, each matched occurrence; over a
; conjunction, the occurrences that take part in some joint match, each taken once,
; that is every stored copy of each participating atom.
(= (tf:takes-onto $s $matches $joint $fp)
   (if $joint
       (let $atoms (tf:participants $matches ()) (tf:every-copy-onto $s $atoms $fp))
       (if (tf:nil $matches) $fp
         (let ($a $more) (decons-atom $matches)
           (let $entry (tf:triple take $s $a)
             (let $fp2 (cons-atom $entry $fp) (tf:takes-onto $s $more $joint $fp2)))))))

; The distinct atoms of the tuples, in order of first appearance.
(= (tf:participants $tuples $seen)
   (if (tf:nil $tuples) (tf:rev-onto $seen ())
     (let ($t $more) (decons-atom $tuples)
       (let ($_comma $atoms) (decons-atom $t)
         (let $seen2 (tf:add-new $atoms $seen) (tf:participants $more $seen2))))))

(= (tf:add-new $atoms $seen)
   (if (tf:nil $atoms) $seen
     (let ($a $more) (decons-atom $atoms)
       (let $seen2 (if (tf:alpha-member $a $seen) $seen (cons-atom $a $seen))
         (tf:add-new $more $seen2)))))

(= (tf:every-copy-onto $s $atoms $fp)
   (if (tf:nil $atoms) $fp
     (let ($a $more) (decons-atom $atoms)
       (let $n (tf:stored $s $a)
         (let $fp2 (tf:copies-onto $s $a $n $fp) (tf:every-copy-onto $s $more $fp2))))))

(= (tf:copies-onto $s $a $n $fp)
   (if (<= $n 0) $fp
     (let $entry (tf:triple take $s $a)
       (let $fp2 (cons-atom $entry $fp) (tf:copies-onto $s $a (- $n 1) $fp2)))))

; The joint matches whose take positions are distinct occurrences: no atom more
; often in the tuple than it is stored.
(= (tf:distinct-tuples $s $tuples)
   (if (tf:nil $tuples) ()
     (let ($t $more) (decons-atom $tuples)
       (let $tail (tf:distinct-tuples $s $more)
         (let ($_comma $atoms) (decons-atom $t)
           (if (tf:fits $s $atoms $atoms) (cons-atom $t $tail) $tail))))))

(= (tf:fits $s $atoms $all)
   (if (tf:nil $atoms) True
     (let ($a $more) (decons-atom $atoms)
       (if (<= (tf:occurrences $a $all) (tf:stored $s $a)) (tf:fits $s $more $all) False))))

(= (tf:occurrences $a $xs)
   (if (tf:nil $xs) 0
     (let ($x $more) (decons-atom $xs)
       (let $n (tf:occurrences $a $more) (if (=alpha $x $a) (+ $n 1) $n)))))

(= (tf:absent $space $c $rest $fp $template $phase)
   (let (tf:at $_m $s $p) (tf:locate $space $c)
     (let $found (collapse (match $s $p found))
       (if (tf:nil $found)
           (tf:walk $space $rest $fp $template $phase)
           (empty)))))

; Read sources hold no pattern headed by ',' here, so one is an atom a branch
; accesses exactly, which is data: matched as ($h a1 ... an) with $h then ','.
(= (tf:match $space $c $rest $fp $template $phase)
   (let (tf:at $mode $s $p) (tf:locate $space $c)
     (if (tf:comma $p)
         (let ($_comma $args) (decons-atom $p)
           (let $q (cons-atom $h $args)
             (match $s $q
               (if (=alpha $h ,)
                   (let $entry (tf:triple $mode $s $p)
                     (let $fp2 (cons-atom $entry $fp)
                       (tf:walk $space $rest $fp2 $template $phase)))
                   (empty)))))
         (match $s $p
           (let $entry (tf:triple $mode $s $p)
             (let $fp2 (cons-atom $entry $fp)
               (tf:walk $space $rest $fp2 $template $phase)))))))

;; ---- validation -------------------------------------------------------------------
; The footprint of a candidate is its kernel's, then each row's in row order.

(= (tf:footprint $rows $acc)
   (if (tf:nil $rows) (tf:rev-onto $acc ())
     (let ($row $more) (decons-atom $rows)
       (let (tf:row $fp $_i) $row
         (let $acc2 (tf:append $fp $acc) (tf:footprint $more $acc2))))))

; (tf:ok DISTINCT FOOTPRINT ROWS) when the candidate's footprint validates.
(= (tf:try $candidate)
   (let (tf:cand $kernel $rows) $candidate
     (let $fp (tf:footprint $rows $kernel)
       (if (tf:distinct-ground $fp)
           (tf:ok True $fp $rows)
           (if (tf:valid $fp $fp) (tf:ok False $fp $rows) (empty))))))

(= (tf:first-ok $candidates)
   (if (tf:nil $candidates) (empty)
     (let ($candidate $more) (decons-atom $candidates)
       (let $ok (collapse (tf:try $candidate))
         (if (tf:nil $ok) (tf:first-ok $more) (let ($first $_none) (decons-atom $ok) $first))))))

; Ground footprint atoms that are pairwise distinct, each in its space.
(= (tf:distinct-ground $fp)
   (if (tf:ground $fp)
       (let $keys (tf:keys $fp)
         (let $sorted (sort-atom $keys) (tf:no-adjacent-equal $sorted)))
       False))

(= (tf:keys $fp)
   (if (tf:nil $fp) ()
     (let ($e $more) (decons-atom $fp)
       (let ($_mode $s $a) $e
         (let $tail (tf:keys $more) (cons-atom ($a $s) $tail))))))

(= (tf:no-adjacent-equal $xs)
   (if (tf:nil $xs) True
     (let ($x $more) (decons-atom $xs) (tf:no-adjacent-from $x $more))))

(= (tf:no-adjacent-from $x $xs)
   (if (tf:nil $xs) True
     (let ($y $more) (decons-atom $xs)
       (if (=alpha $x $y) False (tf:no-adjacent-from $y $more)))))

; The count, for footprints that repeat an atom or hold variables.
(= (tf:count $entry $fp)
   (if (tf:nil $fp) 0
     (let ($e $more) (decons-atom $fp)
       (let $n (tf:count $entry $more)
         (if (=alpha $e $entry) (+ $n 1) $n)))))

(= (tf:stored $s $a)
   (if (tf:comma $a)
       (let ($_comma $args) (decons-atom $a)
         (let $q (cons-atom $h $args)
           (let $found (collapse (match $s $q (if (=alpha $h ,) found (empty)))) (size-atom $found))))
       (let $found (collapse (match $s $a found)) (size-atom $found))))

(= (tf:valid $all $todo)
   (if (tf:nil $todo) True
     (let ($e $more) (decons-atom $todo)
       (if (tf:entry-valid $e $all) (tf:valid $all $more) False))))

(= (tf:entry-valid $e $all)
   (let ($mode $s $a) $e
     (if (== $mode take)
         (let $need (tf:count $e $all)
           (let $readentry (tf:triple read $s $a)
             (let $read (tf:count $readentry $all)
               (>= (tf:stored $s $a) (+ $need (if (> $read 0) 1 0))))))
         True)))

;; ---- the commit -------------------------------------------------------------------

; Each taken atom of a distinct footprint, in footprint order, removed once: HE's
; remove-atom removes one copy.
(= (tf:take-each $fp)
   (if (tf:nil $fp) ()
     (let ($e $more) (decons-atom $fp)
       (let ($mode $s $a) $e
         (if (== $mode take)
             (let $_r (remove-atom $s $a) (tf:take-each $more))
             (tf:take-each $more))))))

; The commit of any other footprint: each distinct taken atom, in footprint order.
(= (tf:apply-takes $all $todo $done)
   (if (tf:nil $todo) $done
     (let ($e $more) (decons-atom $todo)
       (let ($mode $s $a) $e
         (if (and (== $mode take) (not (tf:alpha-member $e $done)))
             (let* (($n (tf:count $e $all))
                    ($_r (tf:remove-n $s $a $n))
                    ($done2 (cons-atom $e $done)))
               (tf:apply-takes $all $more $done2))
             (tf:apply-takes $all $more $done))))))

; $n copies of $a removed, one by one.
(= (tf:remove-n $s $a $n)
   (if (<= $n 0) ()
     (let $_x (remove-atom $s $a) (tf:remove-n $s $a (- $n 1)))))

(= (tf:row-instances $rows)
   (if (tf:nil $rows) ()
     (let ($row $more) (decons-atom $rows)
       (let (tf:row $_f $instance) $row
         (let $tail (tf:row-instances $more) (cons-atom $instance $tail))))))

;; ---- routing ----------------------------------------------------------------------

; The instances of a firing: a template (, t1 ... tn) gives the n parts of each, any
; other template its instances whole, by the template's own syntax, read before the
; firing binds its variables.  COMMA is that reading.
(= (tf:split $comma $instances)
   (if $comma (tf:parts-of $instances) $instances))

(= (tf:parts-of $instances)
   (if (tf:nil $instances) ()
     (let ($i $more) (decons-atom $instances)
       (let $tail (tf:parts-of $more)
         (let ($_comma $items) (decons-atom $i) (tf:append $items $tail))))))

(= (tf:route $comma $instances $destination)
   (let $all (tf:split $comma $instances)
     (if (== $destination tf:caller)
         (let $instance (superpose $all) $instance)
         (let $instance (superpose $all) ($destination $instance)))))

(= (tf:add-all $s $instances $n)
   (if (tf:nil $instances) $n
     (let ($i $more) (decons-atom $instances)
       (let $_x (add-atom $s $i) (tf:add-all $s $more (+ $n 1))))))
