; Canonical declaration data for CeTTa's neutral ABT waist constructors.
; This equation is both importable MeTTa data and the sole source compiled into
; the native bootstrap blob.  Native code admits its right-hand side through
; the same AbtSignature/BindingDecl parser used for package extensions.

(= (abt-default-signatures)
   (AbtSignatures
     (AbtSignature App
       (Fields function argument)
       Bind0)
     (AbtSignature Pi
       (Fields domain codomain)
       (BindPi domain codomain))
     (AbtSignature Sigma
       (Fields domain codomain)
       (BindPi domain codomain))
     (AbtSignature Lam
       (Fields domain body)
       (Bind1 body))
     (AbtSignature Lam
       (Fields body)
       (Bind1 body))
     (AbtSignature Pair
       (Fields first second)
       Bind0)
     (AbtSignature Fst
       (Fields pair)
       Bind0)
     (AbtSignature Snd
       (Fields pair)
       Bind0)
     (AbtSignature Id
       (Fields carrier left right)
       Bind0)
     (AbtSignature Refl
       (Fields value)
       Bind0)
     (AbtSignature Sort
       (Fields level)
       Bind0)
     (AbtSignature LevelConst
       (Fields value)
       Bind0)
     (AbtSignature LevelParam
       (Fields parameter)
       Bind0)
     (AbtSignature LevelSucc
       (Fields level)
       Bind0)
     (AbtSignature LevelMax
       (Fields left right)
       Bind0)
     (AbtSignature ABTChainV1
       (Fields source body)
       (Bind1 body))
     (AbtSignature ABTPatternVarV1
       (Fields slot)
       Bind0)
     (AbtSignature ABTLetScopeV1
       (Fields body)
       (Bind1 body))
     (AbtSignature ABTLetV1
       (Fields pattern source scope)
       Bind0)
     (AbtSignature ArgsCons
       (Fields argument rest)
       Bind0)
     (AbtSignature Case
       (Fields constructor body)
       Bind0)
     (AbtSignature Cases
       (Fields case rest)
       Bind0)
     (AbtSignature IndG
       (Fields name parameters motive cases indices scrutinee)
       Bind0)
     (AbtSignature NatRec
       (Fields motive zero-case successor-case scrutinee)
       Bind0)))
