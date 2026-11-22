; p1.metta -- authoring t-ISA profile p1 programs (CeTTa, PeTTa lane).
;
; A p1 program has one text: the HOL term tisa_print_program prints in HOL
; style (tcalc/tisa/tisa_print.c). HOL Light proves theorems about that text,
; C loads it (tcalc/native/p1/p1_load.h) to run the reference evaluator and
; the native code, and this library writes it. The library is outside the
; trust boundary: whatever it does, the proofs are about the text it emits.
;
; Four layers:
;
;   forms       statements and instructions written as data (section 4), with
;               registers and regions by name, structured sequencing, register
;               aliases (With) and annotations (Block, Invariant, Spec, Note)
;   templates   ordinary MeTTa functions that return forms; parameterized by
;               registers and regions, they compose by inlining
;   resolution  (p1:program NAME REGIONS REGISTERS INIT BODY) binds every
;               name, checks every field against tisa.h, expands the forms and
;               collects the annotations with their statement paths
;               (section 5); the result is
;                 (P1Program NAME (TProg DECLS INITS ROOT) NOTES)
;               where the TProg atom is the S-expression spelling, the atom
;               tisa_x86.metta emits
;   text        (p1:hol-text PROGRAM) is the HOL text, character for
;               character as tisa_print_program writes it (section 7); notes
;               are written to a side file, never into the program text
;               (section 8)
;
; Statement paths are tisa_print.c's: child indices joined by ".", "" the
; root; SEQ first 0, second 1; IF then 1, else 2; a loop's body 0.
; Input outside these forms stops the run with a message and exit status 3.

!(import! &self str)
!(import! &self fs)
!(import! &self system)

; ---- 1. Failure --------------------------------------------------------------

(= (p1:reject $why)
   (let $shown (system:write stderr (str:concat "p1.metta: " (str:concat (repr $why) "\n")))
     (system:exit 3)))

; ---- 2. Fields ------------------------------------------------------------------
;
; Every number must fit the field of tisa.h that holds it: registers v0..v11,
; regions 0..8, uint8_t shift, lsb, width and scale fields, int32_t
; displacements, uint32_t caps and units, uint64_t immediates; a frame word
; index is 0..23.

(= (p1:number? $x) (== (get-type $x) Number))

(= (p1:in $x $lo $hi $what)
   (if (p1:number? $x)
       (if (and (>= $x $lo) (< $x $hi)) $x (p1:reject ($what $x out-of-range)))
       (p1:reject ($what $x not-a-number))))

(= (p1:u8 $x $what) (p1:in $x 0 256 $what))
(= (p1:u32 $x $what) (p1:in $x 0 4294967296 $what))
(= (p1:u64 $x) (p1:in $x 0 18446744073709551616 word))
(= (p1:s32 $x) (p1:in $x -2147483648 2147483648 displacement))

(= (p1:ty $t)
   (case $t
     ((TyU64 TyU64) (TyU32Z TyU32Z) (TyI32S TyI32S) (TyBool TyBool) (TyPtr TyPtr)
      ($t0 (p1:reject (type $t0))))))

(= (p1:binop $o)
   (case $o
     ((BinAdd BinAdd) (BinSub BinSub) (BinAnd BinAnd) (BinOr BinOr) (BinXor BinXor)
      (BinMulLo BinMulLo) ($o0 (p1:reject (binary-operation $o0))))))

(= (p1:shiftop $o)
   (case $o
     ((ShiftLeft ShiftLeft) (ShiftRightU ShiftRightU) (ShiftRightS ShiftRightS)
      ($o0 (p1:reject (shift-operation $o0))))))

; Comparisons by their short names or the datatype's.
(= (p1:cond $c)
   (case $c
     ((Eq CondEq) (Ne CondNe) (Ult CondUlt) (Ule CondUle) (Slt CondSlt) (Sle CondSle)
      (CondEq CondEq) (CondNe CondNe) (CondUlt CondUlt) (CondUle CondUle)
      (CondSlt CondSlt) (CondSle CondSle)
      ($c0 (p1:reject (comparison $c0))))))

; RETURN's outcome: FIRED 1, REFUSED 0, UNSUPPORTED 2 (SEMANTICS.txt, section 3).
(= (p1:outcome $o)
   (case $o
     ((FIRED 1) (REFUSED 0) (UNSUPPORTED 2) (1 1) (0 0) (2 2)
      ($o0 (p1:reject (return-outcome $o0))))))

(= (p1:mode $m)
   (case $m
     ((RW 2) (RO 1) (NONE 0) ($m0 (p1:reject (region-mode $m0))))))

