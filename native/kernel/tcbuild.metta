; Terms with tcalc's assignments applied, over its own term store, as
; resolve_from and close_from make them (native/tcalc/tcalc_core.c): the roots
; at the bottom of the stack region, each read at the base, with every
; assignment followed; when closing, every local without a value is opened to
; a fresh unknown. One walk epoch serves all roots, as the core's walk records
; do. A program cannot make terms, so it writes what the host is to make, in
; the order the core would make it:
;
;   FRESH slot          a fresh unknown, assigned to the local slot
;   MAKE n ref...       the expression of the n items
;
; Entry k is referred to as k | 2^31; any other reference is a term. An
; expression none of whose items changes is that expression itself, the term
; the core's interning would give. Each root becomes the reference of its
; value, in place. The program writes nothing but its stack, its list, its
; state and walk records of this epoch, so a call that leaves for room (ROOM,
; need saying which) is made again from the start.
;
; Registers (tcstore.metta): v11 the state, v10 the term nodes, v8 the bound
; of every loop; v9 is the index of the main loop, v1..v7 are free in each step.
!(import! &self "../../vendor/tcalc-native/native/p1/p1.metta")
!(import! &self "tcstore.metta")
!(import! &self "tcbuild_layout.metta")

(= (tb:get $r $f) (Load 4 TyU64 state $r v11 (* 8 (tb:f $f))))
(= (tb:put $f $r) (Store state v11 (* 8 (tb:f $f)) $r))
(= (tb:done $code) (Seq (Imm v1 (tb:r $code)) (Return FIRED v1)))
(= (tb:exit $what) (Seq (Imm v1 (tb:n $what)) (tb:put need v1) (tb:done room)))
(= (tb:none) 4294967295)
(= (tb:entry-bit) 2147483648)

; The stack: the roots, then frames of four words (term, term reached, next
; item, the frame below) with the values of their finished items above each.
(= (tb:push $v $a $b)
   (Seq (tb:get $a top) (tb:get $b scap) (Cmp Ult $b $a $b) (If $b Skip (tb:exit stack))
     (Base $b stack) (Addr stack $b $b $a 2 0) (Store 4 stack $b 0 $v)
     (Imm $b 1) (Add $a $a $b) (tb:put top $a)))
(= (tb:remember $t $ref $a $b)
   (Seq (tb:get $a epoch) (Base $b seen) (Addr seen $b $b $t 2 0) (Store 4 seen $b 0 $a)
     (Base $b memo) (Addr memo $b $b $t 2 0) (Store 4 memo $b 0 $ref)))
; $hit with v3 the value of $c if $c was met in this epoch, else $miss.
(= (tb:recall $c $hit $miss)
   (Seq (Base v2 seen) (Addr seen v2 v2 $c 2 0) (Load 4 TyU64 seen v3 v2 0) (tb:get v4 epoch) (Cmp Eq v4 v3 v4)
     (If v4 (Seq (Base v2 memo) (Addr memo v2 v2 $c 2 0) (Load 4 TyU64 memo v3 v2 0) $hit) $miss)))
(= (tb:entry $r)
   (Seq (tb:get $r entries) (Imm v3 1) (Add v3 $r v3) (tb:put entries v3) (Imm v3 (tb:entry-bit)) (Or $r $r v3)))

; The local reached (in v1, its index in v5) is opened: FRESH.
(= (tb:fresh)
   (Seq (tb:get v2 base) (Add v5 v5 v2)
     (tb:get v2 outlen) (Imm v3 2) (Add v3 v2 v3) (tb:get v4 outcap) (Cmp Ule v4 v3 v4) (If v4 Skip (tb:exit out))
     (Base v4 out) (Addr out v4 v4 v2 2 0) (Store 4 out v4 4 v5) (Imm v6 (tb:k fresh)) (Store 4 out v4 0 v6)
     (tb:put outlen v3)
     (tb:entry v2)
     (tb:get v3 vt) (tb:remember v3 v2 v4 v5) (tb:get v3 vat) (tb:remember v3 v2 v4 v5)
     (tb:push v2 v4 v5)))

