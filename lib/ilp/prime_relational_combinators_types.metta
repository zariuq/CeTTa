; Proof-relevant relational combinators over the native Prime `list` family.
; Predicate arguments remain ordinary Prime values.  These families describe
; the evidence retained by a specialization; none is a privileged evaluator
; constructor.

(: rel:list:empty
  (-> (element : (u $element-level))
      (values : (list element))
      (u $element-level)))

(: rel:list:head
  (-> (element : (u $element-level))
      (values : (list element))
      (head : element)
      (u $element-level)))

(: rel:list:tail
  (-> (element : (u $element-level))
      (values : (list element))
      (tail : (list element))
      (u $element-level)))

(: rel:list:member
  (-> (element : (u $element-level))
      (values : (list element))
      (member : element)
      (u $element-level)))

(: rel:list:empty-proof
  (-> (element : (u $element-level))
      (rel:list:empty element (list:nil element))))

(: rel:list:head-proof
  (-> (element : (u $element-level))
      (head : element)
      (tail : (list element))
      (rel:list:head element
        (list:cons element head tail) head)))

(: rel:list:tail-proof
  (-> (element : (u $element-level))
      (head : element)
      (tail : (list element))
      (rel:list:tail element
        (list:cons element head tail) tail)))

(: rel:list:member-here
  (-> (element : (u $element-level))
      (head : element)
      (tail : (list element))
      (rel:list:member element
        (list:cons element head tail) head)))

(: rel:list:member-there
  (-> (element : (u $element-level))
      (head : element)
      (tail : (list element))
      (member : element)
      (tail-evidence : (rel:list:member element tail member))
      (rel:list:member element
        (list:cons element head tail) member)))

(: rel:list:snoc
  (-> (element : (u $element-level))
      (values : (list element))
      (last : element)
      (result : (list element))
      (u $element-level)))

(: rel:list:snoc:nil
  (-> (element : (u $element-level))
      (last : element)
      (rel:list:snoc element (list:nil element) last
        (list:cons element last (list:nil element)))))

(: rel:list:snoc:cons
  (-> (element : (u $element-level))
      (head : element)
      (tail : (list element))
      (last : element)
      (result-tail : (list element))
      (tail-evidence :
        (rel:list:snoc element tail last result-tail))
      (rel:list:snoc element
        (list:cons element head tail) last
        (list:cons element head result-tail))))

(: rel:list:front
  (-> (element : (u $element-level))
      (values : (list element))
      (front : (list element))
      (u $element-level)))

(: rel:list:front:singleton
  (-> (element : (u $element-level))
      (head : element)
      (rel:list:front element
        (list:cons element head (list:nil element))
        (list:nil element))))

(: rel:list:front:cons
  (-> (element : (u $element-level))
      (head : element)
      (next : element)
      (tail : (list element))
      (front-tail : (list element))
      (tail-evidence :
        (rel:list:front element
          (list:cons element next tail) front-tail))
      (rel:list:front element
        (list:cons element head (list:cons element next tail))
        (list:cons element head front-tail))))

(: rel:list:last
  (-> (element : (u $element-level))
      (values : (list element))
      (last : element)
      (u $element-level)))

(: rel:list:last:singleton
  (-> (element : (u $element-level))
      (last : element)
      (rel:list:last element
        (list:cons element last (list:nil element)) last)))

(: rel:list:last:cons
  (-> (element : (u $element-level))
      (head : element)
      (next : element)
      (tail : (list element))
      (last : element)
      (tail-evidence :
        (rel:list:last element
          (list:cons element next tail) last))
      (rel:list:last element
        (list:cons element head (list:cons element next tail)) last)))

(: rel:iterate
  (-> (value : (u $value-level))
      (counter : (u $counter-level))
      (step :
        (-> (source : value) (target : value)
            (u $evidence-level)))
      (predecessor :
        (-> (later : counter) (earlier : counter)
            (u $evidence-level)))
      (zero : counter)
      (source : value)
      (count : counter)
      (target : value)
      (u $evidence-level)))

(: rel:iterate:zero
  (-> (value : (u $value-level))
      (counter : (u $counter-level))
      (step :
        (-> (source : value) (target : value)
            (u $evidence-level)))
      (predecessor :
        (-> (later : counter) (earlier : counter)
            (u $evidence-level)))
      (zero : counter)
      (source : value)
      (rel:iterate value counter step predecessor zero
        source zero source)))

(: rel:iterate:step
  (-> (value : (u $value-level))
      (counter : (u $counter-level))
      (step :
        (-> (source : value) (target : value)
            (u $evidence-level)))
      (predecessor :
        (-> (later : counter) (earlier : counter)
            (u $evidence-level)))
      (zero : counter)
      (source : value)
      (next : value)
      (later : counter)
      (earlier : counter)
      (target : value)
      (predecessor-evidence : (predecessor later earlier))
      (step-evidence : (step source next))
      (recursive-evidence :
        (rel:iterate value counter step predecessor zero
          next earlier target))
      (rel:iterate value counter step predecessor zero
        source later target)))