; ---- 3. Names ----------------------------------------------------------------------
;
; The environment (Env REGISTERS REGIONS) holds (Bound NAME NUMBER) entries,
; innermost first. v0..v11 always name themselves; a number names its
; register. A name never heads an expression, here or in the forms: PeTTa
; would call a function of that name (a register named empty would call the
; builtin empty), so every binding has a constructor at its head.

(= (p1:machine-registers)
   ((Bound v0 0) (Bound v1 1) (Bound v2 2) (Bound v3 3) (Bound v4 4) (Bound v5 5)
    (Bound v6 6) (Bound v7 7) (Bound v8 8) (Bound v9 9) (Bound v10 10) (Bound v11 11)))

(= (p1:lookup $name $entries $what)
   (if (== $entries ())
       (p1:reject (unbound $what $name))
       (let ((Bound $key $value) $rest) (decons-atom $entries)
         (if (== $key $name) $value (p1:lookup $name $rest $what)))))

(= (p1:reg $r (Env $regs $regions))
   (if (p1:number? $r) (p1:in $r 0 12 register) (p1:lookup $r $regs register)))

(= (p1:region $r (Env $regs $regions))
   (if (p1:number? $r) (p1:in $r 0 9 region) (p1:lookup $r $regions region)))

; Region 255 is the frame: a load from it is a TLoadFrame.
(= (p1:load-region $r $env) (p1:region $r $env))

; Frame words: region r's base is word 2r, its length 2r + 1; 18 status,
; 19 ret_val, 20 fuel_budget, 21 fuel_used, 22 arg0, 23 arg1.
(= (p1:frame-word $j $env)
   (case $j
     (((FBase $r1) (* 2 (p1:region $r1 $env)))
      ((FLen $r2) (+ 1 (* 2 (p1:region $r2 $env))))
      ($j3 (p1:in $j3 0 24 frame-word)))))

; (As NAME TARGET): NAME stands for TARGET's register; TARGET is a name of the
; enclosing scope, vK, or a number.
(= (p1:bind-register $bind $env)
   (case $bind
     (((As $name1 $target1)
       (let (Env $regs1 $regions1) $env
         (Env (cons-atom (Bound $name1 (p1:reg $target1 $env)) $regs1) $regions1)))
      ($other2 (p1:reject (register-binding $other2 expected (As NAME REGISTER)))))))

(= (p1:bind-registers $binds $env)
   (foldl-atom $binds $env $acc $bind (p1:bind-register $bind $acc)))

; The leading (As ...) bindings of a With, and the statements after them.
(= (p1:split-bindings $args $binds)
   (if (== $args ())
       ((reverse $binds) ())
       (let ($first $rest) (decons-atom $args)
         (if (== (get-metatype $first) Expression)
             (if (== (car-atom $first) As)
                 (p1:split-bindings $rest (cons-atom $first $binds))
                 ((reverse $binds) $args))
             ((reverse $binds) $args)))))

; ---- 4. Forms -----------------------------------------------------------------------
;
; Statements:
;   (Seq S ...)               S ... in order (right-nested TSeq); (Seq) is TSkip
;   Skip                      TSkip
;   (If C THEN)  (If C THEN ELSE)
;   (Loop X N CAP S ...)      X := 0; while X < N: S ...; X := X + 1
;   (EarlyLoop X N K CAP S ...)  as Loop, stopping when K is 0 (before and
;                             after each body)
;   (Return OUTCOME R)        OUTCOME is FIRED, REFUSED or UNSUPPORTED
;   (With (As NAME REG) ... S ...)  register aliases for S ...
;   (Block NAME S ...)        S ... as one statement, named: its text goes to
;                             the notes, for a HOL definition
;   (Invariant NAME TEXT LOOP)          a loop invariant (HOL text)
;   (Spec NAME PRE POST FAIL S ...)     a block specification: HOL texts
;                             P : tisa_state->bool, Q : tisa_state->bool,
;                             R : num->tisa_state->bool, as tisa_spec P S Q R
;   (Note KIND NAME (TEXT ...) S ...)   any other annotation
; and every datatype form: TSkip (TSeq A B) (TIf C A B) (TCountLoop X N CAP B)
; (TEarlyLoop X N K CAP B) (TReturn O R) (TFund U) (TInstr I). FUND has no
; shorthand: only a compiler places FUND points (SEMANTICS.txt, section 8).
;
; Instructions (as statements), U64 unless a type comes first:
;   (Imm [T] D K)  (Mov [T] D A)
;   (Add D A B) (Sub ..) (And ..) (Or ..) (Xor ..) (Mul ..)  (Bin OP T D A B)
;   (Shl D A S) (Shr D A S) (Sar D A S)  (Shift OP T D A S)
;   (Extract [T] D A LSB WIDTH)
;   (Cmp [T] COND D A B)  (CmpI [T] COND D A K)   COND: Eq Ne Ult Ule Slt Sle
;   (Select [T] D K A B)                          D := K ? A : B
;   (Addr REGION D P INDEX SCALE DISP)            INDEX: a register or None
;   (Load REGION D P DISP)  (Load WIDTH T REGION D P DISP)
;   (Store REGION P DISP V)  (Store WIDTH REGION P DISP V)
;   (Base D REGION) (Len D REGION) (Arg0 D) (Arg1 D) (FrameWord T D WORD)
; and the datatype forms (TImm T D K) ... (TStore W R P O V), with names in
; register and region fields, (FBase REGION) and (FLen REGION) as frame words.

