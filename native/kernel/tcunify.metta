; One tcalc unification over tcalc's own term store: the equation a = b (at
; abase, bbase), put on the work stack by the first call (phase 2), and the
; equations it leads to are solved, or refuted, with every
; assignment written to the local or unknown table and recorded on the trail,
; as unify_c does (native/tcalc/tcalc_core.c). The store is read where it
; lies; the only work left to the host is what needs new terms: closing a
; term (CLOSE, with the term and its base in pt, ptbase; the host writes the
; closed term to closed and calls again), room in the work stack or the trail,
; and a fresh walk epoch (ROOM, with need saying which). A call that returns
; ROOM changed nothing since the equation it was solving left the stack, and
; that equation is back on it.
;
; Registers (tcstore.metta): v11 the state, v10 the term nodes, v8 the bound
; of every loop; v9 is the index of the main loop, v1..v7 are free in each step.
!(import! &self "../../vendor/tcalc-native/native/p1/p1.metta")
!(import! &self "tcstore.metta")
!(import! &self "tcunify_layout.metta")

(= (tu:get $r $f) (Load 4 TyU64 state $r v11 (* 8 (tu:f $f))))
(= (tu:put $f $r) (Store state v11 (* 8 (tu:f $f)) $r))
(= (tu:done $code) (Seq (Imm v1 (tu:r $code)) (Return FIRED v1)))

; Leaving without solving: on the main path the equation goes back on the
; stack; while a closed term is awaited nothing is left to put back.
(= (tu:exit restore $what)
   (Seq (tu:get v1 wlen) (Imm v2 4) (Add v1 v1 v2) (tu:put wlen v1)
     (Imm v1 (tu:n $what)) (tu:put need v1) (tu:done room)))
(= (tu:exit held $what) (Seq (Imm v1 (tu:n $what)) (tu:put need v1) (tu:done room)))
(= (tu:refute) (Seq (tu:get v1 floor) (tu:put wlen v1) (Imm v1 0) (tu:put phase v1) (tu:done refuted)))
(= (tu:close) (Seq (Imm v1 1) (tu:put phase v1) (Imm v1 0) (tu:put closed v1) (tu:done close)))

; The assignment of the pending variable (pindex, plocal) to the term in
; register $val, recorded on the trail as (index << 1) | is-local.
(= (tu:assign $val $mode)
   (Seq (tu:get v1 tlen) (tu:get v2 tcap) (Cmp Ult v4 v1 v2)
     (If v4 Skip (tu:exit $mode trail))
     (tu:get v2 pindex) (tu:get v4 plocal) (CmpI Ne v5 v4 0)
     (If v5 (Seq (Base v6 local) (Addr local v6 v6 v2 2 0) (Store 4 local v6 0 $val))
            (Seq (Base v6 unknown) (Addr unknown v6 v6 v2 2 0) (Store 4 unknown v6 0 $val)))
     (Shl v2 v2 1) (Or v2 v2 v4) (Base v6 trail) (Addr trail v6 v6 v1 2 0) (Store 4 trail v6 0 v2)
     (Imm v4 1) (Add v1 v1 v4) (tu:put tlen v1)))
(= (tu:assign-field $f $mode) (Seq (tu:get v3 $f) (tu:assign v3 $mode)))