(: rel:iterate-with
  (-> (environment : (u $environment-level))
      (value : (u $value-level))
      (counter : (u $counter-level))
      (step :
        (-> (parameter : environment)
            (source : value) (target : value)
            (u $evidence-level)))
      (predecessor :
        (-> (later : counter) (earlier : counter)
            (u $evidence-level)))
      (zero : counter)
      (parameter : environment)
      (source : value)
      (count : counter)
      (target : value)
      (u $evidence-level)))

(: rel:iterate-with:zero
  (-> (environment : (u $environment-level))
      (value : (u $value-level))
      (counter : (u $counter-level))
      (step :
        (-> (parameter : environment)
            (source : value) (target : value)
            (u $evidence-level)))
      (predecessor :
        (-> (later : counter) (earlier : counter)
            (u $evidence-level)))
      (zero : counter)
      (parameter : environment)
      (source : value)
      (rel:iterate-with environment value counter step predecessor zero
        parameter source zero source)))

(: rel:iterate-with:step
  (-> (environment : (u $environment-level))
      (value : (u $value-level))
      (counter : (u $counter-level))
      (step :
        (-> (parameter : environment)
            (source : value) (target : value)
            (u $evidence-level)))
      (predecessor :
        (-> (later : counter) (earlier : counter)
            (u $evidence-level)))
      (zero : counter)
      (parameter : environment)
      (source : value)
      (next : value)
      (later : counter)
      (earlier : counter)
      (target : value)
      (predecessor-evidence : (predecessor later earlier))
      (step-evidence : (step parameter source next))
      (recursive-evidence :
        (rel:iterate-with environment value counter step predecessor zero
          parameter next earlier target))
      (rel:iterate-with environment value counter step predecessor zero
        parameter source later target)))

(: rel:list:repeat
  (-> (element : (u $element-level))
      (counter : (u $counter-level))
      (predecessor :
        (-> (later : counter) (earlier : counter)
            (u $evidence-level)))
      (zero : counter)
      (item : element)
      (before : (list element))
      (count : counter)
      (after : (list element))
      (u $evidence-level)))

(: rel:list:repeat:zero
  (-> (element : (u $element-level))
      (counter : (u $counter-level))
      (predecessor :
        (-> (later : counter) (earlier : counter)
            (u $evidence-level)))
      (zero : counter)
      (item : element)
      (before : (list element))
      (rel:list:repeat element counter predecessor zero
        item before zero before)))

(: rel:list:repeat:step
  (-> (element : (u $element-level))
      (counter : (u $counter-level))
      (predecessor :
        (-> (later : counter) (earlier : counter)
            (u $evidence-level)))
      (zero : counter)
      (item : element)
      (before : (list element))
      (next : (list element))
      (later : counter)
      (earlier : counter)
      (after : (list element))
      (predecessor-evidence : (predecessor later earlier))
      (append-evidence : (rel:list:snoc element before item next))
      (recursive-evidence :
        (rel:list:repeat element counter predecessor zero
          item next earlier after))
      (rel:list:repeat element counter predecessor zero
        item before later after)))

(: rel:unfold
  (-> (state : (u $state-level))
      (counter : (u $counter-level))
      (output : (u $output-level))
      (advance :
        (-> (later : counter) (earlier : counter)
            (u $evidence-level)))
      (emit :
        (-> (current : state) (head : output) (next : state)
            (u $evidence-level)))
      (zero : counter)
      (current : state)
      (count : counter)
      (outputs : (list output))
      (u $evidence-level)))

(: rel:unfold:zero
  (-> (state : (u $state-level))
      (counter : (u $counter-level))
      (output : (u $output-level))
      (advance :
        (-> (later : counter) (earlier : counter)
            (u $evidence-level)))
      (emit :
        (-> (current : state) (head : output) (next : state)
            (u $evidence-level)))
      (zero : counter)
      (current : state)
      (rel:unfold state counter output advance emit zero
        current zero (list:nil output))))

(: rel:unfold:step
  (-> (state : (u $state-level))
      (counter : (u $counter-level))
      (output : (u $output-level))
      (advance :
        (-> (later : counter) (earlier : counter)
            (u $evidence-level)))
      (emit :
        (-> (current : state) (head : output) (next : state)
            (u $evidence-level)))
      (zero : counter)
      (current : state)
      (head : output)
      (next : state)
      (later : counter)
      (earlier : counter)
      (tail : (list output))
      (advance-evidence : (advance later earlier))
      (emit-evidence : (emit current head next))
      (recursive-evidence :
        (rel:unfold state counter output advance emit zero
          next earlier tail))
      (rel:unfold state counter output advance emit zero
        current later (list:cons output head tail))))

