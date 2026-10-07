(set:profile hol)
(: X (u 0))
(: x X)

; Receipts retain the declarations and admitted conversion rules selected by
; the actual checker. Source binders contribute no unrelated global authority.

; independent: Logical introduction uses no external object declaration.
!(set:theorem independent (all prop (lam p (imp p p))) (pf:all-intro (pf:imp-intro (pf:hyp 0))))

; free_witness: The free witness x is declared in X.
!(set:theorem inhabX (all prop (lam r (imp (all X (lam z r)) r))) (pf:all-intro (pf:imp-intro (pf:all-elim (pf:hyp 0) x))))

; footprint: The actual declaration class contains X and x.
!(match &self (set:known inhabX $p $o $f $d $r $s) $d)

; bound_shadow: The bound x supplies the witness without the unrelated global x.
!(set:theorem bound-use (all X (lam x (all prop (lam r (imp (all X (lam z r)) r))))) (pf:fix x (pf:fix r (pf:assume h (pf:all-elim h x)))))

; bound_footprint: Only the carrier declaration remains external.
!(match &self (set:known bound-use $p $o $f $d $r $s) $d)

; nested: The nested proof retains the original theorem citation.
!(set:theorem nested (all prop (lam r (imp (all X (lam z r)) r))) (pf:known inhabX))

; native_alias: The native theorem keeps its own declaration context.
!(type:theorem xi X x)

; native_alias_use: A selected native theorem has current original witness authority.
!(set:theorem via-native (all prop (lam r (imp (all X (lam z r)) r))) (pf:all-intro (pf:imp-intro (pf:all-elim (pf:hyp 0) xi))))

; definition: A checked definition retains actual external witness dependencies.
!(set:define pick X (= pick x))

; definition_use: A definition witness is justified by its constructed authority.
!(set:theorem via-definition (all prop (lam r (imp (all X (lam z r)) r))) (pf:all-intro (pf:imp-intro (pf:all-elim (pf:hyp 0) pick))))

; save_native: Save an actually checked native package before withdrawal.
!(let $package (set:native-proof nested) (add-atom &self (saved-native $package)))

; native_current: The original native proof is usable before withdrawal.
!(car-atom (try (set:native-proof nested)))

; strip_witness: Imported record keeps the carrier pin but deliberately omits the witness.
!(match &self (set:known inhabX $p $o $f $d $r $s) (add-atom &self (set:known stripped $p $o $f (((: X (u 0)) (: X (u 0)))) $r $s)))

; strip_refused: Actual rechecking detects the omitted witness dependency.
!(try (set:recheck stripped))

; strip_not_admitted: An incomplete footprint is not admitted by the ordinary recheck.
!(set:recheck stripped)

; wrong_pin: A declaration-shaped name must pin exactly the same declaration.
!(match &self (set:known inhabX $p $o $f $d $r $s) (add-atom &self (set:known wrong-pin $p $o $f (((: x X) (: x prop))) $r $s)))

; wrong_pin_refused: A different payload is not accepted as the declaration pin.
!(try (set:recheck wrong-pin))

; remove_witness: Withdraw only the inhabitant declaration.
!(remove-atom &self (: x X))

; withdraw_direct: Reusing a dependent proof requires the missing witness.
!(car-atom (try (set:proves (pf:known inhabX) (all prop (lam r (imp (all X (lam z r)) r))))))

; withdraw_nested: Reusing a dependent proof requires the missing witness.
!(car-atom (try (set:proves (pf:known nested) (all prop (lam r (imp (all X (lam z r)) r))))))

; withdraw_definition: Reusing a dependent proof requires the missing witness.
!(car-atom (try (set:proves (pf:known via-definition) (all prop (lam r (imp (all X (lam z r)) r))))))

; withdraw_native_alias: The raw xi declaration does not override its unavailable native proof context.
!(car-atom (try (set:proves (pf:known via-native) (all prop (lam r (imp (all X (lam z r)) r))))))

; withdraw_recheck: Recheck names the exact absent declaration.
!(try (set:recheck inhabX))

; withdraw_native: Native compilation checks current theorem dependencies.
!(car-atom (try (set:native-proof nested)))

; saved_native-use: Saved packages remain subject to their original current support.
!(match &self (saved-native $package) (let (SetNativeProofV1 $n $p $f $t $ty $c $r $rows $d) $package (car-atom (try (set:native-use $package (Lam $ty (idx 0)))))))

; saved_native-normalize: Saved packages remain subject to their original current support.
!(match &self (saved-native $package) (let (SetNativeProofV1 $n $p $f $t $ty $c $r $rows $d) $package (car-atom (try (set:native-normalize $package (Lam $ty (idx 0)))))))

