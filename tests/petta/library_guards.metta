; The libraries' checks guard a comparison or an arithmetic step with `if`.
; `and` and `or` evaluate both of their operands, so a guard written as their
; first operand did not keep a value it rejects (a symbol where a number was
; expected) from the comparison, which raised a type error instead of the
; check's own answer.  Each check: a value its guard rejects, then one it
; admits.
!(import! &self (library lib_binary_schema))
!(import! &self (library lib_fair_queue))
!(import! &self (library lib_ic_context_library))
!(import! &self (library lib_ic_library))
!(import! &self (library lib_ic_library_stack))
!(import! &self (library lib_ic_library_transfer))
!(import! &self (library lib_ic_mil_source))
!(import! &self (library lib_ic_search))
!(import! &self (library lib_ic_clustering))
!(import! &self (library lib_scoped_templates))
!(import! &self (library lib_source_scope))
!(import! &self (library lib_source_rule_heads))
!(import! &self (library lib_tensor_program_compose))
!(import! &self (library lib_random_closure))
!(import! &self (library lib_evidence_ledger))

; A schema row's tag is a positive natural.
!(binary-schema:shape? ((Binary:Form tag C ())))
!(binary-schema:shape? ((Binary:Form 1 C ())))

; A fair queue's phase, next ticket and entry ids are naturals.
!(fair-queue:entries? ((Fair:Entry id 1 v)) -1 5)
!(fair-queue:entries? ((Fair:Entry 0 1 v)) -1 5)
!(fair-queue:valid? (Fair:Queue phase 1 ()))
!(fair-queue:valid? (Fair:Queue 0 next ((Fair:Entry 0 1 v))))
!(fair-queue:valid? (Fair:Queue 0 1 ((Fair:Entry 0 1 v))))

; A variable's index is a natural below the ports.
!(context-library:value-image? 2 (Syntax:Variable index))
!(context-library:value-image? 2 (Syntax:Variable 1))
!(source-library:call-arguments? 2 ((Syntax:Variable index)))
!(source-library:call-arguments? 2 ((Syntax:Variable 1)))
!(library-transfer:port-at 2 role index Delayed)
!(library-transfer:port-at 2 role 1 Delayed)
!(mil-source:term? () () 2 (Syntax:Variable index))
!(mil-source:term? () () 2 (Syntax:Expr ((Syntax:Variable index))))
!(mil-source:term? () () 2 (Syntax:Variable 1))
!(source-scope:fresh? () 2 (Syntax:Variable index))
!(source-scope:fresh? () 2 (Syntax:Variable 1))
!(source-scope:valid? () parameters 2 (Syntax:Expr ()))
!(source-scope:valid? () 0 count (Syntax:Expr ()))
!(scoped:spine (a b) (Wire:Pair (Wire:Pair (Wire:Atom 0) (Wire:Atom index)) (Wire:Atom 0)) ())
!(scoped:spine (a b) (Wire:Pair (Wire:Pair (Wire:Atom 0) (Wire:Atom 1)) (Wire:Atom 0)) ())
!(tp-compose:reverse-at (a b c) index)
!(tp-compose:reverse-at (a b c) 0)

; A library stack's seeds are pairs of naturals below the count.
!(library-stack:seeds? ((left 0)) 2)
!(library-stack:seeds? ((0 right)) 2)
!(library-stack:seeds? ((0 1)) 2)

; Search statistics, quantum and budget are naturals.
!(ic-search:statistics? (IC:Statistics 0 candidates 0 0))
!(ic-search:statistics? (IC:Statistics 0 1 accepted 0))
!(ic-search:statistics? (IC:Statistics 0 1 0 0))
!(ic:prepare Model Original () quantum ())
!(ic:resume (IC:SearchState (IC:Task Model Original Observation () quantum) Queue Best Statistics First) 5)

; Cluster indices, counts and farthest points are naturals.
!(tp-cluster:indices? (index) 3)
!(tp-cluster:indices? (1 2) 3)
!(tp-cluster:counts? (count) 3)
!(tp-cluster:counts? (1 2) 3)
!(tp-cluster:farthest? (TP:Farthest index 0.5) 2 ())
!(tp-cluster:farthest? (TP:Farthest 1 0.5) 2 ())

; A rule head's names are a list of plain names, as many as its ports.
!(rule-heads:clause () (Syntax:Form names (Syntax:Expr ((Syntax:Literal Symbol (binary:utf8 "=")) (Syntax:Expr ((Syntax:Literal Symbol (binary:utf8 "f")))) (Syntax:Expr ())))))
!(case (rule-heads:clause () (Syntax:Form () (Syntax:Expr ((Syntax:Literal Symbol (binary:utf8 "=")) (Syntax:Expr ((Syntax:Literal Symbol (binary:utf8 "f")))) (Syntax:Expr ())))))
   (((IC:Some (Ctx:Rule $head $body $bound $ports)) (Rule $body $bound $ports)) ($other $other)))

; A random closure's component index is an integer in range.
!(let* (($p (rc:prepare (RC:Program (a b) () ((RC:Component c () () ((RC:Rule b (a)))))) ((RC:Query () b))))
        ($e (rc:engine-new $p ((RC:LocalState () ())))))
   (rc:engine-replace-component $e index (RC:Component c () () ()) (RC:LocalState () ())))
!(let* (($p (rc:prepare (RC:Program (a b) () ((RC:Component c () () ((RC:Rule b (a)))))) ((RC:Query () b))))
        ($e (rc:engine-new $p ((RC:LocalState () ())))))
   (rc:engine-replace-component $e 5 (RC:Component c () () ()) (RC:LocalState () ())))
!(let* (($p (rc:prepare (RC:Program (a b) () ((RC:Component c () () ((RC:Rule b (a)))))) ((RC:Query () b))))
        ($e (rc:engine-new $p ((RC:LocalState () ())))))
   (rc:engine-update $e ((index (RC:LocalState () ()))) keep))
!(let* (($p (rc:prepare (RC:Program (a b) () ((RC:Component c () () ((RC:Rule b (a)))))) ((RC:Query () b))))
        ($e (rc:engine-new $p ((RC:LocalState () ())))))
   (rc:engine-update $e ((5 (RC:LocalState () ()))) keep))
!(let* (($prog (RC:Program (alarm) () ((RC:Component sensor (fault) () ((RC:Rule alarm (fault)))))))
        ($b (ledger:bind-engine (ledger:from-prepared (rc:prepare $prog ((RC:Query () alarm))) (((Rat 1 2))) ()))))
   (ledger:replace-component $b index (RC:Component sensor (fault) () ()) ((Rat 1 2))))
!(let* (($prog (RC:Program (alarm) () ((RC:Component sensor (fault) () ((RC:Rule alarm (fault)))))))
        ($b (ledger:bind-engine (ledger:from-prepared (rc:prepare $prog ((RC:Query () alarm))) (((Rat 1 2))) ()))))
   (ledger:replace-component $b 5 (RC:Component sensor (fault) () ()) ((Rat 1 2))))