; The expression reached (in v1) gets a frame.
(= (tb:frame)
   (Seq (tb:get v2 top) (Imm v3 4) (Add v3 v2 v3) (tb:get v4 scap) (Cmp Ule v4 v3 v4) (If v4 Skip (tb:exit stack))
     (Base v4 stack) (Addr stack v4 v4 v2 2 0)
     (tb:get v5 cur) (Store 4 stack v4 12 v5) (Imm v5 0) (Store 4 stack v4 8 v5)
     (Store 4 stack v4 4 v1) (tb:get v5 vt) (Store 4 stack v4 0 v5)
     (tb:put cur v2) (tb:put top v3)))

; The term reached (in v1), met first in this epoch: a ground term, an
; unknown and (resolving) a local stand for themselves.
(= (tb:reached)
   (Seq (ts:node v2 v1) (Load 4 TyU64 node v5 v2 16) (Load 1 TyU64 node v3 v2 0) (Load 1 TyU64 node v4 v2 1)
     (CmpI Ne v6 v4 0)
     (If v6 (tb:push v1 v4 v6)
       (Seq (CmpI Eq v6 v3 (ts:k expression))
         (If v6 (tb:frame)
           (Seq (CmpI Eq v6 v3 (ts:k local)) (tb:get v4 close) (CmpI Ne v4 v4 0) (Bin BinAnd TyBool v6 v6 v4)
             (If v6 (tb:fresh) (tb:push v1 v4 v6))))))))

; A term (in v1): its value, or a frame for it.
(= (tb:visit)
   (Seq (tb:put vt v1)
     (tb:recall v1 (tb:push v3 v4 v5)
       (Seq (ts:walk v1 (* 8 (tb:f base)) v2 v3 v4 v5) (tb:put vat v1)
         (tb:recall v1 (Seq (tb:get v1 vt) (tb:remember v1 v3 v4 v5) (tb:push v3 v4 v5))
           (tb:reached))))))

; The frame on top with all its items' values: its value is the expression
; reached when no item changed, else a MAKE of the values.
(= (tb:make)
   (Seq (tb:get v1 outlen) (tb:get v4 flen) (Imm v3 2) (Add v3 v3 v4) (Add v3 v3 v1) (tb:get v5 outcap)
     (Cmp Ule v5 v3 v5) (If v5 Skip (tb:exit out))
     (Base v5 out) (Addr out v5 v5 v1 2 0) (Store 4 out v5 4 v4) (Imm v3 (tb:k make)) (Store 4 out v5 0 v3)
     (Loop v2 v4 (ts:cap)
       (tb:get v1 cur) (Imm v3 4) (Add v1 v1 v3) (Add v1 v1 v2) (Base v3 stack) (Addr stack v3 v3 v1 2 0)
       (Load 4 TyU64 stack v1 v3 0)
       (tb:get v3 outlen) (Imm v5 2) (Add v3 v3 v5) (Add v3 v3 v2) (Base v5 out) (Addr out v5 v5 v3 2 0)
       (Store 4 out v5 0 v1))
     (tb:get v1 outlen) (Imm v3 2) (Add v1 v1 v3) (Add v1 v1 v4) (tb:put outlen v1)
     (tb:entry v1) (tb:put ref v1)))
