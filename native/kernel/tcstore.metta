; Reading tcalc's term store (native/tcalc/tcalc_core.c) in a p1 program:
; the conventions of native_tcunify and native_tcbuild. Their regions are
; named node, item, local and unknown; v11 holds their state words and v10
; the node region's base throughout, and v8 the bound of every loop. Every
; term, index and count is below 2^32 (the host keeps its tables below 2^31
; entries), so state words and a node's value are read as 32-bit words: an
; index read so needs only its length compare.

(= (ts:cap) 4294967295)
; tcalc's term kinds (tcalc_core.h, TcKind).
(= (ts:k symbol) 0) (= (ts:k integer) 1) (= (ts:k string) 2)
(= (ts:k local) 3) (= (ts:k unknown) 4) (= (ts:k expression) 5)

; $p := the node of term $t (32 bytes: kind 0, ground 1, length 4, start 8,
; value 16). The accesses right after it are checked with it, at once.
(= (ts:node $p $t) (Seq (Shl $p $t 2) (Addr node $p v10 $p 3 0)))

; walk: follows assignments from $t (a local at the base in the state word at
; byte $base) until a variable without a value or a term that is not one.
; Afterwards $p is the kind of the term reached.
(= (ts:walk $t $base $x $k $p $w)
   (Seq (Imm TyBool $k 1) (Imm $p 0)
     (EarlyLoop $x v8 $k (ts:cap)
       (ts:node $p $t) (Load 4 TyU64 node $w $p 16) (Load 1 TyU64 node $p $p 0)
       (CmpI Eq $k $p (ts:k local))
       (If $k
         (Seq (Load 4 TyU64 state $k v11 $base) (Add $w $w $k) (Base $k local) (Addr local $k $k $w 2 0)
           (Load 4 TyU64 local $w $k 0))
         (Seq (CmpI Eq $k $p (ts:k unknown))
           (If $k (Seq (Base $k unknown) (Addr unknown $k $k $w 2 0) (Load 4 TyU64 unknown $w $k 0))
                  (Imm $w 0))))
       (CmpI Ne $k $w 0) (If $k (Mov $t $w)))))