; independent_survives: Unrelated logical proof stays usable.
!(car-atom (try (set:proves (pf:known independent) (all prop (lam p (imp p p))))))

; bound_survives: The local witness does not depend on the removed global x.
!(car-atom (try (set:proves (pf:known bound-use) (all X (lam x (all prop (lam r (imp (all X (lam z r)) r))))))))

; stale_global_shadow: A bound pick does not consult the now-stale global definition pick.
!(set:theorem local-pick (all X (lam pick (all prop (lam r (imp (all X (lam z r)) r))))) (pf:fix pick (pf:fix r (pf:assume h (pf:all-elim h pick)))))

; stale_global_shadow_footprint: The global definition record is absent from the local proof footprint.
!(match &self (set:known local-pick $p $o $f $d $r $s) $d)

; raw_tag_same_name: Plant an unadmitted construction-shaped record sharing only the bound name.
!(match &self (set:known inhabX $p $o $f $d $r $s) (add-atom &self (set:known x (inductive (u 0) (: xx x)) inductive raw () $r $s)))

; raw_tag_shadow: Unselected global construction metadata has no effect on a bound variable.
!(set:theorem bound-tag-shadow (all X (lam x (all prop (lam r (imp (all X (lam z r)) r))))) (pf:fix x (pf:fix r (pf:assume h (pf:all-elim h x)))))

; remove_raw_tag: Remove the planted untrusted record to isolate later declaration controls.
!(match &self (set:known x $p $o $f $d $r $s) (remove-atom &self (set:known x $p $o $f $d $r $s)))

; restore_witness: Restore exactly the declaration that the original proof used.
!(add-atom &self (: x X))

; restore_inhabX: Exact restoration revalidates the retained support without a global generation cutoff.
!(car-atom (try (set:proves (pf:known inhabX) (all prop (lam r (imp (all X (lam z r)) r))))))

; restore_nested: Exact restoration revalidates the retained support without a global generation cutoff.
!(car-atom (try (set:proves (pf:known nested) (all prop (lam r (imp (all X (lam z r)) r))))))

; restore_via-native: Exact restoration revalidates the retained support without a global generation cutoff.
!(car-atom (try (set:proves (pf:known via-native) (all prop (lam r (imp (all X (lam z r)) r))))))

; restore_via-definition: Exact restoration revalidates the retained support without a global generation cutoff.
!(car-atom (try (set:proves (pf:known via-definition) (all prop (lam r (imp (all X (lam z r)) r))))))

; carrier_withdraw: Keep witness declarations while withdrawing their carrier.
!(remove-atom &self (: X (u 0)))

; carrier_refused: The carrier type is a genuine transitive dependency.
!(try (set:recheck inhabX))

; carrier_independent: Pure logical proof does not depend on this carrier.
!(car-atom (try (set:proves (pf:known independent) (all prop (lam p (imp p p))))))

; carrier_restore: Restore the exact carrier declaration.
!(add-atom &self (: X (u 0)))

; add_Y: Provide a distinct result carrier.
!(add-atom &self (: Y (u 0)))

; add_g: The witness function type reaches X transitively.
!(add-atom &self (: g (-> X Y)))

; transitive_witness: The final proposition mentions Y, while the used witness also requires X.
!(set:theorem inhabY (all prop (lam r (imp (all Y (lam z r)) r))) (pf:all-intro (pf:imp-intro (pf:all-elim (pf:hyp 0) (g x)))))

; remove_transitive_X: Remove the hidden argument carrier, keeping Y and g declarations.
!(remove-atom &self (: X (u 0)))

; transitive_refused: The final proposition still forms, but its retained witness support is unavailable.
!(car-atom (try (set:proves (pf:known inhabY) (all prop (lam r (imp (all Y (lam z r)) r))))))

; restore_transitive_X: Restore the hidden type declaration.
!(add-atom &self (: X (u 0)))

; transitive_restored: The same proof becomes reusable under the exact restored declaration.
!(car-atom (try (set:proves (pf:known inhabY) (all prop (lam r (imp (all Y (lam z r)) r))))))

; add_P: Provide a proposition for the conversion rule control.
!(add-atom &self (: P prop))

; rule_definition: Create an admitted computation rule through checked definition.
!(set:define truth prop (= truth (imp P P)))

; rule_proof: The authored truth constant reduces by its actual admitted rule.
!(set:theorem by-conversion truth (pf:imp-intro (pf:hyp 0)))

