;; Named state is not transactional: a name written inside a transaction keeps
;; its value whether the transaction commits or fails, as SWI-PeTTa's
;; nb_setval does. Names created before and inside a transaction, literal and
;; computed keys (computed by atom_concat, which SWI implements, and by sread,
;; which CeTTa implements), commit and failure, and nested transactions.
(= (foreign-key $n) (atom_concat txn_foreign_ $n))
(= (native-key $n) (sread (format-args "txn_native_{}" ($n))))
(= (read $k)
   (case (catch (get-state $k))
     (((Error (existence_error $kind $name) $context) unbound)
      ($value $value))))
;; The transaction's own answers, then the value read after it.
!(before-literal-commit
   (collapse (transaction (change-state! txn_literal_1 2)))
   (read txn_literal_1))
!(change-state! (foreign-key 1) 1)
!(before-foreign-commit
   (collapse (transaction (let $k (foreign-key 1) (change-state! $k 2))))
   (read (foreign-key 1)))
!(change-state! (native-key 1) 1)
!(before-native-commit
   (collapse (transaction (let $k (native-key 1) (change-state! $k 2))))
   (read (native-key 1)))
!(inside-literal-commit
   (collapse (transaction (change-state! txn_literal_2 3)))
   (read txn_literal_2))
!(inside-foreign-commit
   (collapse (transaction (let $k (foreign-key 2) (change-state! $k 3))))
   (read (foreign-key 2)))
!(inside-native-commit
   (collapse (transaction (let $k (native-key 2) (change-state! $k 3))))
   (read (native-key 2)))
!(inside-literal-failure
   (collapse (transaction (progn (change-state! txn_literal_3 4) (empty))))
   (read txn_literal_3))
!(inside-foreign-failure
   (collapse (transaction
     (let $k (foreign-key 3) (progn (change-state! $k 4) (empty)))))
   (read (foreign-key 3)))
!(inside-native-failure
   (collapse (transaction
     (let $k (native-key 3) (progn (change-state! $k 4) (empty)))))
   (read (native-key 3)))
!(nested-foreign-commit
   (collapse (transaction (transaction
     (let $k (foreign-key 4) (change-state! $k 5)))))
   (read (foreign-key 4)))
!(nested-native-inner-failure
   (collapse (transaction (collapse (transaction
     (let $k (native-key 5) (progn (change-state! $k 6) (empty)))))))
   (read (native-key 5)))
!(nested-native-outer-failure
   (collapse (transaction (progn
     (transaction (let $k (native-key 6) (change-state! $k 7)))
     (empty))))
   (read (native-key 6)))
;; A name computed before the transaction is a symbol inside it.
!(let $k (foreign-key 7)
   (computed-before-inside-commit
     (collapse (transaction (change-state! $k 8)))
     (read $k)))
;; The reported shape.
!(transaction (let $k (atom_concat computed_txn_ 42) (change-state! $k 9)))
!(reported (read (atom_concat computed_txn_ 42)))