(= (p1:index $i) (if (== $i None) NONE (if (== $i NONE) NONE (SOME $i))))

; An instruction form as the datatype's instruction, or NotAnInstruction.
(= (p1:instruction-form $s)
   (case $s
     (((Imm $d1 $k1) (TImm TyU64 $d1 $k1))
      ((Imm $t2 $d2 $k2) (TImm $t2 $d2 $k2))
      ((Mov $d3 $a3) (TMov TyU64 $d3 $a3))
      ((Mov $t4 $d4 $a4) (TMov $t4 $d4 $a4))
      ((Add $d5 $a5 $b5) (TBin BinAdd TyU64 $d5 $a5 $b5))
      ((Sub $d6 $a6 $b6) (TBin BinSub TyU64 $d6 $a6 $b6))
      ((And $d7 $a7 $b7) (TBin BinAnd TyU64 $d7 $a7 $b7))
      ((Or $d8 $a8 $b8) (TBin BinOr TyU64 $d8 $a8 $b8))
      ((Xor $d9 $a9 $b9) (TBin BinXor TyU64 $d9 $a9 $b9))
      ((Mul $d10 $a10 $b10) (TBin BinMulLo TyU64 $d10 $a10 $b10))
      ((Bin $o11 $t11 $d11 $a11 $b11) (TBin $o11 $t11 $d11 $a11 $b11))
      ((Shl $d12 $a12 $s12) (TShift ShiftLeft TyU64 $d12 $a12 $s12))
      ((Shr $d13 $a13 $s13) (TShift ShiftRightU TyU64 $d13 $a13 $s13))
      ((Sar $d14 $a14 $s14) (TShift ShiftRightS TyU64 $d14 $a14 $s14))
      ((Shift $o15 $t15 $d15 $a15 $s15) (TShift $o15 $t15 $d15 $a15 $s15))
      ((Extract $d16 $a16 $l16 $w16) (TExtract TyU64 $d16 $a16 $l16 $w16))
      ((Extract $t17 $d17 $a17 $l17 $w17) (TExtract $t17 $d17 $a17 $l17 $w17))
      ((Cmp $c18 $d18 $a18 $b18) (TCmp $c18 TyU64 $d18 $a18 (OpReg $b18)))
      ((Cmp $t19 $c19 $d19 $a19 $b19) (TCmp $c19 $t19 $d19 $a19 (OpReg $b19)))
      ((CmpI $c20 $d20 $a20 $k20) (TCmp $c20 TyU64 $d20 $a20 (OpImm $k20)))
      ((CmpI $t21 $c21 $d21 $a21 $k21) (TCmp $c21 $t21 $d21 $a21 (OpImm $k21)))
      ((Select $d22 $k22 $a22 $b22) (TSelect TyU64 $d22 $k22 $a22 $b22))
      ((Select $t23 $d23 $k23 $a23 $b23) (TSelect $t23 $d23 $k23 $a23 $b23))
      ((Addr $r24 $d24 $p24 $i24 $s24 $o24) (TAddr $r24 $d24 $p24 (p1:index $i24) $s24 $o24))
      ((Load $r25 $d25 $p25 $o25) (TLoad TyU64 8 $r25 $d25 $p25 $o25))
      ((Load $w26 $t26 $r26 $d26 $p26 $o26) (TLoad $t26 $w26 $r26 $d26 $p26 $o26))
      ((Store $r27 $p27 $o27 $v27) (TStore 8 $r27 $p27 $o27 $v27))
      ((Store $w28 $r28 $p28 $o28 $v28) (TStore $w28 $r28 $p28 $o28 $v28))
      ((Base $d29 $r29) (TLoadFrame TyPtr $d29 (FBase $r29)))
      ((Len $d30 $r30) (TLoadFrame TyU64 $d30 (FLen $r30)))
      ((Arg0 $d31) (TLoadFrame TyU64 $d31 22))
      ((Arg1 $d32) (TLoadFrame TyU64 $d32 23))
      ((FrameWord $t33 $d33 $j33) (TLoadFrame $t33 $d33 $j33))
      ((TImm $t34 $d34 $k34) $s)
      ((TMov $t35 $d35 $a35) $s)
      ((TBin $o36 $t36 $d36 $a36 $b36) $s)
      ((TShift $o37 $t37 $d37 $a37 $s37) $s)
      ((TExtract $t38 $d38 $a38 $l38 $w38) $s)
      ((TCmp $c39 $t39 $d39 $a39 $r39) $s)
      ((TSelect $t40 $d40 $k40 $a40 $b40) $s)
      ((TAddr $r41 $d41 $p41 $i41 $s41 $o41) $s)
      ((TLoad $t42 $w42 $r42 $d42 $p42 $o42) $s)
      ((TLoadFrame $t43 $d43 $j43) $s)
      ((TStore $w44 $r44 $p44 $o44 $v44) $s)
      ($other NotAnInstruction))))