; ---- Searches over a term's graph ---------------------------------------------
; Depth first, on the work stack above its top (above the popped equation on
; the main path); each node is met once per epoch (the core's walk records).

(= (tu:epoch $mode)
   (Seq (tu:get v1 epoch) (CmpI Eq v2 v1 4294967295) (If v2 (tu:exit $mode epoch))
     (Imm v2 1) (Add v1 v1 v2) (tu:put epoch v1)))
(= (tu:search-base restore) (Seq (Imm v2 4) (Add v1 v1 v2)))
(= (tu:search-base held) Skip)
(= (tu:search-start $mode $root)
   (Seq (tu:epoch $mode) (tu:get v1 wlen) (tu:search-base $mode) (tu:put dstart v1) (tu:put dtop v1)
     (tu:get v1 $root) (tu:push v1 v2 v3 $mode) (Imm TyBool v6 1)))
(= (tu:push $v $t1 $t2 $mode)
   (Seq (tu:get $t1 dtop) (tu:get $t2 wcap) (Cmp Ult $t2 $t1 $t2)
     (If $t2 Skip (tu:exit $mode work))
     (Base $t2 work) (Addr work $t2 $t2 $t1 2 0) (Store 4 work $t2 0 $v)
     (Imm $t2 1) (Add $t1 $t1 $t2) (tu:put dtop $t1)))
(= (tu:pop $s $p)
   (Seq (tu:get $s dtop) (Imm $p 1) (Sub $s $s $p) (tu:put dtop $s)
     (Base $p work) (Addr work $p $p $s 2 0) (Load 4 TyU64 work $s $p 0)))
; $body runs for $s unless it was met in this epoch.
(= (tu:unmet $s $body)
   (Seq (Base v2 seen) (Addr seen v2 v2 $s 2 0) (Load 4 TyU64 seen v3 v2 0) (tu:get v4 epoch) (Cmp Ne v7 v3 v4)
     (If v7 (Seq (Store 4 seen v2 0 v4) $body))))
(= (tu:more) (Seq (tu:get v1 dtop) (tu:get v2 dstart) (Cmp Ult v6 v2 v1)))
; The items of the expression whose node is $p.
(= (tu:push-items $p $mode)
   (Seq (Load 4 TyU64 node v3 $p 4) (Load 4 TyU64 node v4 $p 8) (tu:put dfrom v4)
     (tu:get v4 dtop) (Add v4 v4 v3) (tu:get v7 wcap) (Cmp Ule v7 v4 v7) (If v7 Skip (tu:exit $mode work))
     (Loop v4 v3 (ts:cap)
       (tu:get v7 dfrom) (Add v7 v7 v4) (Base v2 item) (Addr item v2 v2 v7 2 0) (Load 4 TyU64 item v7 v2 0)
       (tu:get v1 dtop) (Base v2 work) (Addr work v2 v2 v1 2 0) (Store 4 work v2 0 v7)
       (Imm v2 1) (Add v1 v1 v2) (tu:put dtop v1))))
; The node in v2 (an expression, its kind in v3): its items if it is not ground.
(= (tu:open-expression $mode)
   (Seq (CmpI Eq v7 v3 (ts:k expression)) (Load 1 TyU64 node v3 v2 1) (CmpI Eq v4 v3 0)
     (Bin BinAnd TyBool v7 v7 v4) (If v7 (tu:push-items v2 $mode))))

; occurs: refutes if the pending variable occurs in the term in field $root,
; read with assignments followed (locals at the base in field $tbase). A
; variable with a value stands for it; one without is the variable itself.
(= (tu:occurs-var $bound $mode)
   (Seq (CmpI Ne v3 $bound 0)
     (If v3 (tu:push $bound v2 v3 $mode)
       (Seq (tu:get v3 pvar) (Cmp Eq v3 v3 v1) (If v3 (tu:refute))))))
(= (tu:occurs $mode $root $tbase)
   (Seq (tu:search-start $mode $root)
     (EarlyLoop v5 v8 v6 (ts:cap)
       (tu:pop v1 v2)
       (tu:unmet v1
         (Seq (ts:node v2 v1) (Load 4 TyU64 node v4 v2 16) (Load 1 TyU64 node v3 v2 0)
           (CmpI Eq v7 v3 (ts:k local))
           (If v7
             (Seq (tu:get v3 $tbase) (Add v4 v4 v3) (Base v3 local) (Addr local v3 v3 v4 2 0)
               (Load 4 TyU64 local v4 v3 0) (tu:occurs-var v4 $mode))
             (Seq (CmpI Eq v7 v3 (ts:k unknown))
               (If v7
                 (Seq (Base v3 unknown) (Addr unknown v3 v3 v4 2 0) (Load 4 TyU64 unknown v4 v3 0)
                   (tu:occurs-var v4 $mode))
                 (tu:open-expression $mode))))))
       (tu:more))))

; scan, for an unknown about to be assigned the term pt: whether closing pt
; would change it (it mentions a local, or an unknown with a value: CLOSE),
; else whether the unknown occurs in it (refuted). Closing a term that would
; not change gives the term itself, so then pt is the closed term.
(= (tu:scan)
   (Seq (tu:search-start restore pt)
     (EarlyLoop v5 v8 v6 (ts:cap)
       (tu:pop v1 v2)
       (tu:unmet v1
         (Seq (ts:node v2 v1) (Load 4 TyU64 node v4 v2 16) (Load 1 TyU64 node v3 v2 0)
           (CmpI Eq v7 v3 (ts:k local)) (If v7 (tu:close))
           (CmpI Eq v7 v3 (ts:k unknown))
           (If v7
             (Seq (Base v3 unknown) (Addr unknown v3 v3 v4 2 0) (Load 4 TyU64 unknown v4 v3 0)
               (CmpI Ne v7 v4 0) (If v7 (tu:close))
               (tu:get v3 pvar) (Cmp Eq v7 v3 v1) (If v7 (tu:refute)))
             (tu:open-expression restore))))
       (tu:more))))

; ---- One equation ---------------------------------------------------------------

; bind: the variable pvar (at pvbase) is assigned the term pt (at ptbase),
; as tcalc_core.c's bind does.
(= (tu:bind)
   (Seq
     (tu:get v1 pvar) (ts:node v2 v1) (Load 4 TyU64 node v4 v2 16) (Load 1 TyU64 node v3 v2 0)
     (CmpI Eq v5 v3 (ts:k local))
     (If v5 (Seq (tu:get v6 pvbase) (Add v4 v4 v6) (Imm v6 1)) (Imm v6 0))
     (tu:put pindex v4) (tu:put plocal v6)
     (tu:get v1 pt) (ts:node v2 v1) (Load 1 TyU64 node v3 v2 1) (Load 1 TyU64 node v4 v2 0)
     (CmpI Ne v6 v3 0)
     (If v5
       (Seq (CmpI Eq v7 v4 (ts:k unknown)) (Bin BinOr TyBool v7 v7 v6)
         (If v7 (tu:assign-field pt restore)
           (Seq (tu:get v1 ptbase) (tu:get v2 pvbase) (Cmp Eq v3 v1 v2)
             (If v3 (Seq (tu:occurs restore pt ptbase) (tu:assign-field pt restore)) (tu:close)))))
       (If v6 (tu:assign-field pt restore) (Seq (tu:scan) (tu:assign-field pt restore))))))

(= (tu:choose $vf $vbf $tf $tbf)
   (Seq (tu:get v1 $vf) (tu:put pvar v1) (tu:get v1 $vbf) (tu:put pvbase v1)
     (tu:get v1 $tf) (tu:put pt v1) (tu:get v1 $tbf) (tu:put ptbase v1)))

; Two expressions of one length: their item equations, the first on top. The
; equation of item i goes to the stack at wlen + 4 * (n - 1 - i); each is
; stored from its last word down, so that the check of the first store covers
; the other three.
(= (tu:expression)
   (Seq (tu:get v1 ak) (tu:get v2 bk)
     (CmpI Eq v3 v1 (ts:k expression)) (CmpI Eq v4 v2 (ts:k expression)) (Bin BinAnd TyBool v3 v3 v4)
     (If v3 Skip (tu:refute))
     (tu:get v1 a) (ts:node v2 v1) (Load 4 TyU64 node v4 v2 8) (Load 4 TyU64 node v3 v2 4) (tu:put astart v4)
     (tu:get v1 b) (ts:node v2 v1) (Load 4 TyU64 node v4 v2 8) (Load 4 TyU64 node v5 v2 4) (tu:put bstart v4)
     (Cmp Eq v1 v3 v5) (If v1 Skip (tu:refute))
     (tu:get v7 wlen) (Shl v2 v3 2) (Add v2 v2 v7) (tu:get v4 wcap) (Cmp Ule v5 v2 v4)
     (If v5 Skip (tu:exit restore work))
     (tu:put wlen v2) (Imm v1 4) (Sub v7 v2 v1)
     (Loop v6 v3 (ts:cap)
       (Shl v1 v6 2) (Sub v1 v7 v1) (Base v4 work) (Addr work v4 v4 v1 2 0)
       (tu:get v5 bbase) (Store 4 work v4 12 v5)
       (tu:get v2 bstart) (Add v2 v2 v6) (Base v5 item) (Addr item v5 v5 v2 2 0) (Load 4 TyU64 item v2 v5 0)
       (Store 4 work v4 8 v2)
       (tu:get v5 abase) (Store 4 work v4 4 v5)
       (tu:get v2 astart) (Add v2 v2 v6) (Base v5 item) (Addr item v5 v5 v2 2 0) (Load 4 TyU64 item v2 v5 0)
       (Store 4 work v4 0 v2))))

; Two terms that are not one: a variable side is bound (a local before an
; unknown, the left before the right), else they must be expressions.
(= (tu:variable $k $kind) (Seq (Imm $k 3) (Sub $k $kind $k) (CmpI Ule $k $k 1)))
(= (tu:choose-variable)
   (Seq (tu:get v1 ak) (tu:get v2 bk) (CmpI Eq v3 v1 (ts:k local))
     (If v3 (tu:choose a abase b bbase)
       (Seq (CmpI Eq v3 v2 (ts:k local))
         (If v3 (tu:choose b bbase a abase)
           (Seq (CmpI Eq v3 v1 (ts:k unknown))
             (If v3 (tu:choose a abase b bbase) (tu:choose b bbase a abase))))))))
(= (tu:differ)
   (Seq (tu:get v1 ak) (tu:get v2 bk) (tu:variable v3 v1) (tu:variable v4 v2) (Bin BinOr TyBool v3 v3 v4)
     (If v3 (Seq (tu:choose-variable) (tu:bind)) (tu:expression))))

; The top equation, both sides walked. A term is equal to itself unless it
; mentions locals of two frames.
(= (tu:step)
   (Seq
     (tu:get v1 wlen) (Imm v2 4) (Sub v1 v1 v2) (tu:put wlen v1)
     (Base v2 work) (Addr work v2 v2 v1 2 0)
     (Load 4 TyU64 work v6 v2 12) (Load 4 TyU64 work v3 v2 0) (Load 4 TyU64 work v4 v2 4) (Load 4 TyU64 work v5 v2 8)
     (tu:put abase v4) (tu:put b v5) (tu:put bbase v6)
     (ts:walk v3 (* 8 (tu:f abase)) v1 v2 v4 v5) (tu:put a v3) (tu:put ak v4)
     (tu:get v3 b) (ts:walk v3 (* 8 (tu:f bbase)) v1 v2 v4 v5) (tu:put b v3) (tu:put bk v4)
     (tu:get v1 a) (tu:get v2 b) (Cmp Eq v3 v1 v2)
     (If v3
       (Seq (tu:get v4 abase) (tu:get v5 bbase) (Cmp Eq v3 v4 v5)
         (ts:node v4 v1) (Load 1 TyU64 node v5 v4 1) (CmpI Ne v6 v5 0) (Bin BinOr TyBool v3 v3 v6)
         (tu:get v5 ak) (CmpI Eq v6 v5 (ts:k unknown)) (Bin BinOr TyBool v3 v3 v6)))
     (If v3 Skip (tu:differ))))

; A first call (phase 2) puts the equation a = b (at abase, bbase) on the stack.
(= (tu:start)
   (Seq (tu:get v1 wlen) (Imm v2 4) (Add v2 v1 v2) (tu:get v3 wcap) (Cmp Ule v3 v2 v3)
     (If v3 Skip (tu:exit held work))
     (Base v3 work) (Addr work v3 v3 v1 2 0)
     (tu:get v4 bbase) (Store 4 work v3 12 v4) (tu:get v4 b) (Store 4 work v3 8 v4)
     (tu:get v4 abase) (Store 4 work v3 4 v4) (tu:get v4 a) (Store 4 work v3 0 v4)
     (tu:put wlen v2) (Imm v1 0) (tu:put phase v1)))

; The bind left pending by CLOSE, with the closed term the host wrote: an
; unknown is assigned it unless it occurs in it.
(= (tu:resume)
   (Seq (tu:get v1 closed) (CmpI Eq v2 v1 0) (If v2 (tu:done fault))
     (tu:get v1 plocal) (CmpI Eq v2 v1 0)
     (If v2 (Seq (tu:get v1 closed) (ts:node v3 v1) (Load 1 TyU64 node v4 v3 1) (CmpI Eq v5 v4 0)
              (If v5 (tu:occurs held closed ptbase))))
     (tu:assign-field closed held)
     (Imm v1 0) (tu:put phase v1)))

(= (tu:program)
   (p1:program native_tcunify
     (Regions (Region node RO 32 8) (Region item RO 4 4) (Region local RW 4 4) (Region unknown RW 4 4)
       (Region trail RW 4 4) (Region work RW 16 4) (Region seen RW 4 4) (Region state RW 208 8))
     (Registers) (Init)
     (Seq (Base v11 state) (Base v10 node) (Imm v8 (ts:cap))
       (tu:get v1 phase) (CmpI Eq v2 v1 2) (If v2 (tu:start))
       (tu:get v1 phase) (CmpI Ne v2 v1 0) (If v2 (tu:resume))
       (Loop v9 v8 (ts:cap)
         (TFund 1)
         (tu:get v1 wlen) (tu:get v2 floor) (Cmp Ule v3 v1 v2)
         (If v3 (tu:done solved) (tu:step)))
       (tu:done yield))))
!(let* (($text (system:arg 0)) ($notes (system:arg 1))) (p1:write tu:program $text $notes))
