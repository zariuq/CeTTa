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

/-- One successor step of the iterator: share the package, then continue.
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

/-!
Nonempty addition and identity elimination.

The stored rules are data. A matched substitution instantiates a rule,
and that instance is the root step. The consumer reads the contractum.
These rules are outside `RootComputation.empty`: the empty-root negatives
above do not apply to this package.

A source forall-equality, its specialization at an index, the dependent
identity type at that index, and the iterator's sigma package are four
different terms. `supply` maps the specialized source term to evidence
indexed by that same index. The evidence is not reflexivity. At the closed
numeral `zero`, the zero equation fires and reflexivity inhabits the identity
type the supply names.
-/

def zeroTm {k : Nat} : Tm Head k := .const `zero

def numTm {k : Nat} : Tm Head k := .const `num

def sucTm {k : Nat} (m : Tm Head k) : Tm Head k := .app (.const `suc) m

def addTm {k : Nat} (a b : Tm Head k) : Tm Head k :=
  .app (.app (.const `add) a) b

def jApp {k : Nat} (A x P d y e : Tm Head k) : Tm Head k :=
  .app (.app (.app (.app (.app (.app (.const `«id:eliminate») A) x) P) d) y) e

/-- One stored rule after its pattern variables have been instantiated. -/
inductive RuleInstance : {k : Nat} → Tm Head k → Tm Head k → Type where
  | addZero {k : Nat} (arg : Tm Head k) :
      RuleInstance (addTm arg zeroTm) arg
  | addSuc {k : Nat} (arg m : Tm Head k) :
      RuleInstance (addTm arg (sucTm m)) (sucTm (addTm arg m))
  | identityIota {k : Nat} (A x P d : Tm Head k) :
      RuleInstance (jApp A x P d x (.refl x)) d

private theorem rename_addTm {k m : Nat} (rho : Ren k m) (a b : Tm Head k) :
    Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename rho (addTm a b) =
      addTm (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename rho a) (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename rho b) := by
  simp [addTm, Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename]

private theorem rename_sucTm {k m : Nat} (rho : Ren k m) (a : Tm Head k) :
    Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename rho (sucTm a) = sucTm (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename rho a) := by
  simp [sucTm, Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename]

private theorem rename_zeroTm {k m : Nat} (rho : Ren k m) :
    Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename rho (zeroTm : Tm Head k) = zeroTm := by
  simp [zeroTm, Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename]

private theorem rename_reflTm {k m : Nat} (rho : Ren k m) (a : Tm Head k) :
    Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename rho (Tm.refl a) = Tm.refl (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename rho a) := by
  simp [Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename]

private theorem rename_jApp {k m : Nat} (rho : Ren k m)
    (A x P d y e : Tm Head k) :
    Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename rho (jApp A x P d y e) =
      jApp (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename rho A) (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename rho x)
        (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename rho P) (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename rho d)
        (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename rho y) (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename rho e) := by
  simp [jApp, Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename]

private theorem subst_addTm {k m : Nat} (σ : Sub Head k m) (a b : Tm Head k) :
    Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst σ (addTm a b) =
      addTm (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst σ a) (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst σ b) := by
  simp [addTm, Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst]

private theorem subst_sucTm {k m : Nat} (σ : Sub Head k m) (a : Tm Head k) :
    Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst σ (sucTm a) = sucTm (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst σ a) := by
  simp [sucTm, Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst]

private theorem subst_zeroTm {k m : Nat} (σ : Sub Head k m) :
    Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst σ (zeroTm : Tm Head k) = zeroTm := by
  simp [zeroTm, Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst]

private theorem subst_reflTm {k m : Nat} (σ : Sub Head k m) (a : Tm Head k) :
    Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst σ (Tm.refl a) = Tm.refl (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst σ a) := by
  simp [Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst]

private theorem subst_jApp {k m : Nat} (σ : Sub Head k m)
    (A x P d y e : Tm Head k) :
    Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst σ (jApp A x P d y e) =
      jApp (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst σ A) (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst σ x)
        (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst σ P) (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst σ d)
        (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst σ y) (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst σ e) := by
  simp [jApp, Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst]

/-- Root computation of the addition equations and the identity-elimination rule. -/
def additionJRoot : RootComputation Head where
  step {_k} left right := Nonempty (RuleInstance left right)
  rename := by
    intro k m rho left right ⟨inst⟩
    induction inst with
    | addZero arg =>
        rw [rename_addTm rho arg zeroTm, rename_zeroTm rho]
        exact ⟨RuleInstance.addZero (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename rho arg)⟩
    | addSuc arg motive =>
        rw [rename_addTm rho arg (sucTm motive), rename_sucTm rho motive,
          rename_sucTm rho (addTm arg motive), rename_addTm rho arg motive]
        exact ⟨RuleInstance.addSuc (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename rho arg)
          (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename rho motive)⟩
    | identityIota A x P d =>
        rw [rename_jApp rho A x P d x (.refl x), rename_reflTm rho x]
        exact ⟨RuleInstance.identityIota (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename rho A)
          (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename rho x) (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename rho P)
          (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename rho d)⟩
  substitute := by
    intro k m sigma left right ⟨inst⟩
    induction inst with
    | addZero arg =>
        rw [subst_addTm sigma arg zeroTm, subst_zeroTm sigma]
        exact ⟨RuleInstance.addZero (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst sigma arg)⟩
    | addSuc arg motive =>
        rw [subst_addTm sigma arg (sucTm motive), subst_sucTm sigma motive,
          subst_sucTm sigma (addTm arg motive), subst_addTm sigma arg motive]
        exact ⟨RuleInstance.addSuc (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst sigma arg)
          (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst sigma motive)⟩
    | identityIota A x P d =>
        rw [subst_jApp sigma A x P d x (.refl x), subst_reflTm sigma x]
        exact ⟨RuleInstance.identityIota (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst sigma A)
          (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst sigma x) (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst sigma P)
          (Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.subst sigma d)⟩

theorem addition_equation_outside_empty_root :
    additionJRoot.step (addTm (zeroTm : Tm Head n) (zeroTm : Tm Head n))
        (zeroTm : Tm Head n) ∧
      ¬ (RootComputation.empty : RootComputation Head).step
        (addTm (zeroTm : Tm Head n) (zeroTm : Tm Head n))
        (zeroTm : Tm Head n) := by
  exact ⟨⟨RuleInstance.addZero (zeroTm : Tm Head n)⟩,
    fun impossible => impossible.elim⟩

theorem additionJRoot_ne_empty (k : Nat) :
    additionJRoot ≠ (RootComputation.empty : RootComputation Head) := by
  intro equal
  have occupied : additionJRoot.step
      (addTm (zeroTm : Tm Head k) (zeroTm : Tm Head k))
      (zeroTm : Tm Head k) :=
    ⟨RuleInstance.addZero (zeroTm : Tm Head k)⟩
  rw [equal] at occupied
  exact occupied.elim

/-- The stored zero equation, transported along an arbitrary substitution. -/
theorem matched_zero_substitution {m : Nat} (σ : Sub Head n m)
    (arg : Tm Head n) :
    additionJRoot.step (subst σ (addTm arg zeroTm)) (subst σ arg) :=
  additionJRoot.substitute σ ⟨.addZero arg⟩

theorem substituted_addition_equation {m : Nat} (σ : Sub Head n m)
    (arg : Tm Head n) :
    subst σ (addTm arg zeroTm) = addTm (subst σ arg) zeroTm := by
  simp [addTm, zeroTm, subst]

/-- The consumer's computed index is the contractum of the matched rule.
The argument may itself be a computed term. -/
theorem consumer_reads_zero_contractum (arg : Tm Head n) :
    additionJRoot.step (addTm arg zeroTm) arg :=
  ⟨.addZero arg⟩

theorem rule_shape {k : Nat} {left right : Tm Head k}
    (inst : RuleInstance left right) :
    (∃ arg, left = addTm arg zeroTm ∧ right = arg) ∨
      (∃ arg m, left = addTm arg (sucTm m) ∧
        right = sucTm (addTm arg m)) ∨
      (∃ A x P d, left = jApp A x P d x (Tm.refl x) ∧ right = d) := by
  match inst with
  | .addZero arg => exact Or.inl ⟨arg, rfl, rfl⟩
  | .addSuc arg m => exact Or.inr (Or.inl ⟨arg, m, rfl, rfl⟩)
  | .identityIota A x P d => exact Or.inr (Or.inr ⟨A, x, P, d, rfl, rfl⟩)

/-- Distinguish term constructors without using an impossible equality case. -/
def shapeCode : {k : Nat} → Tm Head k → Nat
  | _, .var _ => 0
  | _, .const _ => 1
  | _, .app _ _ => 2
  | _, .head _ => 3
  | _, .pi _ _ => 4
  | _, .sigma _ _ => 5
  | _, .id _ _ _ => 6
  | _, .lam _ => 7
  | _, .pair _ _ => 8
  | _, .fst _ => 9
  | _, .snd _ => 10
  | _, .refl _ => 11

def spineCode : {k : Nat} → Tm Head k → Nat
  | _, .app f _ => spineCode f
  | _, .const name =>
      if name = `add then 0 else if name = `«id:eliminate» then 1 else 2
  | _, _ => 3

def rightArg : {k : Nat} → Tm Head k → Tm Head k
  | _, .app _ argument => argument
  | _, term => term

/-- An open index is not the constant `zero`, so the addition equations do not
rewrite `add zero k` to `k`. Reflexivity is not the evidence at that index. -/
theorem open_index_not_definitional (k : Fin n) :
    ¬ additionJRoot.step (addTm (zeroTm : Tm Head n) (Tm.var k)) (Tm.var k) := by
  intro witnessed
  rcases witnessed with ⟨inst⟩
  cases rule_shape inst with
  | inl shape =>
      obtain ⟨_, leftEq, _⟩ := shape
      have second := congrArg rightArg leftEq
      simp only [addTm, zeroTm, rightArg] at second
      have codes := congrArg shapeCode second
      simp only [shapeCode] at codes
      cases codes
  | inr shape =>
      cases shape with
      | inl sucShape =>
          obtain ⟨_, _, leftEq, _⟩ := sucShape
          have second := congrArg rightArg leftEq
          simp only [addTm, sucTm, zeroTm, rightArg] at second
          have codes := congrArg shapeCode second
          simp only [shapeCode] at codes
          cases codes
      | inr iotaShape =>
          obtain ⟨_, _, _, _, leftEq, _⟩ := iotaShape
          have codes := congrArg spineCode leftEq
          simp only [addTm, jApp, zeroTm, spineCode] at codes
          cases codes

/-- Identity elimination returns the supplied method when the scrutinee is
reflexivity at the same index. The method may be any accepted certificate. -/
theorem iota_returns_method (A x P d : Tm Head n) :
    additionJRoot.step (jApp A x P d x (.refl x)) d :=
  ⟨.identityIota A x P d⟩

def sourceEq {k : Nat} (a b : Tm Head k) : Tm Head k :=
  .app (.app (.const `eq) a) b

/-- Body of `∀ k. eq num (add zero k) k`, with `k` bound at index 0. -/
def sourceBody : Tm Head (n + 1) :=
  sourceEq (addTm zeroTm (.var 0)) (.var 0)

def sourceForall : Tm Head n :=
  .app (.const `all) (.lam sourceBody)

def consumerIdentity (index : Tm Head n) : Tm Head n :=
  .id numTm (addTm zeroTm index) index

def iteratorSigma (index evidence : Tm Head n) : Tm Head n :=
  .pair index evidence

theorem specialize_source (index : Tm Head n) :
    inst0 index sourceBody = sourceEq (addTm zeroTm index) index := by
  simp only [inst0, subst0, sourceBody, sourceEq, addTm, zeroTm, subst]
  rfl

/-- Evidence recorded for one specialized source equation. It names the
identity type the consumer requires and is not reflexivity or the iterator
package. -/
structure OpenIndexSupply (index : Tm Head n) where
  specialized : Tm Head n
  fromSource : specialized = inst0 index sourceBody
  identityType : Tm Head n
  identityType_eq : identityType = consumerIdentity index
  evidence : Tm Head n
  fromSpecialization : evidence = .app (.const `zeroAddEvidence) specialized
  notReflexivity : evidence ≠ .refl index
  notIterator : evidence ≠ iteratorSigma index (.refl index)
  notIdentityType : evidence ≠ identityType

def supply (index : Tm Head n) : OpenIndexSupply index where
  specialized := inst0 index sourceBody
  fromSource := rfl
  identityType := consumerIdentity index
  identityType_eq := rfl
  evidence := .app (.const `zeroAddEvidence) (inst0 index sourceBody)
  fromSpecialization := rfl
  notReflexivity := by intro equal; cases equal
  notIterator := by intro equal; cases equal
  notIdentityType := by intro equal; cases equal

theorem supply_specializes (index : Tm Head n) :
    (supply index).specialized = sourceEq (addTm zeroTm index) index :=
  (supply index).fromSource.trans (specialize_source index)

theorem four_source_objects_differ (index : Tm Head n) :
    sourceForall ≠ (supply index).specialized ∧
      (supply index).specialized ≠ consumerIdentity index ∧
      consumerIdentity index ≠ iteratorSigma index (.refl index) ∧
      (supply index).evidence ≠ .refl index := by
  refine ⟨?_, ?_, ?_, (supply index).notReflexivity⟩
  · intro equal
    have specialized :
        (supply index).specialized = sourceEq (addTm zeroTm index) index :=
      supply_specializes index
    rw [specialized] at equal
    simp only [sourceForall, sourceEq, addTm, zeroTm] at equal
    cases equal
  · intro equal
    rw [supply_specializes index] at equal
    simp only [sourceEq, consumerIdentity, addTm, zeroTm, numTm] at equal
    cases equal
  · intro equal
    simp only [consumerIdentity, iteratorSigma] at equal
    cases equal

theorem closed_identity_converts {R : Rules Head}
    (aligned : R.computation = additionJRoot) :
    Conv R.headEq (consumerIdentity (zeroTm : Tm Head n))
      (.id (numTm : Tm Head n) (zeroTm : Tm Head n) (zeroTm : Tm Head n))
      R.computation := by
  have stepR : R.computation.step
      (addTm (zeroTm : Tm Head n) (zeroTm : Tm Head n))
      (zeroTm : Tm Head n) := by
    simpa [aligned] using consumer_reads_zero_contractum (zeroTm : Tm Head n)
  exact Relation.EqvGen.rel _ _
    (StepCore.congIdLeft (StepCore.root stepR))

/-- At the closed numeral `zero`, the fired equation makes reflexivity evidence
for the identity type named by the open-index supply. -/
theorem closed_executable_instance {R : Rules Head} {Γ : Ctx Head n}
    {u : Head}
    (aligned : R.computation = additionJRoot)
    (numTyped : Typing R Γ numTm (.head u))
    (isUniv : R.isUniverse u)
    (zeroTyped : Typing R Γ zeroTm numTm)
    (addTyped : Typing R Γ (addTm zeroTm zeroTm) numTm) :
    Typing R Γ (.refl zeroTm) (supply zeroTm).identityType := by
  have reflTyped : Typing R Γ (.refl zeroTm) (.id numTm zeroTm zeroTm) :=
    Typing.reflIntro zeroTyped
  have target : Typing R Γ (consumerIdentity zeroTm) (.head u) :=
    Typing.idForm numTyped isUniv addTyped zeroTyped
  have convForward := closed_identity_converts (n := n) aligned
  have backward :=
    Relation.EqvGen.symm
      (consumerIdentity (zeroTm : Tm Head n))
      (.id (numTm : Tm Head n) (zeroTm : Tm Head n) (zeroTm : Tm Head n))
      convForward
  have typed := Typing.conv reflTyped target isUniv backward
  simpa [supply] using typed

/-- The source forall, specialized at an arbitrary index, supplies the
identity type `eqAt` names and evidence for that specialized equation. -/
theorem open_index_supply_names_consumer (index : Tm Head n) :
    (supply index).identityType = consumerIdentity index ∧
      (supply index).specialized =
        sourceEq (addTm zeroTm index) index ∧
      (supply index).evidence =
        .app (.const `zeroAddEvidence)
          (sourceEq (addTm zeroTm index) index) := by
  refine ⟨(supply index).identityType_eq, supply_specializes index, ?_⟩
  rw [(supply index).fromSpecialization, supply_specializes]

private theorem conv_under_suc {R : Rules Head} {a b : Tm Head n}
    (c : Conv R.headEq a b R.computation) :
    Conv R.headEq (sucTm a) (sucTm b) R.computation := by
  induction c with
  | rel _ _ s =>
      exact Relation.EqvGen.rel _ _ (StepCore.congAppArg s)
  | refl _ => exact Relation.EqvGen.refl _
  | symm _ _ _ ih => exact Relation.EqvGen.symm _ _ ih
  | trans _ _ _ _ _ ih₁ ih₂ =>
      exact Relation.EqvGen.trans _ _ _ ih₁ ih₂

private theorem conv_under_id_left {R : Rules Head}
    {A a a' b : Tm Head n}
    (c : Conv R.headEq a a' R.computation) :
    Conv R.headEq (.id A a b) (.id A a' b) R.computation := by
  induction c with
  | rel _ _ s =>
      exact Relation.EqvGen.rel _ _ (StepCore.congIdLeft s)
  | refl _ => exact Relation.EqvGen.refl _
  | symm _ _ _ ih => exact Relation.EqvGen.symm _ _ ih
  | trans _ _ _ _ _ ih₁ ih₂ =>
      exact Relation.EqvGen.trans _ _ _ ih₁ ih₂

private theorem root_conv {R : Rules Head} {a b : Tm Head n}
    (aligned : R.computation = additionJRoot)
    (stepped : additionJRoot.step a b) :
    Conv R.headEq a b R.computation := by
  exact Relation.EqvGen.rel _ _
    (by simpa [aligned] using StepCore.root stepped)

/-- Two successor steps of the nonempty zero-addition equations compute
`eqAt (suc (suc zero))` to the identity the runtime prints. -/
theorem runtime_family_index {R : Rules Head}
    (aligned : R.computation = additionJRoot) :
    Conv R.headEq
      (consumerIdentity (sucTm (sucTm (zeroTm : Tm Head n))))
      (.id (numTm : Tm Head n)
        (sucTm (sucTm zeroTm)) (sucTm (sucTm zeroTm)))
      R.computation := by
  let z : Tm Head n := zeroTm
  have toSucAdd : Conv R.headEq
      (addTm z (sucTm (sucTm z))) (sucTm (addTm z (sucTm z)))
      R.computation :=
    root_conv aligned ⟨.addSuc z (sucTm z)⟩
  have toSucSucAdd : Conv R.headEq
      (sucTm (addTm z (sucTm z))) (sucTm (sucTm (addTm z z)))
      R.computation :=
    conv_under_suc (root_conv aligned ⟨.addSuc z z⟩)
  have toTwo : Conv R.headEq
      (sucTm (sucTm (addTm z z))) (sucTm (sucTm z))
      R.computation :=
    conv_under_suc (conv_under_suc
      (root_conv aligned (consumer_reads_zero_contractum z)))
  have left : Conv R.headEq (addTm z (sucTm (sucTm z))) (sucTm (sucTm z))
      R.computation :=
    Relation.EqvGen.trans _ _ _ toSucAdd
      (Relation.EqvGen.trans _ _ _ toSucSucAdd toTwo)
  simpa [consumerIdentity, z] using
    conv_under_id_left (A := numTm) (b := sucTm (sucTm z)) left

/-- MeTTa spelling of the closed identity index consumed by `eqAt`. -/
def mettaTm : {k : Nat} → Tm Head k → String
  | _, .const name =>
      match name with
      | .str .anonymous s => s
      | other => other.toString
  | _, .var i => "$" ++ toString i.val
  | _, .app (.const `suc) m => "(suc " ++ mettaTm m ++ ")"
  | _, .app (.app (.const `add) x) y =>
      "(add " ++ mettaTm x ++ " " ++ mettaTm y ++ ")"
  | _, .app (.app (.const `eq) x) y =>
      "(eq " ++ mettaTm x ++ " " ++ mettaTm y ++ ")"
  | _, .app f a => "(app " ++ mettaTm f ++ " " ++ mettaTm a ++ ")"
  | _, .id a x y =>
      "(id " ++ mettaTm a ++ " " ++ mettaTm x ++ " " ++ mettaTm y ++ ")"
  | _, .refl a => "(refl " ++ mettaTm a ++ ")"
  | _, .lam body => "(lam " ++ mettaTm body ++ ")"
  | _, .pair a b => "(pair " ++ mettaTm a ++ " " ++ mettaTm b ++ ")"
  | _, .fst a => "(fst " ++ mettaTm a ++ ")"
  | _, .snd a => "(snd " ++ mettaTm a ++ ")"
  | _, .pi _ _ => "(pi)"
  | _, .sigma _ _ => "(sigma)"
  | _, .head _ => "(head)"

def runtimeClosedIndex : Tm Head n :=
  .id numTm (sucTm (sucTm zeroTm)) (sucTm (sucTm zeroTm))

/-- Hosted equality `eq@num`, the constant the native compiler emits. -/
def hostedEq {k : Nat} (a b : Tm Head k) : Tm Head k :=
  .app (.app (.const `«eq@num») a) b

/-- Body of the hosted motive `λk. eq@num (add zero k) k`. -/
def hostedEqMotive : Tm Head (n + 1) :=
  hostedEq (addTm zeroTm (.var 0)) (.var 0)

/-- The compiled zero-add article: `num-ind` applied to that motive,
reflexivity at zero, and the successor step. `step` is the compiled
substitution step; the runtime names the assumptions by their digests. -/
def hostedZeroAddInduction (step : Tm Head n) : Tm Head n :=
  .app (.app (.app (.const `«num-ind») (.lam hostedEqMotive))
      (.app (.const `«refl@num») zeroTm))
    step

/-- Leibniz elimination, the type of the hosted `subst@num` assumption.
Binders, outer to inner: motive, left endpoint, right endpoint, equality, premise. -/
def leibnizAt (u : Head) : Tm Head n :=
  .pi (.pi numTm (.head u)) <|
    .pi numTm <|
      .pi numTm <|
        .pi (hostedEq (.var 1) (.var 0)) <|
          .pi (.app (.var 3) (.var 2)) <|
            .app (.var 4) (.var 2)

/-- `λz. id num (add zero index) z`, the motive that reads the hosted equation
as an identity at the open index. -/
def endpointMotive (index : Tm Head n) : Tm Head n :=
  .lam (.id numTm (addTm zeroTm (rename wk index)) (.var 0))

def hostedAt (hosted index : Tm Head n) : Tm Head n :=
  .app hosted index

/-- `subst@num` at the identity motive `λz. id num (add zero i) z`.
Applying it to an index, the hosted equation at that index, and
`refl (add zero i)` yields `id num (add zero i) i`. -/
def transportAt : Tm Head n :=
  .pi numTm <|
    .pi (hostedEq (addTm zeroTm (.var 0)) (.var 0)) <|
      .pi (.id numTm (addTm zeroTm (.var (Fin.succ 0)))
          (addTm zeroTm (.var (Fin.succ 0)))) <|
        .id numTm (addTm zeroTm (.var (Fin.succ (Fin.succ 0))))
          (.var (Fin.succ (Fin.succ 0)))

/-- Instantiate the hosted induction at `index` and transport
`refl (add zero index)` along that equation. The result is an identity
derivation at `index`, not reflexivity at `index` and not a fresh evidence name. -/
def identityFromHosted (hosted index : Tm Head n) : Tm Head n :=
  .app (.app (.app (.const `«subst@num») index) (hostedAt hosted index))
    (.refl (addTm zeroTm index))

theorem identity_from_hosted_uses_induction (hosted index : Tm Head n) :
    identityFromHosted hosted index =
      .app (.app (.app (.const `«subst@num») index) (.app hosted index))
        (.refl (addTm zeroTm index)) := rfl

private theorem subst_lifted_one (index proof : Tm Head n) :
    subst (subst0 proof)
      (liftSub (subst0 index) (Fin.succ (0 : Fin (n + 1)))) = index := by
  rw [liftSub_succ, subst0_zero]
  simpa [inst0] using inst0_rename_wk proof index

private theorem premise_type_reduces (index proof : Tm Head n) :
    subst (subst0 proof)
      (subst (liftSub (subst0 index))
        (.id numTm (addTm zeroTm (.var (Fin.succ 0)))
          (addTm zeroTm (.var (Fin.succ 0))))) =
      .id numTm (addTm zeroTm index) (addTm zeroTm index) := by
  simp only [subst, addTm, zeroTm, numTm]
  congr 1
  · exact congrArg (addTm zeroTm) (subst_lifted_one index proof)
  · exact congrArg (addTm zeroTm) (subst_lifted_one index proof)

private theorem subst_lifted_two (index proof premise : Tm Head n) :
    subst (subst0 premise)
      (subst (liftSub (subst0 proof))
        (liftSub (liftSub (subst0 index))
          (Fin.succ (Fin.succ (0 : Fin (n + 1)))))) = index := by
  rw [liftSub_succ, liftSub_succ, subst0_zero, subst_liftSub_wk]
  have inner : subst (subst0 proof) (rename wk index) = index := by
    simpa [inst0] using inst0_rename_wk proof index
  rw [inner]
  simpa [inst0] using inst0_rename_wk premise index

private theorem result_type_reduces (index proof premise : Tm Head n) :
    subst (subst0 premise)
      (subst (liftSub (subst0 proof))
        (subst (liftSub (liftSub (subst0 index)))
          (.id numTm (addTm zeroTm (.var (Fin.succ (Fin.succ 0))))
            (.var (Fin.succ (Fin.succ 0)))))) =
      .id numTm (addTm zeroTm index) index := by
  simp only [subst, addTm, zeroTm, numTm]
  congr 1
  · exact congrArg (addTm zeroTm) (subst_lifted_two index proof premise)
  · exact subst_lifted_two index proof premise

private theorem result_type_inst (index proof premise : Tm Head n) :
    inst0 premise
      (subst (liftSub (subst0 proof))
        (subst (liftSub (liftSub (subst0 index)))
          (.id numTm (addTm zeroTm (.var (Fin.succ (Fin.succ 0))))
            (.var (Fin.succ (Fin.succ 0)))))) =
      .id numTm (addTm zeroTm index) index := by
  simpa [inst0] using result_type_reduces index proof premise

theorem identity_from_hosted_induction
    {Γ : Ctx Head n} {index hosted : Tm Head n}
    (substTyped : Typing R Γ (.const `«subst@num») transportAt)
    (indexTyped : Typing R Γ index numTm)
    (addIndexTyped : Typing R Γ (addTm zeroTm index) numTm)
    (hostedTyped : Typing R Γ (hostedAt hosted index)
      (hostedEq (addTm zeroTm index) index)) :
    Typing R Γ (identityFromHosted hosted index) (consumerIdentity index) := by
  have atIndex := Typing.appElim substTyped indexTyped
  have atEquation := Typing.appElim atIndex hostedTyped
  have premise : Typing R Γ (.refl (addTm zeroTm index))
      (.id numTm (addTm zeroTm index) (addTm zeroTm index)) :=
    Typing.reflIntro addIndexTyped
  have domainEq := premise_type_reduces index (hostedAt hosted index)
  rw [subst_comp] at domainEq
  have premiseAtDomain := premise
  rw [← domainEq] at premiseAtDomain
  have transported := Typing.appElim atEquation premiseAtDomain
  rw [result_type_inst index (hostedAt hosted index)
    (.refl (addTm zeroTm index))] at transported
  simpa [identityFromHosted, hostedAt, consumerIdentity] using transported

def domainOf : {k : Nat} → Tm Head k → Tm Head k
  | _, .pi domain _ => domain
  | _, term => term

def codomain : {k : Nat} → Tm Head k → Tm Head (k + 1)
  | _, .pi _ body => body
  | _, _ => .const `unused

theorem domain_after_motive (u : Head) (motive : Tm Head n) :
    domainOf (inst0 motive (codomain (leibnizAt u))) = numTm := by
  simp [domainOf, codomain, leibnizAt, inst0, subst0, subst, liftSub, hostedEq, numTm]

private theorem rename_num {k m : Nat} (ρ : Ren k m) :
    Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename ρ
      (numTm : Tm Head k) = numTm := by
  simp [numTm, Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId.Presentation.rename]

theorem instantiate_hosted_equation (index : Tm Head n) :
    inst0 index (hostedEq (addTm zeroTm (.var 0)) (.var 0)) =
      hostedEq (addTm zeroTm index) index := by
  simp [inst0, subst0, subst, hostedEq, addTm, zeroTm]

def kernelTm : {k : Nat} → Tm Head k → String
  | _, .const name =>
      let shown := match name with
        | .str .anonymous s => s
        | other => other.toString
      "(DeclConst " ++ shown ++ ")"
  | _, .var i => "(idx " ++ toString i.val ++ ")"
  | _, .app f a => "(App " ++ kernelTm f ++ " " ++ kernelTm a ++ ")"
  | _, .lam body => "(Lam " ++ kernelTm body ++ ")"
  | _, .refl a => "(Refl " ++ kernelTm a ++ ")"
  | _, .id a x y =>
      "(id " ++ kernelTm a ++ " " ++ kernelTm x ++ " " ++ kernelTm y ++ ")"
  | _, .pair a b => "(pair " ++ kernelTm a ++ " " ++ kernelTm b ++ ")"
  | _, .fst a => "(fst " ++ kernelTm a ++ ")"
  | _, .snd a => "(snd " ++ kernelTm a ++ ")"
  | _, .pi _ _ => "(pi)"
  | _, .sigma _ _ => "(sigma)"
  | _, .head _ => "(head)"

#eval kernelTm (hostedEqMotive : Tm Unit 1)
#eval mettaTm (identityFromHosted (.const `«zero-add») (.var 0) : Tm Unit 1)
#eval mettaTm (runtimeClosedIndex : Tm Unit 0)
#eval mettaTm (consumerIdentity (zeroTm : Tm Unit 0))
#eval mettaTm (sourceEq (addTm (zeroTm : Tm Unit 1) (.var 0)) (.var 0))

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
#print axioms addition_equation_outside_empty_root
#print axioms additionJRoot_ne_empty
#print axioms matched_zero_substitution
#print axioms substituted_addition_equation
#print axioms consumer_reads_zero_contractum
#print axioms open_index_not_definitional
#print axioms iota_returns_method
#print axioms specialize_source
#print axioms supply_specializes
#print axioms four_source_objects_differ
#print axioms closed_identity_converts
#print axioms closed_executable_instance
#print axioms open_index_supply_names_consumer
#print axioms runtime_family_index
#print axioms identity_from_hosted_uses_induction
#print axioms instantiate_hosted_equation
#print axioms identity_from_hosted_induction

end Cetta.Prime.CertifiedTransformLaws