; A datatype instruction with every name bound and every field checked.
(= (p1:instr $i $env)
   (case $i
     (((TImm $t1 $d1 $k1) (TImm (p1:ty $t1) (p1:reg $d1 $env) (p1:u64 $k1)))
      ((TMov $t2 $d2 $a2) (TMov (p1:ty $t2) (p1:reg $d2 $env) (p1:reg $a2 $env)))
      ((TBin $o3 $t3 $d3 $a3 $b3)
       (TBin (p1:binop $o3) (p1:ty $t3) (p1:reg $d3 $env) (p1:reg $a3 $env) (p1:reg $b3 $env)))
      ((TShift $o4 $t4 $d4 $a4 $s4)
       (TShift (p1:shiftop $o4) (p1:ty $t4) (p1:reg $d4 $env) (p1:reg $a4 $env) (p1:u8 $s4 shift-amount)))
      ((TExtract $t5 $d5 $a5 $l5 $w5)
       (TExtract (p1:ty $t5) (p1:reg $d5 $env) (p1:reg $a5 $env) (p1:u8 $l5 extract-lsb) (p1:u8 $w5 extract-width)))
      ((TCmp $c6 $t6 $d6 $a6 (OpReg $b6))
       (TCmp (p1:cond $c6) (p1:ty $t6) (p1:reg $d6 $env) (p1:reg $a6 $env) (OpReg (p1:reg $b6 $env))))
      ((TCmp $c7 $t7 $d7 $a7 (OpImm $k7))
       (TCmp (p1:cond $c7) (p1:ty $t7) (p1:reg $d7 $env) (p1:reg $a7 $env) (OpImm (p1:u64 $k7))))
      ((TSelect $t8 $d8 $k8 $a8 $b8)
       (TSelect (p1:ty $t8) (p1:reg $d8 $env) (p1:reg $k8 $env) (p1:reg $a8 $env) (p1:reg $b8 $env)))
      ((TAddr $r9 $d9 $p9 NONE $s9 $o9)
       (TAddr (p1:region $r9 $env) (p1:reg $d9 $env) (p1:reg $p9 $env) NONE (p1:u8 $s9 scale) (p1:s32 $o9)))
      ((TAddr $r10 $d10 $p10 (SOME $x10) $s10 $o10)
       (TAddr (p1:region $r10 $env) (p1:reg $d10 $env) (p1:reg $p10 $env) (SOME (p1:reg $x10 $env))
              (p1:u8 $s10 scale) (p1:s32 $o10)))
      ((TLoad $t11 $w11 $r11 $d11 $p11 $o11)
       (TLoad (p1:ty $t11) (p1:u8 $w11 width) (p1:load-region $r11 $env) (p1:reg $d11 $env)
              (p1:reg $p11 $env) (p1:s32 $o11)))
      ((TLoadFrame $t12 $d12 $j12)
       (TLoadFrame (p1:ty $t12) (p1:reg $d12 $env) (p1:frame-word $j12 $env)))
      ((TStore $w13 $r13 $p13 $o13 $v13)
       (TStore (p1:u8 $w13 width) (p1:region $r13 $env) (p1:reg $p13 $env) (p1:s32 $o13) (p1:reg $v13 $env)))
      ($other (p1:reject (instruction $other))))))

