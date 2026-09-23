import Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.FormationSensitiveTyping
import Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.SigmaConversionBoundary
import Mettapedia.TypeTheory.Calculi.CumulativePiSigmaId.Checking.NativeJudgmentReplayCoherence

/-!
General laws for a certified transformation on the candidate syntax.

A step consumes a value and evidence that a supplied family holds of that
value, and returns a value with evidence that the same family holds of the
computed value. The reflexivity step is that construction at the reflexivity
family. Composition shares one computed package. The second projection is
checked at the family instantiated by `fst` of that package; that type
converts to the family instantiated by the computed first component.
Certificate codes for one judgment are not unique, so these laws do not
quantify over a chosen code. These laws do not identify a hosted MeTTa
proof article with a candidate term.
-/

open Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation
open FormationSensitive

set_option autoImplicit false

namespace Cetta.Prime.CertifiedTransformLaws

variable {Head : Type} {n : Nat}

/-- `Id A y y` in the context extended by `y : A`. -/
def reflFamily (A : Tm Head n) : Tm Head (n + 1) :=
  .id (rename wk A) (.var 0) (.var 0)

/-- `Σ y : A. Id A y y`. -/
def reflSigma (A : Tm Head n) : Tm Head n :=
  .sigma A (reflFamily A)

/-- `(x : A) → (e : Id A x x) → Σ y : A. Id A y y`. -/
def stepType (A : Tm Head n) : Tm Head n :=
  .pi A (.pi (reflFamily A) (rename wk (rename wk (reflSigma A))))

/-- `Σ y : A. P y`, for a family already formed in the context extended by `y : A`. -/
def evidenceFamily (A : Tm Head n) (P : Tm Head (n + 1)) : Tm Head n :=
  .sigma A P

/-- `(x : A) → (e : P x) → Σ y : A. P y`. The family is supplied. -/
def stepOver (A : Tm Head n) (P : Tm Head (n + 1)) : Tm Head n :=
  .pi A (.pi P (rename wk (rename wk (evidenceFamily A P))))

/-- `step2 (fst pack) (snd pack)`, with `pack` the newest variable. -/
def sharedBody (step2 : Tm Head n) : Tm Head (n + 1) :=
  .app (.app (rename wk step2) (.fst (.var 0))) (.snd (.var 0))

/-- One shared use of `step1` at a possibly computed argument, then `step2`. -/
def shared (step1 step2 x evidence : Tm Head n) : Tm Head n :=
  .app (.lam (sharedBody step2)) (.app (.app step1 x) evidence)

variable {R : Rules Head}

theorem reflFamily_typed {Γ : Ctx Head n} {A : Tm Head n} {u : Head}
    (carrier : Typing R Γ A (.head u)) (isUniv : R.isUniverse u) :
    Typing R (.snoc Γ A) (reflFamily A) (.head u) := by
  exact Typing.idForm
    (by simpa [reflFamily, rename] using carrier.weaken (extension := A))
    isUniv
    (by simpa [reflFamily, Ctx.lookup_snoc_zero] using
      (Typing.var (R := R) (Γ := .snoc Γ A) (0 : Fin (n + 1))))
    (by simpa [reflFamily, Ctx.lookup_snoc_zero] using
      (Typing.var (R := R) (Γ := .snoc Γ A) (0 : Fin (n + 1))))

theorem reflSigma_typed {Γ : Ctx Head n} {A : Tm Head n} {u : Head}
    (carrier : Typing R Γ A (.head u)) (isUniv : R.isUniverse u)
    (joined : R.join u u u) :
    Typing R Γ (reflSigma A) (.head u) :=
  Typing.sigmaForm carrier isUniv (reflFamily_typed carrier isUniv) isUniv joined

theorem stepType_typed {Γ : Ctx Head n} {A : Tm Head n} {u : Head}
    (carrier : Typing R Γ A (.head u)) (isUniv : R.isUniverse u)
    (joined : R.join u u u) :
    Typing R Γ (stepType A) (.head u) := by
  have family := reflFamily_typed carrier isUniv
  have sigma := reflSigma_typed carrier isUniv joined
  have result :
      Typing R (.snoc (.snoc Γ A) (reflFamily A))
        (rename wk (rename wk (reflSigma A))) (.head u) := by
    simpa [rename] using (sigma.weaken (extension := A)).weaken (extension := reflFamily A)
  have inner := Typing.piForm family isUniv result isUniv joined
  exact Typing.piForm carrier isUniv inner isUniv joined

/-- The reflexivity step is the supplied-family step at the reflexivity family. -/
theorem stepType_is_stepOver (A : Tm Head n) :
    stepType A = stepOver A (reflFamily A) := by
  rfl

