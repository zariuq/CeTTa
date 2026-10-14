(hol:profile core)
(hol:rule hol-equality)
; Ordinary dependent typing reads its explicit declaration view.
(: prop (u 0))
(: Falsum prop)

; Each definition is checked against the preceding inventory.
!(hol:define HistoryCarrier (u 0) (= HistoryCarrier prop))
!(hol:define HistoryAlias (u 0) (= HistoryAlias HistoryCarrier))
!(hol:define HistoryFunction (u 0) (= HistoryFunction (-> HistoryAlias HistoryAlias)))
!(hol:define historyId HistoryFunction (= (historyId $p) $p))
!(car-atom (try (hol:check (historyId Falsum) HistoryAlias)))
!(car-atom (try (hol:eq (historyId Falsum) Falsum)))

; Even a computationally equivalent replacement changes recorded authority.
!(car-atom (try (hol:recheck historyId)))
!(match &self (hol:known HistoryFunction $p $o $proof $deps $r $s)
    (remove-atom &self (hol:known HistoryFunction $p $o $proof $deps $r $s)))
!(match &self (type:rule HistoryFunction $n $patterns $rhs)
    (remove-atom &self (type:rule HistoryFunction $n $patterns $rhs)))
!(remove-atom &self (: HistoryFunction (u 0)))
!(hol:define HistoryFunction (u 0) (= HistoryFunction (-> prop prop)))
!(car-atom (try (hol:recheck historyId)))
!(match &self (hol:known HistoryFunction $p $o $proof $deps $r $s)
    (remove-atom &self (hol:known HistoryFunction $p $o $proof $deps $r $s)))
!(match &self (type:rule HistoryFunction $n $patterns $rhs)
    (remove-atom &self (type:rule HistoryFunction $n $patterns $rhs)))
!(remove-atom &self (: HistoryFunction (u 0)))
!(hol:define HistoryFunction (u 0) (= HistoryFunction (-> HistoryAlias HistoryAlias)))
!(car-atom (try (hol:recheck historyId)))
!(car-atom (try (hol:check historyId (-> prop prop))))

; The codomain depends on the argument; a carrier alias is not a value.
!(hol:define historyRefl (-> (p : HistoryAlias) (id HistoryAlias p p))
   (= (historyRefl $p) (refl $p)))
!(type:check (historyRefl Falsum) (id prop Falsum Falsum))
!(car-atom (try (hol:check (historyId HistoryFunction) HistoryAlias)))

; A retained logical proof can refer to the complete alias chain.
!(hol:theorem history-reflexivity
   (all HistoryAlias (lam p (eq HistoryAlias p p)))
   (pf:fix p (pf:eq-refl HistoryAlias p)))
!(car-atom (try (hol:proves (pf:known history-reflexivity)
   (all prop (lam p (eq prop p p))))))
!(car-atom (try (hol:native-proof history-reflexivity)))

; Replacement, self-reference and a missing predecessor are not admissions.
!(car-atom (try (hol:define HistoryCarrier (u 0) (= HistoryCarrier (-> prop prop)))))
!(car-atom (try (hol:define historySelf prop (= historySelf historySelf))))
!(== (collapse (match &self (: historySelf $type) $type){$type}) ())
!(car-atom (try (hol:define historyForward (u 0) (= historyForward HistoryLater))))
!(== (collapse (match &self (: historyForward $type) $type){$type}) ())
!(hol:define HistoryLater (u 0) (= HistoryLater prop))
!(hol:define historyForward (u 0) (= historyForward HistoryLater))
!(car-atom (try (hol:check Falsum historyForward)))

; An unrelated fresh definition leaves old proofs and conversions intact.
!(hol:define unrelatedHistory prop (= unrelatedHistory (imp Falsum Falsum)))
!(car-atom (try (hol:proves (pf:known history-reflexivity)
   (all prop (lam p (eq prop p p))))))
!(car-atom (try (hol:eq (historyId Falsum) Falsum)))