; rule_withdraw: Withdraw only the actual computation rule; retain the declaration and definition record.
!(match &self (type:rule truth $n $pats $rhs) (remove-atom &self (type:rule truth $n $pats $rhs)))

; rule_refused: A constructed record cannot replace its withdrawn selected kernel rule.
!(car-atom (try (set:proves (pf:known by-conversion) truth)))

; rule_independent: Unrelated proofs retain their exact smaller environment.
!(car-atom (try (set:proves (pf:known independent) (all prop (lam p (imp p p))))))


; stored_recursor_type: The positive datatype creates its genuine recursor and schema.
!(set:inductive ReceiptNat (u 0) (: receipt-zero ReceiptNat) (: receipt-succ (-> ReceiptNat ReceiptNat)))

; stored_recursor_definition: The actual kernel checks the recursor application before publication.
!(set:define receipt-rec (-> ReceiptNat ReceiptNat) (= (receipt-rec $n) (ReceiptNat-rec (lam k ReceiptNat) receipt-zero (lam k (lam r (receipt-succ r))) $n)))

; stored_definition_current: Publication admission refers to the stored template, despite fresh schema frames.
!(car-atom (try (set:recheck receipt-rec)))

; stored_definition_consumer: A real proof consumes the independently checked recursive definition.
!(set:theorem receipt-witness (all prop (lam p (imp (all ReceiptNat (lam z p)) p))) (pf:all-intro (pf:imp-intro (pf:all-elim (pf:hyp 0) (receipt-rec receipt-zero)))))

; stored_consumer_current: Embedding a definition template in another receipt preserves its complete variable relationships.
!(car-atom (try (set:recheck receipt-witness)))

; stored_consumer_native: The same current support permits native proof compilation.
!(car-atom (try (set:native-proof receipt-witness)))

; renamed-definition-pin_insert: An imported theorem may record an extra dependency; it must still be current.
!(match &self (set:known independent $p $o $f $d $r $s) (add-atom &self (set:known renamed-definition-pin $p $o $f ((receipt-rec (define (-> ReceiptNat ReceiptNat) (= (receipt-rec $first) (ReceiptNat-rec (lam k ReceiptNat) receipt-zero (lam k (lam r (receipt-succ r))) $first))))) $r $s)))

; renamed-definition-pin_check: A bijective variable rename preserves the entire definition template.
!(car-atom (try (set:recheck renamed-definition-pin)))

; split-definition-pin_insert: An imported theorem may record an extra dependency; it must still be current.
!(match &self (set:known independent $p $o $f $d $r $s) (add-atom &self (set:known split-definition-pin $p $o $f ((receipt-rec (define (-> ReceiptNat ReceiptNat) (= (receipt-rec $first) (ReceiptNat-rec (lam k ReceiptNat) receipt-zero (lam k (lam r (receipt-succ r))) $second))))) $r $s)))

; split-definition-pin_check: Splitting the repeated equation variable changes the template and is rejected.
!(car-atom (try (set:recheck split-definition-pin)))

; schema_declaration: The declaration repeats the same universe schema variable at both occurrences.
!(add-atom &self (: schema-f (-> (u $same) (u $same))))

; schema_pin: A full schema pin retains the repeated-variable relationship.
!(match &self (set:known independent $p $o $f $d $r $s) (add-atom &self (set:known schema-pinned $p $o $f (((: schema-f (-> (u $same) (u $same))) (: schema-f (-> (u $same) (u $same))))) $r $s)))

; schema_initial: The unchanged well-formed declaration satisfies its extra pin.
!(car-atom (try (set:recheck schema-pinned)))

; schema_remove: Remove the original schema before replacing it.
!(match &self (: schema-f $type) (remove-atom &self (: schema-f $type)))

; schema_rename: Change only the bound schema variable spelling.
!(add-atom &self (: schema-f (-> (u $renamed) (u $renamed))))

; schema_rename_current: A bijective rename is the same declaration schema.
!(car-atom (try (set:recheck schema-pinned)))

; schema_remove_renamed: Remove the renamed declaration before changing the relationship.
!(match &self (: schema-f $type) (remove-atom &self (: schema-f $type)))

; schema_split: Use independent variables where the pinned declaration used one repeated variable.
!(add-atom &self (: schema-f (-> (u $left) (u $right))))

; schema_split_refused: A non-bijective split is not a current instance of the original declaration schema.
!(car-atom (try (set:recheck schema-pinned)))

; schema_split_independent: Unrelated proofs do not acquire the extra schema pin.
!(car-atom (try (set:proves (pf:known independent) (all prop (lam p (imp p p))))))