theorem stepOver_typed {Γ : Ctx Head n} {A : Tm Head n} {P : Tm Head (n + 1)} {u : Head}
    (carrier : Typing R Γ A (.head u))
    (family : Typing R (.snoc Γ A) P (.head u))
    (isUniv : R.isUniverse u) (joined : R.join u u u) :
    Typing R Γ (stepOver A P) (.head u) := by
  have packed := Typing.sigmaForm carrier isUniv family isUniv joined
  have result :
      Typing R (.snoc (.snoc Γ A) P)
        (rename wk (rename wk (evidenceFamily A P))) (.head u) := by
    simpa [rename, evidenceFamily] using
      (packed.weaken (extension := A)).weaken (extension := P)
  have inner := Typing.piForm family isUniv result isUniv joined
  exact Typing.piForm carrier isUniv inner isUniv joined

theorem subst_stepOver {m : Nat} (σ : Sub Head n m)
    (A : Tm Head n) (P : Tm Head (n + 1)) :
    subst σ (stepOver A P) = stepOver (subst σ A) (subst (liftSub σ) P) := by
  have shiftOne {k p : Nat} (τ : Sub Head k p) (term : Tm Head k) :
      subst (liftSub τ) (rename wk term) = rename wk (subst τ term) := by
    calc
      subst (liftSub τ) (rename wk term) =
          subst (fun i => liftSub τ (wk i)) term := by
            simpa using subst_rename (sigma := liftSub τ) (rho := wk) (term := term)
      _ = subst (fun i => rename wk (τ i)) term := by
            apply subst_ext
            intro i
            rfl
      _ = rename wk (subst τ term) := by
            symm
            simpa using rename_subst (rho := wk) (sigma := τ) (term := term)
  have shiftTwo (term : Tm Head n) :
      subst (liftSub (liftSub σ)) (rename wk (rename wk term)) =
        rename wk (rename wk (subst σ term)) := by
    calc
      subst (liftSub (liftSub σ)) (rename wk (rename wk term)) =
          rename wk (subst (liftSub σ) (rename wk term)) := by
            simpa using shiftOne (liftSub σ) (rename wk term)
      _ = rename wk (rename wk (subst σ term)) := by
            rw [shiftOne σ term]
  simp only [stepOver, evidenceFamily, subst, shiftTwo]

theorem step_conv {root : RootComputation Head} {headEq : Head → Head → Prop}
    {a b : Tm Head n} (step : StepCore root headEq a b) : Conv headEq a b root :=
  Relation.EqvGen.rel a b step

theorem evidence_index_converts {root : RootComputation Head} {headEq : Head → Head → Prop}
    (A a b : Tm Head n) :
    Conv headEq
      (Tm.id A (.fst (.pair a b)) (.fst (.pair a b)))
      (Tm.id A a a) root :=
  Relation.EqvGen.trans
    (Tm.id A (.fst (.pair a b)) (.fst (.pair a b)))
    (Tm.id A a (.fst (.pair a b)))
    (Tm.id A a a)
    (step_conv (StepCore.congIdLeft (b := .fst (.pair a b)) (StepCore.betaSigmaFst a b)))
    (step_conv (StepCore.congIdRight (a := a) (StepCore.betaSigmaFst a b)))

theorem snd_typed_at_computed_component {Γ : Ctx Head n}
    {A a b : Tm Head n} {B : Tm Head (n + 1)} {u : Head}
    (sigma : Typing R Γ (.sigma A B) (.head u)) (isUniv : R.isUniverse u)
    (first : Typing R Γ a A) (second : Typing R Γ b (inst0 a B))
    (target : Typing R Γ (inst0 a B) (.head u))
    (index : Conv R.headEq (inst0 (.fst (.pair a b)) B) (inst0 a B) R.computation) :
    Typing R Γ (.snd (.pair a b)) (inst0 a B) := by
  have packed := Typing.pairIntro sigma isUniv first second
  exact Typing.conv (Typing.sndElim packed) target isUniv index

