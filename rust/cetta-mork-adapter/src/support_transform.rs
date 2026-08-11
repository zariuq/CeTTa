use cetta_space::stable_bridge_expr_packet_bytes;
use mork::space::Space;
use mork_expr::{Expr, ExprEnv};
use pathmap::PathMap;
use pathmap::zipper::ZipperWriting;
use std::collections::{BTreeMap, HashMap};

const PROFILE_MAGIC: &[u8; 4] = b"CSTP";
const PROFILE_VERSION: u16 = 1;
const SCHEDULER_LEAST_MORK_COMPACT_EXPRESSION: u8 = 1;
const UNSUPPORTED_LEAVE_INERT: u8 = 1;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SupportProviderKind {
    SnapshotMatch,
    Equal,
    NotEqual,
    Add,
    Remove,
    Head,
    Tail,
    GroupCardinality,
    EvaluateProjectPureF64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct NativeSupportProvider {
    pub kind: SupportProviderKind,
    pub native_surface: String,
    pub argument_count: u8,
}

#[derive(Clone, Debug, Default)]
pub struct PhysicalProviderRegistry {
    providers: BTreeMap<String, NativeSupportProvider>,
}

impl PhysicalProviderRegistry {
    pub fn register(
        &mut self,
        operator_id: impl Into<String>,
        provider: NativeSupportProvider,
    ) -> Result<(), String> {
        let operator_id = operator_id.into();
        if operator_id.is_empty() || provider.native_surface.is_empty() {
            return Err("support provider identity and surface must be nonempty".to_string());
        }
        if self.providers.insert(operator_id.clone(), provider).is_some() {
            return Err(format!("support provider is registered twice: {operator_id}"));
        }
        Ok(())
    }

    pub fn mork_native() -> Self {
        let mut registry = Self::default();
        for (operator_id, kind, surface, argument_count) in [
            (
                "support.snapshot-match.v1",
                SupportProviderKind::SnapshotMatch,
                "BTM",
                1,
            ),
            (
                "support.equal.v1",
                SupportProviderKind::Equal,
                "==",
                2,
            ),
            (
                "support.not-equal.v1",
                SupportProviderKind::NotEqual,
                "!=",
                2,
            ),
            (
                "support.add.v1",
                SupportProviderKind::Add,
                "+",
                1,
            ),
            (
                "support.remove.v1",
                SupportProviderKind::Remove,
                "-",
                1,
            ),
            (
                "support.head.v1",
                SupportProviderKind::Head,
                "head",
                2,
            ),
            (
                "support.tail.v1",
                SupportProviderKind::Tail,
                "tail",
                2,
            ),
            (
                "support.group-cardinality.v1",
                SupportProviderKind::GroupCardinality,
                "count",
                3,
            ),
            (
                "support.evaluate-project.mm2-pure-f64.v1",
                SupportProviderKind::EvaluateProjectPureF64,
                "pure",
                3,
            ),
        ] {
            registry
                .register(
                    operator_id,
                    NativeSupportProvider {
                        kind,
                        native_surface: surface.to_string(),
                        argument_count,
                    },
                )
                .expect("the native MORK provider table is static and unique");
        }
        registry
    }

    fn get(&self, operator_id: &str) -> Option<&NativeSupportProvider> {
        self.providers.get(operator_id)
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SupportOperatorDeclaration {
    pub surface: String,
    pub argument_count: u8,
    pub operator_id: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SupportTransformProfile {
    pub language: String,
    pub profile: String,
    pub manifest_sha256: String,
    pub compiler_sha256: String,
    pub work_symbol: String,
    pub compatible_input_symbol: String,
    pub compatible_input_operator_id: String,
    pub explicit_input_symbol: String,
    pub compatible_output_symbol: String,
    pub compatible_output_operator_id: String,
    pub explicit_output_symbol: String,
    pub work_arity: u8,
    pub location_position: u8,
    pub input_position: u8,
    pub output_position: u8,
    pub scheduler: u8,
    pub unsupported_policy: u8,
    pub sources: Vec<SupportOperatorDeclaration>,
    pub sinks: Vec<SupportOperatorDeclaration>,
}

struct PacketReader<'a> {
    packet: &'a [u8],
    offset: usize,
}

impl<'a> PacketReader<'a> {
    fn new(packet: &'a [u8]) -> Self {
        Self { packet, offset: 0 }
    }

    fn take(&mut self, count: usize) -> Result<&'a [u8], String> {
        let end = self
            .offset
            .checked_add(count)
            .ok_or_else(|| "physical profile offset overflow".to_string())?;
        let bytes = self
            .packet
            .get(self.offset..end)
            .ok_or_else(|| "physical profile packet is truncated".to_string())?;
        self.offset = end;
        Ok(bytes)
    }

    fn u8(&mut self) -> Result<u8, String> {
        Ok(self.take(1)?[0])
    }

    fn u16(&mut self) -> Result<u16, String> {
        let bytes: [u8; 2] = self
            .take(2)?
            .try_into()
            .expect("two bytes were requested");
        Ok(u16::from_be_bytes(bytes))
    }

    fn text(&mut self) -> Result<String, String> {
        let length = usize::from(self.u16()?);
        let bytes = self.take(length)?;
        let text = std::str::from_utf8(bytes)
            .map_err(|error| format!("physical profile text is not UTF-8: {error}"))?;
        if text.is_empty() {
            return Err("physical profile text fields must be nonempty".to_string());
        }
        Ok(text.to_string())
    }

    fn declarations(&mut self) -> Result<Vec<SupportOperatorDeclaration>, String> {
        let count = usize::from(self.u16()?);
        let mut declarations = Vec::with_capacity(count);
        for _ in 0..count {
            declarations.push(SupportOperatorDeclaration {
                surface: self.text()?,
                argument_count: self.u8()?,
                operator_id: self.text()?,
            });
        }
        Ok(declarations)
    }
}

fn is_lower_hex_sha256(text: &str) -> bool {
    text.len() == 64
        && text
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}

fn declarations_are_unique(declarations: &[SupportOperatorDeclaration]) -> bool {
    declarations.iter().enumerate().all(|(index, declaration)| {
        declarations[..index]
            .iter()
            .all(|prior| prior.surface != declaration.surface)
    })
}

impl SupportTransformProfile {
    pub fn decode(packet: &[u8]) -> Result<Self, String> {
        let mut reader = PacketReader::new(packet);
        if reader.take(4)? != PROFILE_MAGIC {
            return Err("physical profile has the wrong magic".to_string());
        }
        if reader.u16()? != PROFILE_VERSION {
            return Err("physical profile has an unsupported version".to_string());
        }
        if reader.u16()? != 0 {
            return Err("physical profile reserved bits must be zero".to_string());
        }
        let mut decoded = Self {
            language: reader.text()?,
            profile: reader.text()?,
            manifest_sha256: reader.text()?,
            compiler_sha256: reader.text()?,
            work_symbol: reader.text()?,
            compatible_input_symbol: reader.text()?,
            compatible_input_operator_id: reader.text()?,
            explicit_input_symbol: reader.text()?,
            compatible_output_symbol: reader.text()?,
            compatible_output_operator_id: reader.text()?,
            explicit_output_symbol: reader.text()?,
            work_arity: reader.u8()?,
            location_position: reader.u8()?,
            input_position: reader.u8()?,
            output_position: reader.u8()?,
            scheduler: reader.u8()?,
            unsupported_policy: reader.u8()?,
            sources: Vec::new(),
            sinks: Vec::new(),
        };
        decoded.sources = reader.declarations()?;
        decoded.sinks = reader.declarations()?;
        if reader.offset != packet.len() {
            return Err("physical profile contains trailing bytes".to_string());
        }
        decoded.validate()?;
        Ok(decoded)
    }

    fn validate(&self) -> Result<(), String> {
        if !is_lower_hex_sha256(&self.manifest_sha256)
            || !is_lower_hex_sha256(&self.compiler_sha256)
        {
            return Err("physical profile identities must be lowercase SHA-256".to_string());
        }
        if self.work_arity != 3 {
            return Err("support-transform V1 requires work arity three".to_string());
        }
        let mut positions = [
            self.location_position,
            self.input_position,
            self.output_position,
        ];
        positions.sort_unstable();
        if positions != [0, 1, 2] {
            return Err("support-transform work positions must be a permutation of 0,1,2".to_string());
        }
        if self.scheduler != SCHEDULER_LEAST_MORK_COMPACT_EXPRESSION
            || self.unsupported_policy != UNSUPPORTED_LEAVE_INERT
        {
            return Err("physical profile requests an unsupported policy".to_string());
        }
        if !declarations_are_unique(&self.sources) || !declarations_are_unique(&self.sinks) {
            return Err("physical profile repeats a source or sink surface".to_string());
        }
        Ok(())
    }
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct SupportTransformStats {
    pub plan_builds: u64,
    pub plan_cache_hits: u64,
    pub initial_execs_scanned: u64,
    pub delta_execs_scanned: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SupportTransformRun {
    pub performed: u64,
    pub has_work: bool,
    pub stats: SupportTransformStats,
}

#[derive(Clone, Debug)]
struct CompiledPlan {
    source_count: usize,
    sink_count: usize,
}

#[derive(Clone, Debug)]
enum PlanAdmission {
    Supported(CompiledPlan),
    Unsupported,
}

type SchedulerKey = Vec<u8>;

struct DirectiveView {
    input: Expr,
    output: Expr,
}

pub struct SupportTransformExecutor {
    profile: SupportTransformProfile,
    providers: PhysicalProviderRegistry,
    plan_cache: BTreeMap<Vec<u8>, PlanAdmission>,
    queue: BTreeMap<SchedulerKey, Vec<u8>>,
    queued_by_expr: HashMap<Vec<u8>, SchedulerKey>,
    stats: SupportTransformStats,
}

impl SupportTransformExecutor {
    pub fn from_packet(
        packet: &[u8],
        providers: PhysicalProviderRegistry,
    ) -> Result<Self, String> {
        let profile = SupportTransformProfile::decode(packet)?;
        let executor = Self {
            profile,
            providers,
            plan_cache: BTreeMap::new(),
            queue: BTreeMap::new(),
            queued_by_expr: HashMap::new(),
            stats: SupportTransformStats::default(),
        };
        executor.validate_native_shell()?;
        Ok(executor)
    }

    fn validate_native_shell(&self) -> Result<(), String> {
        let compatible_input = self
            .providers
            .get(&self.profile.compatible_input_operator_id)
            .ok_or_else(|| "compatible input provider is unavailable".to_string())?;
        let compatible_output = self
            .providers
            .get(&self.profile.compatible_output_operator_id)
            .ok_or_else(|| "compatible output provider is unavailable".to_string())?;
        if self.profile.work_symbol != "exec"
            || self.profile.compatible_input_symbol != ","
            || self.profile.explicit_input_symbol != "I"
            || self.profile.compatible_output_symbol != ","
            || self.profile.explicit_output_symbol != "O"
            || compatible_input.kind != SupportProviderKind::SnapshotMatch
            || compatible_output.kind != SupportProviderKind::Add
        {
            return Err(
                "native MORK physical execution does not realize this authored shell".to_string(),
            );
        }
        Ok(())
    }

    pub fn run(&mut self, space: &mut Space, fuel: u64) -> Result<SupportTransformRun, String> {
        self.rebuild_queue(space)?;
        let mut performed = 0u64;
        while performed < fuel {
            let Some((key, directive_bytes)) = self.queue.pop_first() else {
                break;
            };
            self.queued_by_expr.remove(&directive_bytes);
            let removed = space.btm.remove(&directive_bytes).is_some();
            if !removed {
                continue;
            }
            let post_remove = space.btm.clone();
            let mut owned_directive = directive_bytes.clone();
            let directive = Expr {
                ptr: owned_directive.as_mut_ptr(),
            };
            if let Err(error) = space.interpret(directive) {
                space.btm.insert(&directive_bytes, ());
                self.queue.insert(key.clone(), directive_bytes.clone());
                self.queued_by_expr.insert(directive_bytes, key);
                return Err(format!(
                    "admitted native support-transform directive failed: {error}"
                ));
            }
            let added = map_difference(&space.btm, &post_remove);
            let removed = map_difference(&post_remove, &space.btm);
            self.remove_delta_execs(&removed);
            self.add_delta_execs(space, &added)?;
            performed += 1;
        }
        Ok(SupportTransformRun {
            performed,
            has_work: !self.queue.is_empty(),
            stats: self.stats.clone(),
        })
    }

    fn rebuild_queue(&mut self, space: &Space) -> Result<(), String> {
        self.queue.clear();
        self.queued_by_expr.clear();
        let candidates = space
            .btm
            .iter()
            .map(|(path, _)| path)
            .filter(|path| compact_expr_head_is(path, self.profile.work_symbol.as_bytes()))
            .collect::<Vec<_>>();
        self.stats.initial_execs_scanned = self
            .stats
            .initial_execs_scanned
            .saturating_add(candidates.len() as u64);
        for candidate in candidates {
            self.admit_to_queue(space, candidate)?;
        }
        Ok(())
    }

    fn add_delta_execs(&mut self, space: &Space, delta: &PathMap<()>) -> Result<(), String> {
        let candidates = delta
            .iter()
            .map(|(path, _)| path)
            .filter(|path| compact_expr_head_is(path, self.profile.work_symbol.as_bytes()))
            .collect::<Vec<_>>();
        self.stats.delta_execs_scanned = self
            .stats
            .delta_execs_scanned
            .saturating_add(candidates.len() as u64);
        for candidate in candidates {
            self.admit_to_queue(space, candidate)?;
        }
        Ok(())
    }

    fn remove_delta_execs(&mut self, delta: &PathMap<()>) {
        for (candidate, _) in delta.iter() {
            if let Some(key) = self.queued_by_expr.remove(&candidate) {
                self.queue.remove(&key);
            }
        }
    }

    fn admit_to_queue(&mut self, space: &Space, raw: Vec<u8>) -> Result<(), String> {
        if self.queued_by_expr.contains_key(&raw) {
            return Ok(());
        }
        let Some((view, plan_key)) = self.directive_view_and_plan_key(space, &raw)? else {
            return Ok(());
        };
        let admission = if let Some(cached) = self.plan_cache.get(&plan_key) {
            self.stats.plan_cache_hits = self.stats.plan_cache_hits.saturating_add(1);
            cached.clone()
        } else {
            self.stats.plan_builds = self.stats.plan_builds.saturating_add(1);
            let admission = match self.validate_plan(space, &view) {
                Ok(plan) => PlanAdmission::Supported(plan),
                Err(()) => PlanAdmission::Unsupported,
            };
            self.plan_cache.insert(plan_key, admission.clone());
            admission
        };
        let PlanAdmission::Supported(plan) = admission else {
            return Ok(());
        };
        debug_assert!(plan.source_count > 0);
        let _sink_count = plan.sink_count;
        let scheduler_key = raw.clone();
        self.queue.insert(scheduler_key.clone(), raw.clone());
        self.queued_by_expr.insert(raw, scheduler_key);
        Ok(())
    }

    fn directive_view_and_plan_key(
        &self,
        space: &Space,
        raw: &[u8],
    ) -> Result<Option<(DirectiveView, Vec<u8>)>, String> {
        let directive = Expr {
            ptr: raw.as_ptr().cast_mut(),
        };
        let arguments = expr_arguments(directive);
        if arguments.len() != usize::from(self.profile.work_arity) + 1
            || symbol_text(space, arguments[0].subsexpr())? != self.profile.work_symbol
        {
            return Ok(None);
        }
        let location = arguments[usize::from(self.profile.location_position) + 1].subsexpr();
        if location.variables() != 0 {
            return Ok(None);
        }
        let input = arguments[usize::from(self.profile.input_position) + 1].subsexpr();
        let output = arguments[usize::from(self.profile.output_position) + 1].subsexpr();
        let input_key = stable_bridge_expr_packet_bytes(space, input)?;
        let output_key = stable_bridge_expr_packet_bytes(space, output)?;
        let mut plan_key = Vec::with_capacity(input_key.len() + output_key.len() + 8);
        let input_len = u32::try_from(input_key.len())
            .map_err(|_| "support-transform input key exceeds u32".to_string())?;
        let output_len = u32::try_from(output_key.len())
            .map_err(|_| "support-transform output key exceeds u32".to_string())?;
        plan_key.extend_from_slice(&input_len.to_be_bytes());
        plan_key.extend_from_slice(&input_key);
        plan_key.extend_from_slice(&output_len.to_be_bytes());
        plan_key.extend_from_slice(&output_key);
        Ok(Some((DirectiveView { input, output }, plan_key)))
    }

    fn validate_plan(&self, space: &Space, view: &DirectiveView) -> Result<CompiledPlan, ()> {
        let input = expr_arguments(view.input);
        let output = expr_arguments(view.output);
        if input.is_empty() || output.is_empty() {
            return Err(());
        }
        let input_head = symbol_text(space, input[0].subsexpr()).map_err(|_| ())?;
        let output_head = symbol_text(space, output[0].subsexpr()).map_err(|_| ())?;
        let compatible_input = input_head == self.profile.compatible_input_symbol;
        let compatible_output = output_head == self.profile.compatible_output_symbol;
        if !compatible_input && input_head != self.profile.explicit_input_symbol {
            return Err(());
        }
        if !compatible_output && output_head != self.profile.explicit_output_symbol {
            return Err(());
        }
        if input.len() <= 1 {
            return Err(());
        }
        if !compatible_input {
            for factor in &input[1..] {
                self.validate_declared_form(space, factor.subsexpr(), true)?;
            }
        }
        if !compatible_output {
            for sink in &output[1..] {
                let provider = self.validate_declared_form(space, sink.subsexpr(), false)?;
                if matches!(provider.kind, SupportProviderKind::Head | SupportProviderKind::Tail)
                    && !head_tail_limit_is_positive(space, sink.subsexpr())
                {
                    return Err(());
                }
                if provider.kind == SupportProviderKind::GroupCardinality
                    && !cardinality_guard_is_supported(space, sink.subsexpr())
                {
                    return Err(());
                }
                if provider.kind == SupportProviderKind::EvaluateProjectPureF64
                    && !pure_f64_sink_is_supported(space, sink.subsexpr())
                {
                    return Err(());
                }
            }
        }
        Ok(CompiledPlan {
            source_count: input.len() - 1,
            sink_count: output.len() - 1,
        })
    }

    fn validate_declared_form<'a>(
        &'a self,
        space: &Space,
        form: Expr,
        source: bool,
    ) -> Result<&'a NativeSupportProvider, ()> {
        let arguments = expr_arguments(form);
        if arguments.is_empty() {
            return Err(());
        }
        let head = symbol_text(space, arguments[0].subsexpr()).map_err(|_| ())?;
        let declarations = if source {
            &self.profile.sources
        } else {
            &self.profile.sinks
        };
        let declaration = declarations
            .iter()
            .find(|declaration| {
                declaration.surface == head
                    && usize::from(declaration.argument_count) + 1 == arguments.len()
            })
            .ok_or(())?;
        let provider = self.providers.get(&declaration.operator_id).ok_or(())?;
        if provider.native_surface != declaration.surface
            || provider.argument_count != declaration.argument_count
        {
            return Err(());
        }
        let correct_side = if source {
            matches!(
                provider.kind,
                SupportProviderKind::SnapshotMatch
                    | SupportProviderKind::Equal
                    | SupportProviderKind::NotEqual
            )
        } else {
            matches!(
                provider.kind,
                SupportProviderKind::Add
                    | SupportProviderKind::Remove
                    | SupportProviderKind::Head
                    | SupportProviderKind::Tail
                    | SupportProviderKind::GroupCardinality
                    | SupportProviderKind::EvaluateProjectPureF64
            )
        };
        correct_side.then_some(provider).ok_or(())
    }
}

pub fn run_support_transform_packet(
    space: &mut Space,
    packet: &[u8],
    fuel: u64,
) -> Result<SupportTransformRun, String> {
    let mut executor = SupportTransformExecutor::from_packet(
        packet,
        PhysicalProviderRegistry::mork_native(),
    )?;
    executor.run(space, fuel)
}

fn expr_arguments(expr: Expr) -> Vec<ExprEnv> {
    let mut arguments = Vec::new();
    ExprEnv::new(0, expr).args(&mut arguments);
    arguments
}

fn symbol_text(space: &Space, expr: Expr) -> Result<String, String> {
    if expr.symbol().is_none() {
        return Err("support-transform expected a symbol".to_string());
    }
    let packet = stable_bridge_expr_packet_bytes(space, expr)?;
    if packet.first().copied() != Some(1) || packet.len() < 5 {
        return Err("support-transform symbol packet is malformed".to_string());
    }
    let length = u32::from_be_bytes(
        packet[1..5]
            .try_into()
            .expect("four symbol length bytes were checked"),
    ) as usize;
    let bytes = packet
        .get(5..5 + length)
        .ok_or_else(|| "support-transform symbol packet is truncated".to_string())?;
    if 5 + length != packet.len() {
        return Err("support-transform symbol packet has trailing bytes".to_string());
    }
    std::str::from_utf8(bytes)
        .map(str::to_string)
        .map_err(|error| format!("support-transform symbol is not UTF-8: {error}"))
}

fn head_tail_limit_is_positive(space: &Space, form: Expr) -> bool {
    let arguments = expr_arguments(form);
    if arguments.len() != 3 || arguments[1].subsexpr().variables() != 0 {
        return false;
    }
    let Ok(text) = symbol_text(space, arguments[1].subsexpr()) else {
        return false;
    };
    text.parse::<usize>().is_ok_and(|value| value > 0)
}

fn cardinality_guard_is_supported(space: &Space, form: Expr) -> bool {
    let arguments = expr_arguments(form);
    if arguments.len() != 4 {
        return false;
    }
    let counter = arguments[2].subsexpr();
    if counter.arity().is_none() && counter.symbol().is_none() && counter.variables() == 1 {
        return true;
    }
    if counter.variables() != 0 {
        return false;
    }
    let Ok(text) = symbol_text(space, counter) else {
        return false;
    };
    !text.is_empty() && text.bytes().all(|byte| byte.is_ascii_digit())
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum PureF64Type {
    Atom,
    F64,
}

fn pure_f64_type(space: &Space, term: Expr) -> Option<PureF64Type> {
    if term.arity().is_none() {
        return Some(PureF64Type::Atom);
    }
    let arguments = expr_arguments(term);
    if arguments.is_empty() {
        return None;
    }
    let head = symbol_text(space, arguments[0].subsexpr()).ok()?;
    let argument_types = arguments[1..]
        .iter()
        .map(|argument| pure_f64_type(space, argument.subsexpr()))
        .collect::<Option<Vec<_>>>()?;
    match head.as_str() {
        "f64_from_string"
            if argument_types.as_slice() == [PureF64Type::Atom] =>
        {
            Some(PureF64Type::F64)
        }
        "f64_to_string"
            if argument_types.as_slice() == [PureF64Type::F64] =>
        {
            Some(PureF64Type::Atom)
        }
        "signum_f64"
            if argument_types.as_slice() == [PureF64Type::F64] =>
        {
            Some(PureF64Type::F64)
        }
        "sub_f64" | "div_f64"
            if argument_types.as_slice() == [PureF64Type::F64, PureF64Type::F64] =>
        {
            Some(PureF64Type::F64)
        }
        "sum_f64" | "product_f64"
            if argument_types.iter().all(|kind| *kind == PureF64Type::F64) =>
        {
            Some(PureF64Type::F64)
        }
        _ => None,
    }
}

fn pure_f64_sink_is_supported(space: &Space, form: Expr) -> bool {
    let arguments = expr_arguments(form);
    arguments.len() == 4
        && pure_f64_type(space, arguments[3].subsexpr()) == Some(PureF64Type::Atom)
}

fn compact_expr_head_is(raw: &[u8], expected: &[u8]) -> bool {
    if raw.len() < expected.len() + 2 || raw[0] & 0xc0 != 0 {
        return false;
    }
    let symbol_tag = raw[1];
    if symbol_tag & 0xc0 != 0xc0 || symbol_tag == 0xc0 {
        return false;
    }
    let length = usize::from(symbol_tag & 0x3f);
    length == expected.len() && raw.get(2..2 + length) == Some(expected)
}

fn map_difference(left: &PathMap<()>, right: &PathMap<()>) -> PathMap<()> {
    let mut difference = left.clone();
    {
        let right_zipper = right.read_zipper();
        let mut difference_zipper = difference.write_zipper();
        difference_zipper.subtract_into(&right_zipper, true);
    }
    difference
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parse(space: &mut Space, text: &[u8]) -> Vec<u8> {
        let mut encoded = vec![0; text.len().saturating_mul(4).saturating_add(4096)];
        let (_, used) = space
            .parse_sexpr(text, encoded.as_mut_ptr())
            .expect("test expression should parse");
        encoded.truncate(used);
        encoded
    }

    fn insert(space: &mut Space, text: &[u8]) {
        let encoded = parse(space, text);
        space.btm.insert(&encoded, ());
    }

    fn contains(space: &mut Space, text: &[u8]) -> bool {
        let encoded = parse(space, text);
        space.btm.contains(&encoded)
    }

    fn test_packet() -> Vec<u8> {
        fn text(packet: &mut Vec<u8>, value: &str) {
            packet.extend_from_slice(&(value.len() as u16).to_be_bytes());
            packet.extend_from_slice(value.as_bytes());
        }
        let mut packet = Vec::new();
        packet.extend_from_slice(PROFILE_MAGIC);
        packet.extend_from_slice(&PROFILE_VERSION.to_be_bytes());
        packet.extend_from_slice(&0u16.to_be_bytes());
        for value in [
            "mm2",
            "gslt",
            &"a".repeat(64),
            &"b".repeat(64),
            "exec",
            ",",
            "support.snapshot-match.v1",
            "I",
            ",",
            "support.add.v1",
            "O",
        ] {
            text(&mut packet, value);
        }
        packet.extend_from_slice(&[3, 0, 1, 2, 1, 1]);
        let sources = [
            ("BTM", 1, "support.snapshot-match.v1"),
            ("==", 2, "support.equal.v1"),
            ("!=", 2, "support.not-equal.v1"),
        ];
        packet.extend_from_slice(&(sources.len() as u16).to_be_bytes());
        for (surface, arity, operator) in sources {
            text(&mut packet, surface);
            packet.push(arity);
            text(&mut packet, operator);
        }
        let sinks = [
            ("+", 1, "support.add.v1"),
            ("-", 1, "support.remove.v1"),
            ("head", 2, "support.head.v1"),
            ("tail", 2, "support.tail.v1"),
            ("count", 3, "support.group-cardinality.v1"),
            (
                "pure",
                3,
                "support.evaluate-project.mm2-pure-f64.v1",
            ),
        ];
        packet.extend_from_slice(&(sinks.len() as u16).to_be_bytes());
        for (surface, arity, operator) in sinks {
            text(&mut packet, surface);
            packet.push(arity);
            text(&mut packet, operator);
        }
        packet
    }

    #[test]
    fn packet_round_trip_exposes_authored_capabilities() {
        let profile = SupportTransformProfile::decode(&test_packet()).unwrap();
        assert_eq!(profile.language, "mm2");
        assert_eq!(profile.sources.len(), 3);
        assert_eq!(profile.sinks.len(), 6);
        assert_eq!(profile.sinks[2].operator_id, "support.head.v1");

        let mut malformed = test_packet();
        malformed.push(0);
        assert!(SupportTransformProfile::decode(&malformed).is_err());
    }

    #[test]
    fn relational_product_reuses_support_atom() {
        let mut space = Space::new();
        insert(&mut space, b"(p a)");
        insert(
            &mut space,
            b"(exec (0 reusable) (, (p $x) (p $y)) (, (pair $x $y)))",
        );
        let run = run_support_transform_packet(&mut space, &test_packet(), 8).unwrap();
        assert_eq!(run.performed, 1);
        assert!(!run.has_work);
        assert!(contains(&mut space, b"(pair a a)"));
    }

    #[test]
    fn generated_exec_reuses_alpha_normalized_plan() {
        let mut space = Space::new();
        insert(&mut space, b"(seed a)");
        insert(
            &mut space,
            b"(exec (0 first) (, (seed $x)) (O (+ (seen $x))))",
        );
        insert(
            &mut space,
            b"(exec (1 generator) (, (seen $x)) (O (+ (exec (2 generated) (, (seed $y)) (O (+ (seen $y)))))))",
        );
        let run = run_support_transform_packet(&mut space, &test_packet(), 8).unwrap();
        assert_eq!(run.performed, 3);
        assert!(contains(&mut space, b"(seen a)"));
        assert!(run.stats.plan_builds >= 2);
        assert!(run.stats.plan_cache_hits >= 1);
    }

    #[test]
    fn unsupported_provider_stays_inert() {
        let mut space = Space::new();
        insert(&mut space, b"(seed a)");
        insert(
            &mut space,
            b"(exec (0 unknown) (, (seed $x)) (O (mystery (bad $x))))",
        );
        let run = run_support_transform_packet(&mut space, &test_packet(), 8).unwrap();
        assert_eq!(run.performed, 0);
        assert!(!run.has_work);
        assert_eq!(space.btm.val_count(), 2);
    }

    #[test]
    fn exact_fuel_reports_residual_supported_work() {
        let mut space = Space::new();
        insert(&mut space, b"(seed a)");
        insert(
            &mut space,
            b"(exec (0 bounded) (, (seed $x)) (, (answer $x)))",
        );
        let run = run_support_transform_packet(&mut space, &test_packet(), 0).unwrap();
        assert_eq!(run.performed, 0);
        assert!(run.has_work);
        assert!(!contains(&mut space, b"(answer a)"));
    }

    #[test]
    fn scheduler_uses_mork_compact_expression_order() {
        let mut space = Space::new();
        insert(&mut space, b"(ready)");
        insert(
            &mut space,
            "(exec é (, (ready)) (O (+ (winner unicode)) (- (ready))))".as_bytes(),
        );
        insert(
            &mut space,
            b"(exec aa (, (ready)) (O (+ (winner ascii)) (- (ready))))",
        );
        let run = run_support_transform_packet(&mut space, &test_packet(), 8).unwrap();
        assert_eq!(run.performed, 2);
        assert!(contains(&mut space, b"(winner ascii)"));
        assert!(!contains(&mut space, b"(winner unicode)"));
    }
}