(: rel:either
  (-> (value : (u $value-level))
      (left : (-> (subject : value) (u $evidence-level)))
      (right : (-> (subject : value) (u $evidence-level)))
      (subject : value)
      (u $evidence-level)))

(: rel:either:left
  (-> (value : (u $value-level))
      (left : (-> (subject : value) (u $evidence-level)))
      (right : (-> (subject : value) (u $evidence-level)))
      (subject : value)
      (evidence : (left subject))
      (rel:either value left right subject)))

(: rel:either:right
  (-> (value : (u $value-level))
      (left : (-> (subject : value) (u $evidence-level)))
      (right : (-> (subject : value) (u $evidence-level)))
      (subject : value)
      (evidence : (right subject))
      (rel:either value left right subject)))

(: rel:all
  (-> (element : (u $element-level))
      (predicate :
        (-> (value : element) (u $evidence-level)))
      (values : (list element))
      (u $evidence-level)))

(: rel:all:nil
  (-> (element : (u $element-level))
      (predicate :
        (-> (value : element) (u $evidence-level)))
      (rel:all element predicate (list:nil element))))

(: rel:all:cons
  (-> (element : (u $element-level))
      (predicate :
        (-> (value : element) (u $evidence-level)))
      (head : element)
      (tail : (list element))
      (head-evidence : (predicate head))
      (tail-evidence : (rel:all element predicate tail))
      (rel:all element predicate
        (list:cons element head tail))))

(: rel:any
  (-> (element : (u $element-level))
      (target : (u $target-level))
      (predicate :
        (-> (remaining : (list element)) (answer : target)
            (u $evidence-level)))
      (values : (list element))
      (answer : target)
      (u $evidence-level)))

(: rel:any:here
  (-> (element : (u $element-level))
      (target : (u $target-level))
      (predicate :
        (-> (remaining : (list element)) (answer : target)
            (u $evidence-level)))
      (values : (list element))
      (answer : target)
      (evidence : (predicate values answer))
      (rel:any element target predicate values answer)))

(: rel:any:there
  (-> (element : (u $element-level))
      (target : (u $target-level))
      (predicate :
        (-> (remaining : (list element)) (answer : target)
            (u $evidence-level)))
      (head : element)
      (tail : (list element))
      (answer : target)
      (evidence : (rel:any element target predicate tail answer))
      (rel:any element target predicate
        (list:cons element head tail) answer)))

(: rel:case-list
  (-> (element : (u $element-level))
      (target : (u $target-level))
      (nil-case :
        (-> (answer : target) (u $evidence-level)))
      (cons-case :
        (-> (head : element) (tail : (list element))
            (answer : target) (u $evidence-level)))
      (values : (list element))
      (answer : target)
      (u $evidence-level)))

(: rel:case-list:nil
  (-> (element : (u $element-level))
      (target : (u $target-level))
      (nil-case :
        (-> (answer : target) (u $evidence-level)))
      (cons-case :
        (-> (head : element) (tail : (list element))
            (answer : target) (u $evidence-level)))
      (answer : target)
      (evidence : (nil-case answer))
      (rel:case-list element target nil-case cons-case
        (list:nil element) answer)))

(: rel:case-list:cons
  (-> (element : (u $element-level))
      (target : (u $target-level))
      (nil-case :
        (-> (answer : target) (u $evidence-level)))
      (cons-case :
        (-> (head : element) (tail : (list element))
            (answer : target) (u $evidence-level)))
      (head : element)
      (tail : (list element))
      (answer : target)
      (evidence : (cons-case head tail answer))
      (rel:case-list element target nil-case cons-case
        (list:cons element head tail) answer)))

(: rel:fold
  (-> (element : (u $element-level))
      (accumulator : (u $accumulator-level))
      (step :
        (-> (before : accumulator) (head : element)
            (after : accumulator) (u $evidence-level)))
      (before : accumulator)
      (values : (list element))
      (after : accumulator)
      (u $evidence-level)))

(: rel:fold:nil
  (-> (element : (u $element-level))
      (accumulator : (u $accumulator-level))
      (step :
        (-> (before : accumulator) (head : element)
            (after : accumulator) (u $evidence-level)))
      (before : accumulator)
      (rel:fold element accumulator step before
        (list:nil element) before)))

(: rel:fold:cons
  (-> (element : (u $element-level))
      (accumulator : (u $accumulator-level))
      (step :
        (-> (before : accumulator) (head : element)
            (after : accumulator) (u $evidence-level)))
      (before : accumulator)
      (head : element)
      (tail : (list element))
      (next : accumulator)
      (after : accumulator)
      (step-evidence : (step before head next))
      (tail-evidence :
        (rel:fold element accumulator step next tail after))
      (rel:fold element accumulator step before
        (list:cons element head tail) after)))