/-- A conversion lifts through one term constructor. -/
private theorem conv_map {k : Nat} {root : RootComputation Head} {headEq : Head → Head → Prop}
    {wrap : Tm Head n → Tm Head k}
    (preserve : ∀ {x x' : Tm Head n}, StepCore root headEq x x' →
      StepCore root headEq (wrap x) (wrap x'))
    {x x' : Tm Head n} (conversion : Conv headEq x x' root) :
    Conv headEq (wrap x) (wrap x') root := by
  induction conversion with
  | rel _ _ step => exact Relation.EqvGen.rel _ _ (preserve step)
  | refl _ => exact Relation.EqvGen.refl _
  | symm _ _ _ ih => exact Relation.EqvGen.symm _ _ ih
  | trans _ _ _ _ _ ih₁ ih₂ => exact Relation.EqvGen.trans _ _ _ ih₁ ih₂

private theorem conv_pi_dom {root : RootComputation Head} {headEq : Head → Head → Prop}
    {A A' : Tm Head n} {B : Tm Head (n + 1)}
    (conversion : Conv headEq A A' root) :
    Conv headEq (.pi A B) (.pi A' B) root :=
  conv_map (fun step => StepCore.congPiDom step) conversion

private theorem conv_pi_cod {root : RootComputation Head} {headEq : Head → Head → Prop}
    {A : Tm Head n} {B B' : Tm Head (n + 1)}
    (conversion : Conv headEq B B' root) :
    Conv headEq (.pi A B) (.pi A B') root :=
  conv_map (fun step => StepCore.congPiCod step) conversion

private theorem conv_sigma_dom {root : RootComputation Head} {headEq : Head → Head → Prop}
    {A A' : Tm Head n} {B : Tm Head (n + 1)}
    (conversion : Conv headEq A A' root) :
    Conv headEq (.sigma A B) (.sigma A' B) root :=
  conv_map (fun step => StepCore.congSigmaDom step) conversion

private theorem conv_sigma_cod {root : RootComputation Head} {headEq : Head → Head → Prop}
    {A : Tm Head n} {B B' : Tm Head (n + 1)}
    (conversion : Conv headEq B B' root) :
    Conv headEq (.sigma A B) (.sigma A B') root :=
  conv_map (fun step => StepCore.congSigmaCod step) conversion

private theorem conv_id_ty {root : RootComputation Head} {headEq : Head → Head → Prop}
    {A A' a b : Tm Head n} (conversion : Conv headEq A A' root) :
    Conv headEq (.id A a b) (.id A' a b) root :=
  conv_map (fun step => StepCore.congIdTy step) conversion

private theorem conv_id_left {root : RootComputation Head} {headEq : Head → Head → Prop}
    {A a a' b : Tm Head n} (conversion : Conv headEq a a' root) :
    Conv headEq (.id A a b) (.id A a' b) root :=
  conv_map (fun step => StepCore.congIdLeft step) conversion

private theorem conv_id_right {root : RootComputation Head} {headEq : Head → Head → Prop}
    {A a b b' : Tm Head n} (conversion : Conv headEq b b' root) :
    Conv headEq (.id A a b) (.id A a b') root :=
  conv_map (fun step => StepCore.congIdRight step) conversion

private theorem conv_lam {root : RootComputation Head} {headEq : Head → Head → Prop}
    {body body' : Tm Head (n + 1)} (conversion : Conv headEq body body' root) :
    Conv headEq (.lam body) (.lam body') root :=
  conv_map (fun step => StepCore.congLam step) conversion

private theorem conv_app_fun {root : RootComputation Head} {headEq : Head → Head → Prop}
    {g g' a : Tm Head n} (conversion : Conv headEq g g' root) :
    Conv headEq (.app g a) (.app g' a) root :=
  conv_map (fun step => StepCore.congAppFun step) conversion

private theorem conv_app_arg {root : RootComputation Head} {headEq : Head → Head → Prop}
    {g a a' : Tm Head n} (conversion : Conv headEq a a' root) :
    Conv headEq (.app g a) (.app g a') root :=
  conv_map (fun step => StepCore.congAppArg step) conversion

private theorem conv_pair_fst {root : RootComputation Head} {headEq : Head → Head → Prop}
    {a a' b : Tm Head n} (conversion : Conv headEq a a' root) :
    Conv headEq (.pair a b) (.pair a' b) root :=
  conv_map (fun step => StepCore.congPairFst step) conversion

private theorem conv_pair_snd {root : RootComputation Head} {headEq : Head → Head → Prop}
    {a b b' : Tm Head n} (conversion : Conv headEq b b' root) :
    Conv headEq (.pair a b) (.pair a b') root :=
  conv_map (fun step => StepCore.congPairSnd step) conversion

private theorem conv_fst {root : RootComputation Head} {headEq : Head → Head → Prop}
    {p p' : Tm Head n} (conversion : Conv headEq p p' root) :
    Conv headEq (.fst p) (.fst p') root :=
  conv_map (fun step => StepCore.congFst step) conversion

private theorem conv_snd {root : RootComputation Head} {headEq : Head → Head → Prop}
    {p p' : Tm Head n} (conversion : Conv headEq p p' root) :
    Conv headEq (.snd p) (.snd p') root :=
  conv_map (fun step => StepCore.congSnd step) conversion

private theorem conv_refl_tm {root : RootComputation Head} {headEq : Head → Head → Prop}
    {a a' : Tm Head n} (conversion : Conv headEq a a' root) :
    Conv headEq (.refl a) (.refl a') root :=
  conv_map (fun step => StepCore.congRefl step) conversion

private theorem conv_trans_bin {root : RootComputation Head} {headEq : Head → Head → Prop}
    {a b c : Tm Head n}
    (left : Conv headEq a b root) (right : Conv headEq b c root) :
    Conv headEq a c root :=
  Relation.EqvGen.trans a b c left right

/-- Pointwise conversion of a substitution survives capture-avoiding substitution. -/
private theorem subst_conv {root : RootComputation Head} {headEq : Head → Head → Prop}
    {m : Nat} (σ τ : Sub Head n m) (term : Tm Head n)
    (pointwise : ∀ i, Conv headEq (σ i) (τ i) root) :
    Conv headEq (subst σ term) (subst τ term) root := by
  induction term generalizing m with
  | var i => simpa [subst] using pointwise i
  | const _ => exact Relation.EqvGen.refl _
  | head _ => exact Relation.EqvGen.refl _
  | pi domain codomain ihDomain ihCodomain =>
      exact conv_trans_bin
        (conv_pi_dom (ihDomain σ τ pointwise))
        (conv_pi_cod (ihCodomain (liftSub σ) (liftSub τ) (by
          intro i
          refine Fin.cases ?_ ?_ i
          · exact Relation.EqvGen.refl _
          · intro j
            simpa [liftSub] using (pointwise j).renameTerms wk)))
  | sigma domain codomain ihDomain ihCodomain =>
      exact conv_trans_bin
        (conv_sigma_dom (ihDomain σ τ pointwise))
        (conv_sigma_cod (ihCodomain (liftSub σ) (liftSub τ) (by
          intro i
          refine Fin.cases ?_ ?_ i
          · exact Relation.EqvGen.refl _
          · intro j
            simpa [liftSub] using (pointwise j).renameTerms wk)))
  | id carrier left right ihCarrier ihLeft ihRight =>
      exact conv_trans_bin
        (conv_trans_bin
          (conv_id_ty (ihCarrier σ τ pointwise))
          (conv_id_left (ihLeft σ τ pointwise)))
        (conv_id_right (ihRight σ τ pointwise))
  | lam body ih =>
      exact conv_lam (ih (liftSub σ) (liftSub τ) (by
        intro i
        refine Fin.cases ?_ ?_ i
        · exact Relation.EqvGen.refl _
        · intro j
          simpa [liftSub] using (pointwise j).renameTerms wk))
  | app function argument ihFunction ihArgument =>
      exact conv_trans_bin
        (conv_app_fun (ihFunction σ τ pointwise))
        (conv_app_arg (ihArgument σ τ pointwise))
  | pair left right ihLeft ihRight =>
      exact conv_trans_bin
        (conv_pair_fst (ihLeft σ τ pointwise))
        (conv_pair_snd (ihRight σ τ pointwise))
  | fst pair ih => exact conv_fst (ih σ τ pointwise)
  | snd pair ih => exact conv_snd (ih σ τ pointwise)
  | refl term ih => exact conv_refl_tm (ih σ τ pointwise)

/-- `B (fst ⟨a, b⟩)` converts to `B a` by the sigma computation, for any family `B`. -/
theorem family_index_converts {root : RootComputation Head} {headEq : Head → Head → Prop}
    (B : Tm Head (n + 1)) (a b : Tm Head n) :
    Conv headEq (inst0 (.fst (.pair a b)) B) (inst0 a B) root := by
  unfold inst0
  refine subst_conv (subst0 (.fst (.pair a b))) (subst0 a) B ?_
  intro i
  refine Fin.cases ?_ ?_ i
  · simpa [subst0] using
      (Relation.EqvGen.rel _ _ (StepCore.betaSigmaFst (headEq := headEq) (root := root) a b))
  · intro _
    exact Relation.EqvGen.refl _

/-- The second projection is typed at the family of the computed first component.
The index conversion is the sigma reduction above, not a separate premise. -/
theorem snd_typed_from_reduction {Γ : Ctx Head n}
    {A a b : Tm Head n} {B : Tm Head (n + 1)} {u : Head}
    (sigma : Typing R Γ (.sigma A B) (.head u)) (isUniv : R.isUniverse u)
    (first : Typing R Γ a A) (second : Typing R Γ b (inst0 a B))
    (target : Typing R Γ (inst0 a B) (.head u)) :
    Typing R Γ (.snd (.pair a b)) (inst0 a B) :=
  snd_typed_at_computed_component sigma isUniv first second target
    (family_index_converts (root := R.computation) (headEq := R.headEq) B a b)

private theorem weaken_subst {k p : Nat} (τ : Sub Head k p) (term : Tm Head k) :
    subst (liftSub τ) (rename wk term) = rename wk (subst τ term) := by
  calc
    subst (liftSub τ) (rename wk term) =
        subst (fun i => liftSub τ (wk i)) term := by
          simpa using subst_rename (sigma := liftSub τ) (rho := wk) (term := term)
    _ = subst (fun i => rename wk (τ i)) term := by
          apply subst_ext
          intro i
          rfl
    _ = rename wk (subst τ term) := by
          symm
          simpa using rename_subst (rho := wk) (sigma := τ) (term := term)

/-- Applying a supplied-family step returns a package of that same family. -/
theorem step_application_type (x : Tm Head n) (P : Tm Head (n + 1))
    (family : Tm Head n) :
    inst0 x (.pi P (rename wk (rename wk family))) =
      .pi (inst0 x P) (rename wk family) := by
  have shift :
      subst (liftSub (subst0 x)) (rename wk (rename wk family)) = rename wk family := by
    calc
      subst (liftSub (subst0 x)) (rename wk (rename wk family)) =
          rename wk (subst (subst0 x) (rename wk family)) := by
            simpa using weaken_subst (subst0 x) (rename wk family)
      _ = rename wk family := by
            rw [show subst (subst0 x) (rename wk family) = inst0 x (rename wk family) from rfl,
              inst0_rename_wk]
  simp only [inst0, subst, shift]

theorem step_application_typed {Γ : Ctx Head n}
    {A x evidence step : Tm Head n} {P : Tm Head (n + 1)}
    (stepTyped : Typing R Γ step (stepOver A P))
    (value : Typing R Γ x A)
    (given : Typing R Γ evidence (inst0 x P)) :
    Typing R Γ (.app (.app step x) evidence) (evidenceFamily A P) := by
  have appliedValue := Typing.appElim stepTyped value
  have appliedValue' :
      Typing R Γ (.app step x)
        (inst0 x (.pi P (rename wk (rename wk (evidenceFamily A P))))) := by
    simpa [stepOver] using appliedValue
  have appliedDomain :=
    (step_application_type x P (evidenceFamily A P)) ▸ appliedValue'
  have appliedEvidence := Typing.appElim appliedDomain given
  simpa [inst0_rename_wk] using appliedEvidence

/-- The shared continuation is typed at the supplied family, not only at reflexivity. -/
theorem shared_over_family_typed {Γ : Ctx Head n}
    {A : Tm Head n} {P : Tm Head (n + 1)} {package continuation : Tm Head n}
    (body : Typing R (.snoc Γ (evidenceFamily A P)) (sharedBody continuation)
      (rename wk (evidenceFamily A P)))
    (packed : Typing R Γ package (evidenceFamily A P)) :
    Typing R Γ (inst0 package (sharedBody continuation)) (evidenceFamily A P) := by
  simpa [inst0_rename_wk] using body.instantiate packed

/-- One successor clause of the iterator: share the package, then continue.
This is the candidate-syntax term of the authored `(lam pack (continuation (fst pack) (snd pack))) (step x e)`. -/
theorem iter_successor_typed {Γ : Ctx Head n}
    {A x evidence step continuation : Tm Head n} {P : Tm Head (n + 1)} {u : Head}
    (sigmaTyped : Typing R Γ (evidenceFamily A P) (.head u))
    (isUniv : R.isUniverse u) (joined : R.join u u u)
    (stepTyped : Typing R Γ step (stepOver A P))
    (value : Typing R Γ x A)
    (given : Typing R Γ evidence (inst0 x P))
    (body : Typing R (.snoc Γ (evidenceFamily A P)) (sharedBody continuation)
      (rename wk (evidenceFamily A P))) :
    Typing R Γ (shared step continuation x evidence) (evidenceFamily A P) := by
  have package := step_application_typed stepTyped value given
  have resultType :
      Typing R (.snoc Γ (evidenceFamily A P))
        (rename wk (evidenceFamily A P)) (.head u) := by
    simpa [rename] using sigmaTyped.weaken (extension := evidenceFamily A P)
  have arrow := Typing.piForm sigmaTyped isUniv resultType isUniv joined
  have closure := Typing.lamIntro arrow isUniv body
  simpa [shared, inst0_rename_wk] using Typing.appElim closure package

/-- The body of `transportCert`: pair the computed value with the moved evidence. -/
def transported (h move x evidence : Tm Head n) : Tm Head n :=
  .pair (.app h x) (.app (.app move x) evidence)

/-- A move whose result is already `P (h x)` packs into the supplied family.
The identity eliminator is one such move in the runtime. Its iota rule is a
declared computation, not one of the primitive steps. -/
theorem transport_pack_typed {Γ : Ctx Head n}
    {A h x evidence move : Tm Head n} {P : Tm Head (n + 1)} {u : Head}
    (sigmaTyped : Typing R Γ (.sigma A P) (.head u)) (isUniv : R.isUniverse u)
    (function : Typing R Γ h (.pi A (rename wk A)))
    (value : Typing R Γ x A)
    (moved : Typing R Γ (.app (.app move x) evidence) (inst0 (.app h x) P)) :
    Typing R Γ (transported h move x evidence) (.sigma A P) := by
  have computedValue := Typing.appElim function value
  have computedValue' : Typing R Γ (.app h x) A := by
    simpa [inst0_rename_wk] using computedValue
  exact Typing.pairIntro sigmaTyped isUniv computedValue' moved

/-- Keeping the supplied evidence does not manufacture reflexivity.
This is the candidate term of `keepCert`. -/
theorem kept_evidence_is_not_reflexivity :
    (.snd (.pair (.var 1) (.var 0)) : Tm Head (n + 2)) ≠ .refl (.var 1) := by
  intro equal
  cases equal

/-- A family whose endpoint is a projection is not the reflexivity family.
`stepType_typed` therefore does not type the zero-addition iterator. -/
def shiftedFamily (A : Tm Head n) : Tm Head (n + 1) :=
  .id (rename wk A) (.fst (.var 0)) (.var 0)

theorem shifted_family_not_refl (A : Tm Head n) :
    shiftedFamily A ≠ reflFamily A := by
  intro equal
  simp [shiftedFamily, reflFamily] at equal

theorem shifted_step_is_not_reflexivity_step (A : Tm Head n) :
    stepOver A (shiftedFamily A) ≠ stepType A := by
  intro equal
  simp [stepOver, stepType, evidenceFamily, reflSigma, shiftedFamily, reflFamily] at equal

theorem shared_beta {headEq : Head → Head → Prop}
    (step1 step2 x evidence : Tm Head n) :
    Step headEq (shared step1 step2 x evidence)
      (inst0 (.app (.app step1 x) evidence) (sharedBody step2)) :=
  Step.betaPi _ _

theorem shared_contractum_typed {Γ : Ctx Head n} {A package step2 : Tm Head n}
    (body : Typing R (.snoc Γ (reflSigma A)) (sharedBody step2)
      (rename wk (reflSigma A)))
    (packed : Typing R Γ package (reflSigma A)) :
    Typing R Γ (inst0 package (sharedBody step2)) (reflSigma A) := by
  simpa [inst0_rename_wk] using body.instantiate packed

theorem subst_sharedBody {m : Nat} (σ : Sub Head n m) (step2 : Tm Head n) :
    subst (liftSub σ) (sharedBody step2) = sharedBody (subst σ step2) := by
  simp only [sharedBody, subst]
  have shift :
      subst (liftSub σ) (rename wk step2) = rename wk (subst σ step2) := by
    calc
      subst (liftSub σ) (rename wk step2) =
          subst (fun i => liftSub σ (wk i)) step2 := by
            simpa using subst_rename (sigma := liftSub σ) (rho := wk) (term := step2)
      _ = subst (fun i => rename wk (σ i)) step2 := by
            apply subst_ext
            intro i
            rfl
      _ = rename wk (subst σ step2) := by
            symm
            simpa using rename_subst (rho := wk) (sigma := σ) (term := step2)
  simp [shift, liftSub]

theorem subst_shared {m : Nat} (σ : Sub Head n m)
    (step1 step2 x evidence : Tm Head n) :
    subst σ (shared step1 step2 x evidence) =
      shared (subst σ step1) (subst σ step2) (subst σ x) (subst σ evidence) := by
  simp only [shared, subst, subst_sharedBody]

open Mettapedia.TypeTheory.Calculi.CumulativePiSigmaId.NativeJudgmentReplay

/-- Two accepted certificates of one reflexivity judgment stay distinct.
Uniqueness of intermediate certificates is false; the typing and step laws
above do not assume it. -/

theorem certificate_uniqueness_fails :
    CoherenceControls.directProof ≠ CoherenceControls.explicitConversionProof ∧
      check Controls.context (.refl (.var 0)) CoherenceControls.identityType
        Controls.contextCode CoherenceControls.directProof = true ∧
      check Controls.context (.refl (.var 0)) CoherenceControls.identityType
        Controls.contextCode CoherenceControls.explicitConversionProof = true := by
  refine ⟨?_, CoherenceControls.distinct_proofs_both_checked⟩
  intro equal
  cases equal

/-!
Reflexivity does not inhabit an identity whose left endpoint is an inert
application of a constant. Declared computation is empty, so that application
does not reduce to the neutral index. Transport of evidence already typed at
the computed index is the replacement; its term is a pair, not reflexivity.
-/

variable {R : Rules Head}

open ConversionCoherence

private theorem step_var_impossible
    (empty : R.computation = RootComputation.empty)
    {index : Fin n} {target : Tm Head n}
    (step : StepCore R.computation R.headEq (.var index) target) : False := by
  cases step with
  | root equation =>
      rw [empty] at equation
      exact equation.elim

private theorem stepStar_var
    (empty : R.computation = RootComputation.empty)
    {index : Fin n} {target : Tm Head n}
    (steps : StepStar R (.var index) target) : target = .var index := by
  induction steps with
  | refl => rfl
  | tail _ final ih =>
      subst ih
      exact (step_var_impossible empty final).elim

private theorem step_const_impossible
    (empty : R.computation = RootComputation.empty)
    {name : DeclName} {target : Tm Head n}
    (step : StepCore R.computation R.headEq (.const name) target) : False := by
  cases step with
  | root equation =>
      rw [empty] at equation
      exact equation.elim

private theorem step_app_const
    (empty : R.computation = RootComputation.empty)
    {name : DeclName} {argument target : Tm Head n}
    (step : StepCore R.computation R.headEq (.app (.const name) argument) target) :
    ∃ argument', target = .app (.const name) argument' ∧
      StepStar R argument argument' := by
  cases step with
  | congAppArg inner =>
      exact ⟨_, rfl, .tail .refl inner⟩
  | congAppFun inner =>
      exact (step_const_impossible empty inner).elim
  | root equation =>
      rw [empty] at equation
      exact equation.elim

private theorem step_neutral_index
    (empty : R.computation = RootComputation.empty)
    {operation : DeclName} {argument target : Tm Head (n + 1)}
    (step : StepCore R.computation R.headEq
        (.app (.app (.const operation) argument) (.var 0)) target) :
    ∃ argument', target = .app (.app (.const operation) argument') (.var 0) ∧
      StepStar R argument argument' := by
  cases step with
  | congAppFun inner =>
      obtain ⟨argument', rfl, steps⟩ := step_app_const empty inner
      exact ⟨argument', rfl, steps⟩
  | congAppArg inner =>
      exact (step_var_impossible empty inner).elim
  | root equation =>
      rw [empty] at equation
      exact equation.elim

private theorem stepStar_neutral_index
    (empty : R.computation = RootComputation.empty)
    {operation : DeclName} {argument target : Tm Head (n + 1)}
    (steps : StepStar R
        (.app (.app (.const operation) argument) (.var 0)) target) :
    ∃ argument', target = .app (.app (.const operation) argument') (.var 0) ∧
      StepStar R argument argument' := by
  induction steps with
  | refl => exact ⟨argument, rfl, .refl⟩
  | tail _ final ih =>
      obtain ⟨argument', rfl, argumentSteps⟩ := ih
      obtain ⟨argument'', rfl, last⟩ := step_neutral_index empty final
      exact ⟨argument'', rfl, argumentSteps.trans last⟩

/-- An application of a constant to a neutral index does not convert to that
index when no declared computation rewrites the constant. -/
theorem neutral_index_application_does_not_convert
    (empty : R.computation = RootComputation.empty)
    (symmetric : Std.Symm R.headEq)
    (operation : DeclName) (argument : Tm Head n) :
    ¬ Conv R.headEq
        (.app (.app (.const operation) (rename wk argument)) (.var 0))
        (.var 0) R.computation := by
  intro conversion
  obtain ⟨common, leftSteps, rightSteps⟩ :=
    EmptyRootConversion.churchRosser R empty symmetric conversion
  have rightShape : common = .var 0 := stepStar_var empty rightSteps
  obtain ⟨_, leftShape, _⟩ := stepStar_neutral_index empty leftSteps
  rw [rightShape] at leftShape
  cases leftShape

private theorem step_identity
    (empty : R.computation = RootComputation.empty)
    {carrier left right target : Tm Head n}
    (step : StepCore R.computation R.headEq (.id carrier left right) target) :
    ∃ carrier' left' right', target = .id carrier' left' right' ∧
      StepStar R carrier carrier' ∧ StepStar R left left' ∧
      StepStar R right right' := by
  cases step with
  | congIdTy inner =>
      exact ⟨_, _, _, rfl, .tail .refl inner, .refl, .refl⟩
  | congIdLeft inner =>
      exact ⟨_, _, _, rfl, .refl, .tail .refl inner, .refl⟩
  | congIdRight inner =>
      exact ⟨_, _, _, rfl, .refl, .refl, .tail .refl inner⟩
  | root equation =>
      rw [empty] at equation
      exact equation.elim

private theorem stepStar_identity
    (empty : R.computation = RootComputation.empty)
    {carrier left right target : Tm Head n}
    (steps : StepStar R (.id carrier left right) target) :
    ∃ carrier' left' right', target = .id carrier' left' right' ∧
      StepStar R carrier carrier' ∧ StepStar R left left' ∧
      StepStar R right right' := by
  induction steps with
  | refl => exact ⟨carrier, left, right, rfl, .refl, .refl, .refl⟩
  | tail _ final ih =>
      obtain ⟨carrier', left', right', rfl, carrierSteps, leftSteps, rightSteps⟩ := ih
      obtain ⟨carrier'', left'', right'', rfl, carrierLast, leftLast, rightLast⟩ :=
        step_identity empty final
      exact ⟨carrier'', left'', right'', rfl,
        carrierSteps.trans carrierLast, leftSteps.trans leftLast,
        rightSteps.trans rightLast⟩

private theorem identity_left_endpoint
    (empty : R.computation = RootComputation.empty)
    (symmetric : Std.Symm R.headEq)
    {carrier carrier' left left' right right' : Tm Head n}
    (conversion : Conv R.headEq (.id carrier left right)
      (.id carrier' left' right') R.computation) :
    Conv R.headEq left left' R.computation := by
  obtain ⟨_, leftPath, rightPath⟩ :=
    EmptyRootConversion.churchRosser R empty symmetric conversion
  obtain ⟨carrier₁, left₁, right₁, leftShape, _, leftSteps, _⟩ :=
    stepStar_identity empty leftPath
  obtain ⟨carrier₂, left₂, right₂, rightShape, _, rightSteps, _⟩ :=
    stepStar_identity empty rightPath
  have joined : Tm.id carrier₁ left₁ right₁ = Tm.id carrier₂ left₂ right₂ :=
    leftShape.symm.trans rightShape
  obtain ⟨_, leftEqual, _⟩ := Tm.id.inj joined
  rw [← leftEqual] at rightSteps
  exact Relation.EqvGen.trans _ _ _
    (stepStar_implies_conv leftSteps)
    (Relation.EqvGen.symm _ _ (stepStar_implies_conv rightSteps))

private theorem identity_not_head
    (empty : R.computation = RootComputation.empty)
    (symmetric : Std.Symm R.headEq)
    {carrier left right : Tm Head n} {head : Head} :
    ¬ Conv R.headEq (.id carrier left right) (.head head) R.computation := by
  intro conversion
  obtain ⟨common, identitySteps, headSteps⟩ :=
    EmptyRootConversion.churchRosser R empty symmetric conversion
  obtain ⟨_, _, _, identityShape, _, _, _⟩ := stepStar_identity empty identitySteps
  obtain ⟨_, headShape⟩ :=
    stepStar_head_shape (EmptyRootConversion.rootPiHeadNeutral R empty) headSteps
  rw [identityShape] at headShape
  cases headShape

/-- The identity at a neutral index of an inert constant application. -/
def neutralComputedIdentity (carrier : Tm Head n) (operation : DeclName)
    (argument : Tm Head n) : Tm Head (n + 1) :=
  .id (rename wk carrier)
    (.app (.app (.const operation) (rename wk argument)) (.var 0))
    (.var 0)

theorem neutral_computed_identity_does_not_convert
    (empty : R.computation = RootComputation.empty)
    (symmetric : Std.Symm R.headEq)
    (carrier argument : Tm Head n) (operation : DeclName) :
    ¬ Conv R.headEq (neutralComputedIdentity carrier operation argument)
        (reflFamily carrier) R.computation := by
  intro conversion
  have endpoints := identity_left_endpoint empty symmetric conversion
  exact neutral_index_application_does_not_convert empty symmetric operation
    argument endpoints

private theorem reflGenerationAux {Γ : Ctx Head n} {term displayed : Tm Head n}
    (typing : Typing R Γ term displayed) :
    ∀ {witness : Tm Head n}, term = .refl witness →
      ∃ carrier, Typing R Γ witness carrier ∧
        TypeAdjustment R (.id carrier witness witness) displayed := by
  induction typing with
  | reflIntro termTyped =>
      intro witness equality
      cases equality
      exact ⟨_, termTyped, .refl _⟩
  | cumul _ order ih =>
      intro witness equality
      obtain ⟨carrier, termTyped, adjustment⟩ := ih equality
      exact ⟨carrier, termTyped, .trans adjustment (.cumulative order)⟩
  | conv _ _ _ conversion ih _ =>
      intro witness equality
      obtain ⟨carrier, termTyped, adjustment⟩ := ih equality
      exact ⟨carrier, termTyped, .trans adjustment (.conversion conversion)⟩
  | _ =>
      intro witness equality
      cases equality

private theorem reflGeneration {Γ : Ctx Head n} {witness displayed : Tm Head n}
    (typing : Typing R Γ (.refl witness) displayed) :
    ∃ carrier, Typing R Γ witness carrier ∧
      TypeAdjustment R (.id carrier witness witness) displayed :=
  reflGenerationAux typing rfl

/-- Reflexivity is not evidence for the inert identity at a neutral index.
The hypotheses are empty declared computation and symmetric head equality.
The replacement is `transport_pack_typed`: evidence already typed at the
computed index, packed with that computed value. -/
theorem neutral_refl_is_not_identity_evidence
    {Γ : Ctx Head (n + 1)} {carrier argument : Tm Head n} {operation : DeclName}
    (empty : R.computation = RootComputation.empty)
    (symmetric : Std.Symm R.headEq)
    (typing : Typing R Γ (.refl (.var 0))
      (neutralComputedIdentity carrier operation argument)) : False := by
  obtain ⟨_, _, adjustment⟩ := reflGeneration typing
  have conversion :=
    TypeAdjustment.toConvOfTargetDisjointHeads adjustment
      (fun head conv => identity_not_head empty symmetric conv)
  have endpoints := identity_left_endpoint empty symmetric conversion
  exact neutral_index_application_does_not_convert empty symmetric operation
    argument (.symm _ _ endpoints)

theorem transported_evidence_is_not_reflexivity
    (operation move index evidence : Tm Head n) :
    transported operation move index evidence ≠ .refl index := by
  intro equal
  cases equal

#print axioms reflFamily_typed
#print axioms reflSigma_typed
#print axioms stepType_typed
#print axioms stepType_is_stepOver
#print axioms stepOver_typed
#print axioms subst_stepOver
#print axioms evidence_index_converts
#print axioms snd_typed_at_computed_component
#print axioms family_index_converts
#print axioms snd_typed_from_reduction
#print axioms step_application_typed
#print axioms shared_over_family_typed
#print axioms iter_successor_typed
#print axioms transport_pack_typed
#print axioms kept_evidence_is_not_reflexivity
#print axioms shifted_family_not_refl
#print axioms shifted_step_is_not_reflexivity_step
#print axioms shared_beta
#print axioms shared_contractum_typed
#print axioms subst_shared
#print axioms certificate_uniqueness_fails
#print axioms neutral_index_application_does_not_convert
#print axioms neutral_computed_identity_does_not_convert
#print axioms neutral_refl_is_not_identity_evidence
#print axioms transported_evidence_is_not_reflexivity

end Cetta.Prime.CertifiedTransformLaws