; ---- 5. Resolution -------------------------------------------------------------------
;
; (p1:resolve S ENV PATH NOTES) is (R CORE NOTES'): CORE the statement with
; every form expanded and every name bound; PATH the statement's path as
; child indices, innermost first; NOTES the annotations so far, newest first,
; each (Note PATH KIND NAME TEXTS) with TEXTS a list of HOL texts, or
; (BlockText CORE) for a block's own text.

(= (p1:seq-of $items)
   (if (== $items ())
       TSkip
       (let ($first $rest) (decons-atom $items)
         (if (== $rest ()) $first (TSeq $first (cons-atom Seq $rest))))))

(= (p1:resolve $s $env $path $notes)
   (if (== (get-metatype $s) Expression)
       (p1:resolve-form $s $env $path $notes)
       (case $s
         ((TSkip (R TSkip $notes))
          (Skip (R TSkip $notes))
          ($s0 (p1:reject (statement $s0)))))))

(= (p1:resolve-form $s $env $path $notes)
   (case $s
     (((Seq) (R TSkip $notes))
      ((TSeq $a1 $b1)
       (let* (((R $a1r $n1) (p1:resolve $a1 $env (cons-atom 0 $path) $notes))
              ((R $b1r $n1b) (p1:resolve $b1 $env (cons-atom 1 $path) $n1)))
         (R (TSeq $a1r $b1r) $n1b)))
      ((TIf $c2 $a2 $b2) (p1:resolve-if $c2 $a2 $b2 $env $path $notes))
      ((If $c3 $a3) (p1:resolve-if $c3 $a3 TSkip $env $path $notes))
      ((If $c4 $a4 $b4) (p1:resolve-if $c4 $a4 $b4 $env $path $notes))
      ((TCountLoop $x5 $n5 $cap5 $body5)
       (let (R $body5r $n5b) (p1:resolve $body5 $env (cons-atom 0 $path) $notes)
         (R (TCountLoop (p1:reg $x5 $env) (p1:reg $n5 $env) (p1:u32 $cap5 loop-cap) $body5r) $n5b)))
      ((TEarlyLoop $x6 $n6 $k6 $cap6 $body6)
       (let (R $body6r $n6b) (p1:resolve $body6 $env (cons-atom 0 $path) $notes)
         (R (TEarlyLoop (p1:reg $x6 $env) (p1:reg $n6 $env) (p1:reg $k6 $env) (p1:u32 $cap6 loop-cap) $body6r)
            $n6b)))
      ((TReturn $o7 $r7) (R (TReturn (p1:outcome $o7) (p1:reg $r7 $env)) $notes))
      ((Return $o8 $r8) (R (TReturn (p1:outcome $o8) (p1:reg $r8 $env)) $notes))
      ((TFund $u9) (R (TFund (p1:in $u9 1 4294967296 fund-units)) $notes))
      ((TInstr $i10) (R (TInstr (p1:instr $i10 $env)) $notes))
      ((Invariant $name11 $text11 $loop11)
       (p1:annotate invariant $name11 ($text11) ($loop11) $env $path $notes))
      ($other (p1:resolve-variadic $other $env $path $notes)))))

(= (p1:resolve-if $c $a $b $env $path $notes)
   (let* (((R $ar $na) (p1:resolve $a $env (cons-atom 1 $path) $notes))
          ((R $br $nb) (p1:resolve $b $env (cons-atom 2 $path) $na)))
     (R (TIf (p1:reg $c $env) $ar $br) $nb)))

; Forms with a variable number of statements, then instructions.
(= (p1:resolve-variadic $s $env $path $notes)
   (let ($head $args) (decons-atom $s)
     (case $head
       ((Seq (p1:resolve (p1:seq-of $args) $env $path $notes))
        (Loop (p1:resolve-loop $args $env $path $notes))
        (EarlyLoop (p1:resolve-early-loop $args $env $path $notes))
        (With (let ($binds $body) (p1:split-bindings $args ())
                (p1:resolve (p1:seq-of $body) (p1:bind-registers $binds $env) $path $notes)))
        (Block (let ($name $body) (decons-atom $args)
                 (p1:annotate block $name Own $body $env $path $notes)))
        (Spec (let ($name $more) (decons-atom $args)
                (let (($pre $post $fail) $body) (p1:split 3 $more)
                  (p1:annotate spec $name ($pre $post $fail) $body $env $path $notes))))
        (Note (let ($kind $more) (decons-atom $args)
                (let (($name $texts) $body) (p1:split 2 $more)
                  (p1:annotate $kind $name $texts $body $env $path $notes))))
        ($h (let $i (p1:instruction-form $s)
              (if (== $i NotAnInstruction)
                  (p1:reject (statement $s))
                  (R (TInstr (p1:instr $i $env)) $notes))))))))

; The first n elements of a list, and the rest.
(= (p1:split $n $items)
   (if (== $n 0)
       (() $items)
       (if (== $items ())
           (p1:reject (too-few-arguments $n))
           (let* ((($x $rest) (decons-atom $items))
                  (($taken $left) (p1:split (- $n 1) $rest)))
             ((cons-atom $x $taken) $left)))))

(= (p1:resolve-loop $args $env $path $notes)
   (let (($x $n $cap) $body) (p1:split 3 $args)
     (p1:resolve (TCountLoop $x $n $cap (p1:seq-of $body)) $env $path $notes)))

(= (p1:resolve-early-loop $args $env $path $notes)
   (let (($x $n $k $cap) $body) (p1:split 4 $args)
     (p1:resolve (TEarlyLoop $x $n $k $cap (p1:seq-of $body)) $env $path $notes)))

; An annotated statement: its notes come after the notes before it and before
; the notes inside it, so that the notes are listed in statement order.
(= (p1:annotate $kind $name $texts $body $env $path $notes)
   (let (R $core $inner) (p1:resolve (p1:seq-of $body) $env $path ())
     (let $note (Note (p1:path-text $path) $kind $name (if (== $texts Own) (BlockText $core) $texts))
       (R $core (append $inner (cons-atom $note $notes))))))

(= (p1:path-text $path)
   (str:join "." (map-atom (reverse $path) $k (str:number->string $k))))

; ---- 6. Programs ----------------------------------------------------------------------
;
; (p1:program NAME (Regions (Region NAME MODE MIN ALIGN) ...)
;                  (Registers (As NAME REG) ...)
;                  (Init (Entry NAME TYPE) | (Entry NAME TyPtr REGION) ...)
;                  BODY)
; Regions are numbered in order (MODE RW, RO or NONE). Registers bind names
; to v0..v11 (a name may also be bound to an earlier name). Init lists the
; registers set on entry, v1..v5 (SEMANTICS.txt, section 3).

(= (p1:regions $decls $k $env-acc $decl-acc)
   (if (== $decls ())
       (Decls (reverse $decl-acc) $env-acc)
       (let ($d $more) (decons-atom $decls)
         (case $d
           (((Region $name $mode $min $align)
             (p1:regions $more (+ $k 1) (cons-atom (Bound $name $k) $env-acc)
                         (cons-atom ((p1:mode $mode) (p1:u32 $min region-length) (p1:u32 $align region-alignment))
                                    $decl-acc)))
            ($other (p1:reject (region-declaration $other expected (Region NAME MODE MIN ALIGN)))))))))

(= (p1:init-entry $entry $env)
   (case $entry
     (((Entry $n1 $t1) (let $r1 (p1:reg $n1 $env)
                         (if (== $t1 TyPtr) (p1:reject (pointer-needs-a-region $n1))
                             ((p1:in $r1 1 6 entry-register) (p1:ty $t1) 0))))
      ((Entry $n2 TyPtr $g2) ((p1:in (p1:reg $n2 $env) 1 6 entry-register) TyPtr (p1:region $g2 $env)))
      ($e3 (p1:reject (init-entry $e3 expected (Entry NAME TYPE)))))))

; Insertion by register number; a register listed twice is refused.
(= (p1:insert-init $e $sorted)
   (if (== $sorted ())
       ($e)
       (let* ((($first $rest) (decons-atom $sorted))
              (($r $t $g) $e)
              (($r0 $t0 $g0) $first))
         (if (< $r $r0)
             (cons-atom $e $sorted)
             (if (== $r $r0)
                 (p1:reject (entry-register-twice $r))
                 (cons-atom $first (p1:insert-init $e $rest)))))))

(= (p1:inits $entries $env)
   (foldl-atom $entries () $acc $e (p1:insert-init (p1:init-entry $e $env) $acc)))

(= (p1:program $name $regions $registers $init $body)
   (let* ((($rh $decls) (decons-atom $regions))
          (($gh $binds) (decons-atom $registers))
          (($ih $entries) (decons-atom $init))
          ($checked (if (and (== $rh Regions) (and (== $gh Registers) (== $ih Init)))
                        ok
                        (p1:reject (program-expects Regions Registers Init))))
          ((Decls $decl-list $region-env) (p1:regions $decls 0 () ()))
          ($env (p1:bind-registers $binds (Env (p1:machine-registers) $region-env)))
          ($init-list (p1:inits $entries $env))
          ((R $root $notes) (p1:resolve $body $env () ())))
     (P1Program $name (TProg $decl-list $init-list $root) (reverse $notes))))

; Exactly one program, or a loud stop: a form that resolves to nothing, or to
; several things, is an error in the program, never a silent drop. MAKE names
; a function of no arguments that returns the program, so that the program is
; made inside the count.
(= (p1:the-program $make)
   (let $all (collapse ($make))
     (if (== (length $all) 1)
         (car-atom $all)
         (p1:reject (program $make resolved-to (length $all) results)))))

; ---- 7. The HOL text --------------------------------------------------------------------
;
; tisa_print.c in HOL style, piece by piece: a printer takes the pieces so
; far (newest first) and adds its own.

(= (p1:num $n) (str:number->string $n))
(= (p1:sym $s) (str:symbol->string $s))
(= (p1:cat $pieces) (str:join "" $pieces))

(= (p1:word-text $k) (p1:cat ("(word " (p1:num $k) ")")))
(= (p1:int-text $o)
   (if (< $o 0)
       (p1:cat ("(-- &" (p1:num (- 0 $o)) ":int)"))
       (p1:cat ("(&" (p1:num $o) ":int)"))))

(= (p1:instr-text $i)
   (case $i
     (((TImm $t1 $d1 $k1)
       (p1:cat ("(TImm " (p1:sym $t1) " " (p1:num $d1) " " (p1:word-text $k1) ")")))
      ((TMov $t2 $d2 $a2)
       (p1:cat ("(TMov " (p1:sym $t2) " " (p1:num $d2) " " (p1:num $a2) ")")))
      ((TBin $o3 $t3 $d3 $a3 $b3)
       (p1:cat ("(TBin " (p1:sym $o3) " " (p1:sym $t3) " " (p1:num $d3) " " (p1:num $a3) " " (p1:num $b3) ")")))
      ((TShift $o4 $t4 $d4 $a4 $s4)
       (p1:cat ("(TShift " (p1:sym $o4) " " (p1:sym $t4) " " (p1:num $d4) " " (p1:num $a4) " " (p1:num $s4) ")")))
      ((TExtract $t5 $d5 $a5 $l5 $w5)
       (p1:cat ("(TExtract " (p1:sym $t5) " " (p1:num $d5) " " (p1:num $a5) " " (p1:num $l5) " " (p1:num $w5) ")")))
      ((TCmp $c6 $t6 $d6 $a6 (OpImm $k6))
       (p1:cat ("(TCmp " (p1:sym $c6) " " (p1:sym $t6) " " (p1:num $d6) " " (p1:num $a6)
                " (OpImm " (p1:word-text $k6) "))")))
      ((TCmp $c7 $t7 $d7 $a7 (OpReg $b7))
       (p1:cat ("(TCmp " (p1:sym $c7) " " (p1:sym $t7) " " (p1:num $d7) " " (p1:num $a7)
                " (OpReg " (p1:num $b7) "))")))
      ((TSelect $t8 $d8 $k8 $a8 $b8)
       (p1:cat ("(TSelect " (p1:sym $t8) " " (p1:num $d8) " " (p1:num $k8) " " (p1:num $a8) " " (p1:num $b8) ")")))
      ((TAddr $r9 $d9 $p9 NONE $s9 $o9)
       (p1:cat ("(TAddr " (p1:num $r9) " " (p1:num $d9) " " (p1:num $p9) " NONE " (p1:num $s9) " "
                (p1:int-text $o9) ")")))
      ((TAddr $r10 $d10 $p10 (SOME $x10) $s10 $o10)
       (p1:cat ("(TAddr " (p1:num $r10) " " (p1:num $d10) " " (p1:num $p10) " (SOME " (p1:num $x10) ") "
                (p1:num $s10) " " (p1:int-text $o10) ")")))
      ((TLoad $t11 $w11 $r11 $d11 $p11 $o11)
       (p1:cat ("(TLoad " (p1:sym $t11) " " (p1:num $w11) " " (p1:num $r11) " " (p1:num $d11) " "
                (p1:num $p11) " " (p1:int-text $o11) ")")))
      ((TLoadFrame $t12 $d12 $j12)
       (p1:cat ("(TLoadFrame " (p1:sym $t12) " " (p1:num $d12) " " (p1:num $j12) ")")))
      ((TStore $w13 $r13 $p13 $o13 $v13)
       (p1:cat ("(TStore " (p1:num $w13) " " (p1:num $r13) " " (p1:num $p13) " " (p1:int-text $o13) " "
                (p1:num $v13) ")")))
      ($other (p1:reject (print-instruction $other))))))

(= (p1:stmt-pieces $s $acc)
   (case $s
     ((TSkip (cons-atom "TSkip" $acc))
      ((TInstr $i1) (cons-atom (p1:cat ("(TInstr " (p1:instr-text $i1) ")")) $acc))
      ((TSeq $a2 $b2)
       (cons-atom ")" (p1:stmt-pieces $b2 (cons-atom " " (p1:stmt-pieces $a2 (cons-atom "(TSeq " $acc))))))
      ((TIf $c3 $a3 $b3)
       (cons-atom ")" (p1:stmt-pieces $b3 (cons-atom " " (p1:stmt-pieces $a3
         (cons-atom (p1:cat ("(TIf " (p1:num $c3) " ")) $acc))))))
      ((TCountLoop $x4 $n4 $cap4 $body4)
       (cons-atom ")" (p1:stmt-pieces $body4
         (cons-atom (p1:cat ("(TCountLoop " (p1:num $x4) " " (p1:num $n4) " " (p1:num $cap4) " ")) $acc))))
      ((TEarlyLoop $x5 $n5 $k5 $cap5 $body5)
       (cons-atom ")" (p1:stmt-pieces $body5
         (cons-atom (p1:cat ("(TEarlyLoop " (p1:num $x5) " " (p1:num $n5) " " (p1:num $k5) " " (p1:num $cap5) " "))
                    $acc))))
      ((TReturn $o6 $r6) (cons-atom (p1:cat ("(TReturn " (p1:num $o6) " " (p1:num $r6) ")")) $acc))
      ((TFund $u7) (cons-atom (p1:cat ("(TFund " (p1:num $u7) ")")) $acc))
      ($other (p1:reject (print-statement $other))))))

(= (p1:stmt-text $s) (p1:cat (reverse (p1:stmt-pieces $s ()))))

(= (p1:decl-text ($m $min $align)) (p1:cat ("(" (p1:num $m) "," (p1:num $min) "," (p1:num $align) ")")))
(= (p1:init-text ($r $t $g)) (p1:cat ("(" (p1:num $r) "," (p1:sym $t) "," (p1:num $g) ")")))

(= (p1:prog-text (TProg $decls $inits $root))
   (p1:cat ("(TProg [" (str:join "; " (map-atom $decls $d (p1:decl-text $d)))
            "] [" (str:join "; " (map-atom $inits $e (p1:init-text $e)))
            "] " (p1:stmt-text $root) ")")))

; The HOL text of a resolved program.
(= (p1:hol-text (P1Program $name $prog $notes)) (p1:prog-text $prog))

; The S-expression text (tisa_print_program's other style) is the atom's own.
(= (p1:sexpr-text (P1Program $name $prog $notes)) (repr $prog))

; ---- 8. Notes ----------------------------------------------------------------------------
;
; One line per annotation, in statement order, loadable as OCaml once
; tisa_note is defined:
;   tisa_note "PROGRAM" "PATH" "KIND" "NAME" [`TEXT`; ...];;
; A block's text is its statement's HOL text.

(= (p1:note-texts $texts)
   (case $texts
     (((BlockText $core) (p1:cat ("[`" (p1:stmt-text $core) "`]")))
      ($list (p1:cat ("[" (str:join "; " (map-atom $list $t (p1:cat ("`" $t "`")))) "]"))))))

(= (p1:note-line $program (Note $path $kind $name $texts))
   (p1:cat ("tisa_note \"" (p1:sym $program) "\" \"" $path "\" \"" (p1:sym $kind) "\" \"" (p1:sym $name) "\" "
            (p1:note-texts $texts) ";;\n")))

(= (p1:notes-text (P1Program $name $prog $notes))
   (p1:cat (map-atom $notes $n (p1:note-line $name $n))))

; ---- 9. Files ------------------------------------------------------------------------------
;
; The program text without a final newline, exactly as tisa_print_program
; writes it; the notes as lines.

(= (p1:write-text $program $path) (fs:write $path (p1:hol-text $program)))
(= (p1:write-notes $program $path) (fs:write $path (p1:notes-text $program)))

; (p1:write MAKE TEXT NOTES): the one program MAKE returns, to both files.
(= (p1:write $make $text-path $notes-path)
   (let* (($program (p1:the-program $make))
          ($text (p1:write-text $program $text-path))
          ($notes (p1:write-notes $program $notes-path)))
     (Wrote $text-path $notes-path)))