(= (tb:finish)
   (Seq (tb:get v1 cur) (Base v2 stack) (Addr stack v2 v2 v1 2 0)
     (Load 4 TyU64 stack v3 v2 12) (Load 4 TyU64 stack v4 v2 4) (Load 4 TyU64 stack v5 v2 0)
     (tb:put parent v3) (tb:put vat v4) (tb:put vt v5)
     (ts:node v2 v4) (Load 4 TyU64 node v3 v2 8) (Load 4 TyU64 node v4 v2 4) (tb:put fstart v3) (tb:put flen v4)
     (Imm TyBool v6 1)
     (EarlyLoop v2 v4 v6 (ts:cap)
       (tb:get v1 cur) (Imm v3 4) (Add v1 v1 v3) (Add v1 v1 v2) (Base v3 stack) (Addr stack v3 v3 v1 2 0)
       (Load 4 TyU64 stack v1 v3 0)
       (tb:get v3 fstart) (Add v3 v3 v2) (Base v5 item) (Addr item v5 v5 v3 2 0) (Load 4 TyU64 item v3 v5 0)
       (Cmp Eq v6 v1 v3))
     (If v6 (Seq (tb:get v1 vat) (tb:put ref v1)) (tb:make))
     (tb:get v1 vt) (tb:get v2 ref) (tb:remember v1 v2 v3 v4) (tb:get v1 vat) (tb:remember v1 v2 v3 v4)
     (tb:get v1 cur) (tb:put top v1) (tb:get v1 parent) (tb:put cur v1)
     (tb:get v2 ref) (tb:push v2 v3 v4)))

; The frame on top: its next item, or its end.
(= (tb:step)
   (Seq (tb:get v1 cur) (Base v2 stack) (Addr stack v2 v2 v1 2 0) (Load 4 TyU64 stack v3 v2 8) (Load 4 TyU64 stack v4 v2 4)
     (ts:node v5 v4) (Load 4 TyU64 node v6 v5 8) (Load 4 TyU64 node v5 v5 4)
     (Cmp Ult v4 v3 v5)
     (If v4
       (Seq (Imm v4 1) (Add v4 v3 v4) (Store 4 stack v2 8 v4)
         (Add v6 v6 v3) (Base v1 item) (Addr item v1 v1 v6 2 0) (Load 4 TyU64 item v1 v1 0)
         (tb:visit))
       (tb:finish))))

; No frame: the root just finished takes its value, and the next one starts.
(= (tb:root)
   (Seq (tb:get v1 started) (CmpI Ne v2 v1 0)
     (If v2
       (Seq (tb:get v1 top) (Imm v2 1) (Sub v1 v1 v2) (tb:put top v1)
         (Base v2 stack) (Addr stack v2 v2 v1 2 0) (Load 4 TyU64 stack v3 v2 0)
         (tb:get v1 r) (Base v2 stack) (Addr stack v2 v2 v1 2 0) (Store 4 stack v2 0 v3)
         (Imm v2 1) (Add v1 v1 v2) (tb:put r v1) (Imm v1 0) (tb:put started v1)))
     (tb:get v1 r) (tb:get v2 roots) (Cmp Ult v3 v1 v2)
     (If v3 Skip (tb:done done))
     (Imm v2 1) (tb:put started v2)
     (Base v2 stack) (Addr stack v2 v2 v1 2 0) (Load 4 TyU64 stack v1 v2 0)
     (tb:visit)))

(= (tb:program)
   (p1:program native_tcbuild
     (Regions (Region node RO 32 8) (Region item RO 4 4) (Region local RO 4 4) (Region unknown RO 4 4)
       (Region seen RW 4 4) (Region memo RW 4 4) (Region out RW 8 4) (Region stack RW 16 4) (Region state RW 152 8))
     (Registers) (Init)
     (Seq (Base v11 state) (Base v10 node) (Imm v8 (ts:cap))
       (tb:get v1 epoch) (CmpI Eq v2 v1 4294967295) (If v2 (tb:exit epoch))
       (Imm v2 1) (Add v1 v1 v2) (tb:put epoch v1)
       (tb:get v1 roots) (tb:put top v1) (Imm v1 (tb:none)) (tb:put cur v1)
       (Imm v1 0) (tb:put r v1) (tb:put started v1) (tb:put outlen v1) (tb:put entries v1)
       (Loop v9 v8 (ts:cap)
         (TFund 1)
         (tb:get v1 cur) (CmpI Eq v2 v1 (tb:none))
         (If v2 (tb:root) (tb:step)))
       (tb:done yield))))
!(let* (($text (system:arg 0)) ($notes (system:arg 1))) (p1:write tb:program $text $notes))
