; A head that gains a meaning while a query runs is called by the next
; occurrence that reaches it: an equation that add-atom installs, and a
; builtin that an import enables, in code translated before and in the
; running query's own code alike.  A value the query bound before the change
; stays the value it was.  Before the import, work-new is an unknown name, so
; its arguments are evaluated and the call stays data.
(= (old-data) (late 5))
(= (late-take) (let $after (work-new (+ 1 1)) (work-take $after 1)))
!(Installed (let $u (add-atom &self (= (late $n) (+ $n 1))) (old-data)))
!(Direct (let $u (add-atom &self (= (t3 $x) v)) (t3 1)))
!(Later (t3 1))
!(Imported
   (let $before (work-new (+ 1 1))
     (let $u (import! &self (library work))
       ($before (late-take) (work-take (work-new (+ 1 1)) 1)))))
!(AfterImport (work-take (work-new (+ 2 2)) 1))
