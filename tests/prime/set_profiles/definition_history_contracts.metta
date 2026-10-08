(set:profile hol)
(set:rule hol-equality)
; Ordinary dependent typing reads its explicit declaration view.
(: prop (u 0))
(: Falsum prop)

; Each definition is checked against the preceding inventory.
!(set:define HistoryCarrier (u 0) (= HistoryCarrier prop))
!(set:define HistoryAlias (u 0) (= HistoryAlias HistoryCarrier))
!(set:define HistoryFunction (u 0) (= HistoryFunction (-> HistoryAlias HistoryAlias)))
!(set:define historyId HistoryFunction (= (historyId $p) $p))
!(car-atom (try (set:check (historyId Falsum) HistoryAlias)))
!(car-atom (try (set:eq (historyId Falsum) Falsum)))

; Even a computationally equivalent replacement changes recorded authority.
!(car-atom (try (set:recheck historyId)))
!(match &self (set:known HistoryFunction $p $o $proof $deps $r $s)
    (remove-atom &self (set:known HistoryFunction $p $o $proof $deps $r $s)))
!(match &self (type:rule HistoryFunction $n $patterns $rhs)
    (remove-atom &self (type:rule HistoryFunction $n $patterns $rhs)))
!(remove-atom &self (: HistoryFunction (u 0)))
!(set:define HistoryFunction (u 0) (= HistoryFunction (-> prop prop)))
!(car-atom (try (set:recheck historyId)))
!(match &self (set:known HistoryFunction $p $o $proof $deps $r $s)
    (remove-atom &self (set:known HistoryFunction $p $o $proof $deps $r $s)))
!(match &self (type:rule HistoryFunction $n $patterns $rhs)
    (remove-atom &self (type:rule HistoryFunction $n $patterns $rhs)))
!(remove-atom &self (: HistoryFunction (u 0)))
!(set:define HistoryFunction (u 0) (= HistoryFunction (-> HistoryAlias HistoryAlias)))
!(car-atom (try (set:recheck historyId)))
!(car-atom (try (set:check historyId (-> prop prop))))

; The codomain depends on the argument; a carrier alias is not a value.
!(set:define historyRefl (-> (p : HistoryAlias) (id HistoryAlias p p))
   (= (historyRefl $p) (refl $p)))
!(type:check (historyRefl Falsum) (id prop Falsum Falsum))
!(car-atom (try (set:check (historyId HistoryFunction) HistoryAlias)))

; A retained logical proof can refer to the complete alias chain.
!(set:theorem history-reflexivity
   (all HistoryAlias (lam p (eq HistoryAlias p p)))
   (pf:fix p (pf:eq-refl HistoryAlias p)))
!(car-atom (try (set:proves (pf:known history-reflexivity)
   (all prop (lam p (eq prop p p))))))
!(car-atom (try (set:native-proof history-reflexivity)))

; Replacement, self-reference and a missing predecessor are not admissions.
!(car-atom (try (set:define HistoryCarrier (u 0) (= HistoryCarrier (-> prop prop)))))
!(car-atom (try (set:define historySelf prop (= historySelf historySelf))))
!(== (collapse (match &self (: historySelf $type) $type){$type}) ())
!(car-atom (try (set:define historyForward (u 0) (= historyForward HistoryLater))))
!(== (collapse (match &self (: historyForward $type) $type){$type}) ())
!(set:define HistoryLater (u 0) (= HistoryLater prop))
!(set:define historyForward (u 0) (= historyForward HistoryLater))
!(car-atom (try (set:check Falsum historyForward)))

; An unrelated fresh definition leaves old proofs and conversions intact.
!(set:define unrelatedHistory prop (= unrelatedHistory (imp Falsum Falsum)))
!(car-atom (try (set:proves (pf:known history-reflexivity)
   (all prop (lam p (eq prop p p))))))
!(car-atom (try (set:eq (historyId Falsum) Falsum)))
