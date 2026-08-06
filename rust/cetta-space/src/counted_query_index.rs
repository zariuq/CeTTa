use crate::{
    CountedEntry, CountedGeneralQueryCursor, counted_entries, counted_exact_entry,
    stable_bridge_expr_bytes, stable_bridge_expr_packet_bytes,
};
use mork::space::Space;
use mork_expr::{Expr, ExprEnv, unify};
use std::cmp::Ordering;
use std::collections::{BTreeMap, BTreeSet};
use std::ops::Bound;
use std::sync::Arc;

use super::counted_pathmap::encode_counted_multi_ref_row;

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct FlatCountedIndexStats {
    pub catalog_builds: u64,
    pub catalog_rows_scanned: u64,
    pub access_path_builds: u64,
    pub access_path_rows_indexed: u64,
    pub incremental_updates: u64,
    pub plan_builds: u64,
    pub plan_cache_hits: u64,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct FlatCountedCursorStats {
    pub trie_seeks: u64,
    pub trie_descents: u64,
    pub rows_emitted: u64,
    pub rows_aggregated: u64,
    pub max_frame_cells: usize,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum FlatCountedQueryAdmission {
    Prepared {
        key: Vec<u8>,
        factor_count: u32,
        has_residual: bool,
        has_exact_partition: bool,
    },
    Unsupported {
        reason: String,
    },
}

#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord)]
struct RelationKey {
    head: Vec<u8>,
    arity: usize,
}

type RowKey = Vec<Vec<u8>>;

#[derive(Clone, Debug, PartialEq, Eq)]
struct FlatFactRow {
    columns: RowKey,
    expr_bytes: Vec<u8>,
    expr_packet: Vec<u8>,
    count: u32,
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct FlatTerminal {
    row_key: RowKey,
    expr_bytes: Vec<u8>,
    expr_packet: Vec<u8>,
    count: u32,
}

#[derive(Clone, Debug, Default)]
struct FlatTrieNode {
    children: BTreeMap<Vec<u8>, usize>,
    terminal: Option<FlatTerminal>,
    subtree_rows: usize,
}

#[derive(Clone, Debug)]
struct FlatAccessPath {
    column_order: Vec<usize>,
    nodes: Vec<FlatTrieNode>,
}

impl FlatAccessPath {
    fn new(column_order: Vec<usize>) -> Self {
        Self {
            column_order,
            nodes: vec![FlatTrieNode::default()],
        }
    }

    fn insert(&mut self, row: &FlatFactRow) {
        let mut node = 0usize;
        let mut path = vec![node];
        for &column in &self.column_order {
            let key = row.columns[column].clone();
            let next = if let Some(next) = self.nodes[node].children.get(&key) {
                *next
            } else {
                let next = self.nodes.len();
                self.nodes.push(FlatTrieNode::default());
                self.nodes[node].children.insert(key, next);
                next
            };
            node = next;
            path.push(node);
        }
        let was_empty = self.nodes[node].terminal.is_none();
        self.nodes[node].terminal = Some(FlatTerminal {
            row_key: row.columns.clone(),
            expr_bytes: row.expr_bytes.clone(),
            expr_packet: row.expr_packet.clone(),
            count: row.count,
        });
        if was_empty {
            for node in path {
                self.nodes[node].subtree_rows = self.nodes[node].subtree_rows.saturating_add(1);
            }
        }
    }

    fn remove(&mut self, row: &FlatFactRow) {
        let mut node = 0usize;
        let mut path = vec![node];
        let mut edges = Vec::<(usize, Vec<u8>, usize)>::new();
        for &column in &self.column_order {
            let key = row.columns[column].clone();
            let Some(&next) = self.nodes[node].children.get(&key) else {
                return;
            };
            edges.push((node, key, next));
            node = next;
            path.push(node);
        }
        if self.nodes[node].terminal.take().is_none() {
            return;
        }
        for &path_node in &path {
            self.nodes[path_node].subtree_rows =
                self.nodes[path_node].subtree_rows.saturating_sub(1);
        }
        for (parent, key, child) in edges.into_iter().rev() {
            if self.nodes[child].terminal.is_none() && self.nodes[child].children.is_empty() {
                self.nodes[parent].children.remove(&key);
            } else {
                break;
            }
        }
    }

    fn prefix_entries(&self, constant_values: &[Vec<u8>]) -> Vec<CountedEntry> {
        let mut node = 0usize;
        for value in constant_values {
            let Some(next) = self.nodes[node].children.get(value).copied() else {
                return Vec::new();
            };
            node = next;
        }

        let mut pending = vec![node];
        let mut entries = Vec::new();
        while let Some(next) = pending.pop() {
            if let Some(terminal) = &self.nodes[next].terminal {
                entries.push(CountedEntry {
                    full_key: terminal.expr_bytes.clone(),
                    atom_expr_bytes: terminal.expr_bytes.clone(),
                    count: terminal.count,
                });
            }
            pending.extend(self.nodes[next].children.values().copied());
        }
        entries
    }
}

#[derive(Clone, Debug, Default)]
struct FlatRelation {
    rows: BTreeMap<RowKey, FlatFactRow>,
    unsupported_rows: BTreeMap<Vec<u8>, u32>,
    access_paths: BTreeMap<Vec<usize>, FlatAccessPath>,
}

impl FlatRelation {
    fn upsert_row(&mut self, row: FlatFactRow) {
        if let Some(previous) = self.rows.insert(row.columns.clone(), row.clone()) {
            for path in self.access_paths.values_mut() {
                path.remove(&previous);
            }
        }
        for path in self.access_paths.values_mut() {
            path.insert(&row);
        }
    }

    fn remove_row(&mut self, key: &RowKey) {
        let Some(previous) = self.rows.remove(key) else {
            return;
        };
        for path in self.access_paths.values_mut() {
            path.remove(&previous);
        }
    }

    fn ensure_access_path(&mut self, column_order: &[usize], stats: &mut FlatCountedIndexStats) {
        if self.access_paths.contains_key(column_order) {
            return;
        }
        let mut path = FlatAccessPath::new(column_order.to_vec());
        for row in self.rows.values() {
            path.insert(row);
        }
        stats.access_path_builds = stats.access_path_builds.saturating_add(1);
        stats.access_path_rows_indexed = stats
            .access_path_rows_indexed
            .saturating_add(self.rows.len() as u64);
        self.access_paths.insert(column_order.to_vec(), path);
    }
}

#[derive(Clone, Debug)]
enum CatalogEntryClass {
    Ground {
        relation: RelationKey,
        row_key: RowKey,
    },
    UnsupportedRelation {
        relation: RelationKey,
    },
    WildcardRelation {
        arity: Option<usize>,
    },
    Irrelevant,
}

#[derive(Clone, Debug, Default)]
struct FlatCatalog {
    entries: BTreeMap<Vec<u8>, CatalogEntryClass>,
    relations: BTreeMap<RelationKey, FlatRelation>,
    wildcard_any: BTreeMap<Vec<u8>, u32>,
    wildcard_by_arity: BTreeMap<usize, BTreeMap<Vec<u8>, u32>>,
}

impl FlatCatalog {
    fn add_class(
        &mut self,
        expr_key: Vec<u8>,
        class: CatalogEntryClass,
        row: Option<FlatFactRow>,
        count: u32,
    ) {
        match &class {
            CatalogEntryClass::Ground { relation, .. } => {
                if let Some(row) = row {
                    self.relations
                        .entry(relation.clone())
                        .or_default()
                        .upsert_row(row);
                }
            }
            CatalogEntryClass::UnsupportedRelation { relation } => {
                self.relations
                    .entry(relation.clone())
                    .or_default()
                    .unsupported_rows
                    .insert(expr_key.clone(), count);
            }
            CatalogEntryClass::WildcardRelation { arity: Some(arity) } => {
                self.wildcard_by_arity
                    .entry(*arity)
                    .or_default()
                    .insert(expr_key.clone(), count);
            }
            CatalogEntryClass::WildcardRelation { arity: None } => {
                self.wildcard_any.insert(expr_key.clone(), count);
            }
            CatalogEntryClass::Irrelevant => {}
        }
        self.entries.insert(expr_key, class);
    }

    fn remove_class(&mut self, expr_key: &[u8], class: CatalogEntryClass) {
        match class {
            CatalogEntryClass::Ground { relation, row_key } => {
                if let Some(relation) = self.relations.get_mut(&relation) {
                    relation.remove_row(&row_key);
                }
            }
            CatalogEntryClass::UnsupportedRelation { relation } => {
                if let Some(relation) = self.relations.get_mut(&relation) {
                    relation.unsupported_rows.remove(expr_key);
                }
            }
            CatalogEntryClass::WildcardRelation { arity: Some(arity) } => {
                if let Some(rows) = self.wildcard_by_arity.get_mut(&arity) {
                    rows.remove(expr_key);
                    if rows.is_empty() {
                        self.wildcard_by_arity.remove(&arity);
                    }
                }
            }
            CatalogEntryClass::WildcardRelation { arity: None } => {
                self.wildcard_any.remove(expr_key);
            }
            CatalogEntryClass::Irrelevant => {}
        }
    }

    fn has_wildcard_for_arity(&self, arity: usize) -> bool {
        !self.wildcard_any.is_empty()
            || self
                .wildcard_by_arity
                .get(&arity)
                .is_some_and(|rows| !rows.is_empty())
    }

    fn residual_entries_for(&self, relation: &RelationKey) -> Vec<CountedEntry> {
        let mut rows = BTreeMap::<Vec<u8>, u32>::new();
        if let Some(relation_rows) = self.relations.get(relation) {
            rows.extend(
                relation_rows
                    .unsupported_rows
                    .iter()
                    .map(|(expr, count)| (expr.clone(), *count)),
            );
        }
        rows.extend(
            self.wildcard_any
                .iter()
                .map(|(expr, count)| (expr.clone(), *count)),
        );
        if let Some(arity_rows) = self.wildcard_by_arity.get(&relation.arity) {
            rows.extend(
                arity_rows
                    .iter()
                    .map(|(expr, count)| (expr.clone(), *count)),
            );
        }
        rows.into_iter()
            .map(|(atom_expr_bytes, count)| CountedEntry {
                full_key: atom_expr_bytes.clone(),
                atom_expr_bytes,
                count,
            })
            .collect()
    }
}

#[derive(Clone, Debug)]
struct QueryFactor {
    relation: RelationKey,
    constants: BTreeMap<usize, Vec<u8>>,
    variables: BTreeMap<u8, Vec<usize>>,
}

#[derive(Clone, Debug)]
struct FactorPlan {
    relation: RelationKey,
    access_shape: Vec<usize>,
    constant_values: Vec<Vec<u8>>,
    group_widths: Vec<usize>,
}

#[derive(Clone, Debug)]
struct FlatQueryPlan {
    factor_count: u32,
    variable_order: Vec<u8>,
    output_slots: Vec<u8>,
    factors: Vec<FactorPlan>,
    has_residual: bool,
    empty: bool,
}

#[derive(Clone, Debug, Default)]
pub struct FlatCountedQueryIndex {
    catalog: Option<FlatCatalog>,
    plans: BTreeMap<Vec<u8>, FlatQueryPlan>,
    stats: FlatCountedIndexStats,
}

impl FlatCountedQueryIndex {
    pub fn stats(&self) -> &FlatCountedIndexStats {
        &self.stats
    }

    pub fn is_catalog_built(&self) -> bool {
        self.catalog.is_some()
    }

    fn classify_entry(
        space: &Space,
        expr_bytes: &[u8],
        count: u32,
    ) -> Result<(CatalogEntryClass, Option<FlatFactRow>), String> {
        let expr = Expr {
            ptr: expr_bytes.as_ptr().cast_mut(),
        };
        if expr.is_ground() && expr.arity().is_none() {
            return Ok((CatalogEntryClass::Irrelevant, None));
        }
        if expr.arity().is_none() {
            return Ok((CatalogEntryClass::WildcardRelation { arity: None }, None));
        }

        let mut args = Vec::new();
        ExprEnv::new(0, expr).args(&mut args);
        if args.is_empty() {
            return Ok((CatalogEntryClass::Irrelevant, None));
        }
        let arity = args.len() - 1;
        let head = args[0];
        if head.var_opt().is_some() || !head.subsexpr().is_ground() {
            return Ok((
                CatalogEntryClass::WildcardRelation { arity: Some(arity) },
                None,
            ));
        }
        let relation = RelationKey {
            head: stable_bridge_expr_packet_bytes(space, head.subsexpr())?,
            arity,
        };
        if !expr.is_ground() {
            return Ok((
                CatalogEntryClass::UnsupportedRelation {
                    relation: relation.clone(),
                },
                None,
            ));
        }

        let columns = args[1..]
            .iter()
            .map(|arg| stable_bridge_expr_packet_bytes(space, arg.subsexpr()))
            .collect::<Result<Vec<_>, _>>()?;
        let row = FlatFactRow {
            columns: columns.clone(),
            expr_bytes: expr_bytes.to_vec(),
            expr_packet: stable_bridge_expr_packet_bytes(space, expr)?,
            count,
        };
        Ok((
            CatalogEntryClass::Ground {
                relation,
                row_key: columns,
            },
            Some(row),
        ))
    }

    fn ensure_catalog(&mut self, space: &Space) -> Result<(), String> {
        if self.catalog.is_some() {
            return Ok(());
        }
        let entries = counted_entries(space)?;
        let mut catalog = FlatCatalog::default();
        for entry in entries {
            let (class, row) = Self::classify_entry(space, &entry.atom_expr_bytes, entry.count)?;
            catalog.add_class(entry.atom_expr_bytes, class, row, entry.count);
            self.stats.catalog_rows_scanned = self.stats.catalog_rows_scanned.saturating_add(1);
        }
        self.stats.catalog_builds = self.stats.catalog_builds.saturating_add(1);
        self.catalog = Some(catalog);
        Ok(())
    }

    pub fn observe_expr(&mut self, space: &Space, expr_bytes: &[u8]) -> Result<(), String> {
        let Some(catalog) = self.catalog.as_mut() else {
            return Ok(());
        };
        if let Some(previous) = catalog.entries.remove(expr_bytes) {
            catalog.remove_class(expr_bytes, previous);
        }
        if let Some(entry) = counted_exact_entry(space, expr_bytes)? {
            let (class, row) = Self::classify_entry(space, &entry.atom_expr_bytes, entry.count)?;
            catalog.add_class(entry.atom_expr_bytes, class, row, entry.count);
        }
        self.plans.clear();
        self.stats.incremental_updates = self.stats.incremental_updates.saturating_add(1);
        Ok(())
    }

    pub fn observe_exprs<I, B>(&mut self, space: &Space, exprs: I) -> Result<(), String>
    where
        I: IntoIterator<Item = B>,
        B: AsRef<[u8]>,
    {
        let mut seen = BTreeSet::<Vec<u8>>::new();
        for expr in exprs {
            let expr = expr.as_ref();
            if seen.insert(expr.to_vec()) {
                self.observe_expr(space, expr)?;
            }
        }
        Ok(())
    }

    fn parse_query_factor(space: &Space, factor: ExprEnv) -> Result<QueryFactor, String> {
        if factor.var_opt().is_some() || factor.subsexpr().arity().is_none() {
            return Err("flat indexed query factors must be compound expressions".to_string());
        }
        let mut args = Vec::new();
        factor.args(&mut args);
        if args.is_empty() {
            return Err("flat indexed query factors require a relation head".to_string());
        }
        let head = args[0];
        if head.var_opt().is_some() || !head.subsexpr().is_ground() {
            return Err("flat indexed query relation heads must be ground".to_string());
        }
        let relation = RelationKey {
            head: stable_bridge_expr_packet_bytes(space, head.subsexpr())?,
            arity: args.len() - 1,
        };
        let mut constants = BTreeMap::new();
        let mut variables = BTreeMap::<u8, Vec<usize>>::new();
        for (column, arg) in args[1..].iter().enumerate() {
            if let Some((side, slot)) = arg.var_opt() {
                if side != 0 {
                    return Err(
                        "flat indexed query encountered a non-query variable environment"
                            .to_string(),
                    );
                }
                variables.entry(slot).or_default().push(column);
            } else if arg.subsexpr().is_ground() {
                constants.insert(
                    column,
                    stable_bridge_expr_packet_bytes(space, arg.subsexpr())?,
                );
            } else {
                return Err(
                    "flat indexed query supports only whole-column variables or ground columns"
                        .to_string(),
                );
            }
        }
        Ok(QueryFactor {
            relation,
            constants,
            variables,
        })
    }

    fn row_satisfies_factor_prefix(row: &FlatFactRow, factor: &QueryFactor) -> bool {
        if factor
            .constants
            .iter()
            .any(|(&column, value)| row.columns.get(column) != Some(value))
        {
            return false;
        }
        factor.variables.values().all(|positions| {
            positions
                .first()
                .and_then(|first| row.columns.get(*first))
                .map(|first_value| {
                    positions
                        .iter()
                        .skip(1)
                        .all(|column| row.columns.get(*column) == Some(first_value))
                })
                .unwrap_or(false)
        })
    }

    fn choose_variable_order(
        catalog: &FlatCatalog,
        factors: &[QueryFactor],
        slots: &BTreeSet<u8>,
    ) -> Vec<u8> {
        let mut scored = slots
            .iter()
            .map(|&slot| {
                let mut best_domain = usize::MAX;
                let mut occurrences = 0usize;
                for factor in factors {
                    let Some(positions) = factor.variables.get(&slot) else {
                        continue;
                    };
                    occurrences = occurrences.saturating_add(positions.len());
                    let Some(relation) = catalog.relations.get(&factor.relation) else {
                        best_domain = 0;
                        continue;
                    };
                    let mut domain = BTreeSet::<Vec<u8>>::new();
                    for row in relation.rows.values() {
                        if !Self::row_satisfies_factor_prefix(row, factor) {
                            continue;
                        }
                        if let Some(value) = row.columns.get(positions[0]) {
                            domain.insert(value.clone());
                        }
                    }
                    best_domain = best_domain.min(domain.len());
                }
                (slot, best_domain, occurrences)
            })
            .collect::<Vec<_>>();
        scored.sort_by(
            |(lhs_slot, lhs_domain, lhs_occ), (rhs_slot, rhs_domain, rhs_occ)| {
                lhs_domain
                    .cmp(rhs_domain)
                    .then_with(|| rhs_occ.cmp(lhs_occ))
                    .then_with(|| lhs_slot.cmp(rhs_slot))
            },
        );
        scored.into_iter().map(|(slot, _, _)| slot).collect()
    }

    pub fn prepare(
        &mut self,
        space: &Space,
        pattern_expr_bytes: &[u8],
    ) -> Result<FlatCountedQueryAdmission, String> {
        self.ensure_catalog(space)?;
        let pattern_expr = Expr {
            ptr: pattern_expr_bytes.as_ptr().cast_mut(),
        };
        let factor_count = pattern_expr
            .arity()
            .ok_or_else(|| "flat indexed query expected a wrapped conjunction".to_string())?
            .checked_sub(1)
            .ok_or_else(|| "flat indexed query expected a wrapped conjunction".to_string())?;
        if factor_count == 0 {
            return Ok(FlatCountedQueryAdmission::Unsupported {
                reason: "flat indexed query requires at least one factor".to_string(),
            });
        }
        let mut args = Vec::new();
        ExprEnv::new(0, pattern_expr).args(&mut args);
        let mut factors = Vec::with_capacity(factor_count as usize);
        for factor in &args[1..] {
            match Self::parse_query_factor(space, *factor) {
                Ok(factor) => factors.push(factor),
                Err(reason) => {
                    return Ok(FlatCountedQueryAdmission::Unsupported { reason });
                }
            }
        }

        let key = stable_bridge_expr_bytes(space, pattern_expr)?;
        let catalog = self
            .catalog
            .as_ref()
            .expect("flat query catalog was built above");
        let has_residual = factors.iter().any(|factor| {
            catalog.has_wildcard_for_arity(factor.relation.arity)
                || catalog
                    .relations
                    .get(&factor.relation)
                    .is_some_and(|relation| !relation.unsupported_rows.is_empty())
        });
        if let Some(plan) = self.plans.get(&key) {
            self.stats.plan_cache_hits = self.stats.plan_cache_hits.saturating_add(1);
            return Ok(FlatCountedQueryAdmission::Prepared {
                key,
                factor_count: plan.factor_count,
                has_residual: plan.has_residual,
                has_exact_partition: !plan.empty,
            });
        }

        let output_slots = factors
            .iter()
            .flat_map(|factor| factor.variables.keys().copied())
            .collect::<BTreeSet<_>>();
        let variable_order = Self::choose_variable_order(catalog, &factors, &output_slots);
        let mut factor_plans = Vec::with_capacity(factors.len());
        let mut has_exact_partition = true;
        for factor in &factors {
            let constant_positions = factor.constants.keys().copied().collect::<Vec<_>>();
            let mut access_shape = constant_positions.clone();
            let mut group_widths = Vec::with_capacity(variable_order.len());
            for slot in &variable_order {
                let positions = factor.variables.get(slot).cloned().unwrap_or_default();
                group_widths.push(positions.len());
                access_shape.extend(positions);
            }
            if access_shape.len() != factor.relation.arity {
                return Ok(FlatCountedQueryAdmission::Unsupported {
                    reason: "flat indexed query did not account for every relation column"
                        .to_string(),
                });
            }
            let constant_values = constant_positions
                .iter()
                .map(|position| {
                    factor
                        .constants
                        .get(position)
                        .expect("constant position came from the same map")
                        .clone()
                })
                .collect::<Vec<_>>();
            let factor_has_exact = if let Some(relation) = self
                .catalog
                .as_mut()
                .and_then(|catalog| catalog.relations.get_mut(&factor.relation))
            {
                relation.ensure_access_path(&access_shape, &mut self.stats);
                relation
                    .access_paths
                    .get(&access_shape)
                    .is_some_and(|path| {
                        !path.prefix_entries(&constant_values).is_empty()
                    })
            } else {
                false
            };
            has_exact_partition &= factor_has_exact;
            factor_plans.push(FactorPlan {
                relation: factor.relation.clone(),
                access_shape,
                constant_values,
                group_widths,
            });
        }

        let plan = FlatQueryPlan {
            factor_count: factor_count as u32,
            variable_order,
            output_slots: output_slots.into_iter().collect(),
            factors: factor_plans,
            has_residual,
            empty: !has_exact_partition,
        };
        self.stats.plan_builds = self.stats.plan_builds.saturating_add(1);
        self.plans.insert(key.clone(), plan);
        Ok(FlatCountedQueryAdmission::Prepared {
            key,
            factor_count: factor_count as u32,
            has_residual,
            has_exact_partition,
        })
    }

    fn plan(&self, key: &[u8]) -> Option<&FlatQueryPlan> {
        self.plans.get(key)
    }

    fn access_path(&self, factor: &FactorPlan) -> Option<&FlatAccessPath> {
        self.catalog
            .as_ref()?
            .relations
            .get(&factor.relation)?
            .access_paths
            .get(&factor.access_shape)
    }

    pub fn residual_candidate_lists(
        &self,
        _space: &Space,
        pattern_expr_bytes: &[u8],
        key: &[u8],
    ) -> Result<Option<Vec<Vec<CountedEntry>>>, String> {
        let Some(plan) = self.plan(key) else {
            return Err("flat indexed residual query plan is unavailable".to_string());
        };
        if !plan.has_residual {
            return Ok(None);
        }
        let catalog = self
            .catalog
            .as_ref()
            .ok_or_else(|| "flat indexed residual catalog is unavailable".to_string())?;
        let pattern_expr = Expr {
            ptr: pattern_expr_bytes.as_ptr().cast_mut(),
        };
        let mut args = Vec::with_capacity(plan.factors.len() + 1);
        ExprEnv::new(0, pattern_expr).args(&mut args);
        let query_factors = &args[1..];
        if query_factors.len() != plan.factors.len() {
            return Err("flat indexed residual factor count drifted from its plan".to_string());
        }

        let mut candidate_lists = Vec::with_capacity(query_factors.len());
        for (factor, factor_plan) in query_factors.iter().zip(&plan.factors) {
            let mut candidates = self
                .access_path(factor_plan)
                .map(|path| path.prefix_entries(&factor_plan.constant_values))
                .unwrap_or_default()
                .into_iter()
                .map(|entry| (entry.atom_expr_bytes.clone(), entry))
                .collect::<BTreeMap<_, _>>();
            for entry in catalog.residual_entries_for(&factor_plan.relation) {
                let atom_expr = Expr {
                    ptr: entry.atom_expr_bytes.as_ptr().cast_mut(),
                };
                if unify(vec![(*factor, ExprEnv::new(1, atom_expr))]).is_ok() {
                    candidates.insert(entry.atom_expr_bytes.clone(), entry);
                }
            }
            candidate_lists.push(candidates.into_values().collect());
        }
        Ok(Some(candidate_lists))
    }

    pub fn residual_cursor(
        &self,
        space: &Space,
        pattern_expr_bytes: &[u8],
        key: &[u8],
    ) -> Result<Option<CountedGeneralQueryCursor>, String> {
        let Some(candidate_lists) =
            self.residual_candidate_lists(space, pattern_expr_bytes, key)?
        else {
            return Ok(None);
        };
        CountedGeneralQueryCursor::new_residual_partition(pattern_expr_bytes, candidate_lists)
            .map(Some)
    }

    fn prepare_source_for_plan(
        &mut self,
        space: &Space,
        plan: &FlatQueryPlan,
    ) -> Result<(), String> {
        self.ensure_catalog(space)?;
        for factor in &plan.factors {
            if let Some(relation) = self
                .catalog
                .as_mut()
                .and_then(|catalog| catalog.relations.get_mut(&factor.relation))
            {
                relation.ensure_access_path(&factor.access_shape, &mut self.stats);
            }
        }
        Ok(())
    }
}

#[derive(Clone, Debug)]
struct CursorFrame {
    input_nodes: Vec<usize>,
    last_value: Option<Vec<u8>>,
    output_nodes: Vec<usize>,
}

pub struct FlatCountedQueryCursor {
    factor_indexes: Vec<Arc<FlatCountedQueryIndex>>,
    plan: FlatQueryPlan,
    root_nodes: Vec<usize>,
    frames: Vec<CursorFrame>,
    assignments: BTreeMap<u8, Vec<u8>>,
    depth: usize,
    ground_emitted: bool,
    done: bool,
    stats: FlatCountedCursorStats,
}

impl FlatCountedQueryCursor {
    pub fn new(index: Arc<FlatCountedQueryIndex>, key: &[u8]) -> Result<Self, String> {
        let factor_count = index
            .plan(key)
            .map(|plan| plan.factors.len())
            .ok_or_else(|| "flat indexed query plan is unavailable".to_string())?;
        Self::new_with_factor_indexes(index.clone(), key, vec![index; factor_count])
    }

    fn new_with_factor_indexes(
        plan_index: Arc<FlatCountedQueryIndex>,
        key: &[u8],
        factor_indexes: Vec<Arc<FlatCountedQueryIndex>>,
    ) -> Result<Self, String> {
        let plan = plan_index
            .plan(key)
            .cloned()
            .ok_or_else(|| "flat indexed query plan is unavailable".to_string())?;
        if factor_indexes.len() != plan.factors.len() {
            return Err("flat indexed query factor-source count mismatch".to_string());
        }
        let mut root_nodes = Vec::with_capacity(plan.factors.len());
        let mut done = plan.empty;
        for (factor_idx, factor) in plan.factors.iter().enumerate() {
            let Some(path) = factor_indexes[factor_idx].access_path(factor) else {
                done = true;
                root_nodes.push(0);
                continue;
            };
            let mut node = 0usize;
            for value in &factor.constant_values {
                let Some(&next) = path.nodes[node].children.get(value) else {
                    done = true;
                    break;
                };
                node = next;
            }
            root_nodes.push(node);
        }
        Ok(Self {
            factor_indexes,
            plan,
            root_nodes,
            frames: Vec::new(),
            assignments: BTreeMap::new(),
            depth: 0,
            ground_emitted: false,
            done,
            stats: FlatCountedCursorStats::default(),
        })
    }

    pub fn factor_count(&self) -> u32 {
        self.plan.factor_count
    }

    pub fn stats(&self) -> &FlatCountedCursorStats {
        &self.stats
    }

    fn access_path(&self, factor_idx: usize) -> Option<&FlatAccessPath> {
        self.factor_indexes
            .get(factor_idx)?
            .access_path(self.plan.factors.get(factor_idx)?)
    }

    fn factor_next_valid(
        &mut self,
        factor_idx: usize,
        input_node: usize,
        variable_depth: usize,
        bound: Bound<&Vec<u8>>,
    ) -> Option<(Vec<u8>, usize)> {
        let factor = self.plan.factors[factor_idx].clone();
        let width = factor.group_widths[variable_depth];
        if width == 0 {
            return None;
        }
        let factor_index = self.factor_indexes[factor_idx].clone();
        let path = factor_index.access_path(&factor)?;
        let mut candidates = path.nodes[input_node]
            .children
            .range::<Vec<u8>, _>((bound, Bound::Unbounded));
        while let Some((value, &first_node)) = candidates.next() {
            self.stats.trie_seeks = self.stats.trie_seeks.saturating_add(1);
            let mut node = first_node;
            let mut valid = true;
            for _ in 1..width {
                self.stats.trie_descents = self.stats.trie_descents.saturating_add(1);
                let Some(&next) = path.nodes[node].children.get(value) else {
                    valid = false;
                    break;
                };
                node = next;
            }
            if valid {
                return Some((value.clone(), node));
            }
        }
        None
    }

    fn seek_join_value(
        &mut self,
        input_nodes: &[usize],
        variable_depth: usize,
        last_value: Option<&Vec<u8>>,
    ) -> Option<(Vec<u8>, Vec<usize>)> {
        let participants = self
            .plan
            .factors
            .iter()
            .enumerate()
            .filter_map(|(idx, factor)| (factor.group_widths[variable_depth] != 0).then_some(idx))
            .collect::<Vec<_>>();
        let &first = participants.first()?;
        let initial_bound = last_value.map(Bound::Excluded).unwrap_or(Bound::Unbounded);
        let (mut candidate, _) =
            self.factor_next_valid(first, input_nodes[first], variable_depth, initial_bound)?;

        loop {
            let mut maximum = candidate.clone();
            let mut outputs = input_nodes.to_vec();
            let mut all_equal = true;
            for &factor_idx in &participants {
                let (found, after) = self.factor_next_valid(
                    factor_idx,
                    input_nodes[factor_idx],
                    variable_depth,
                    Bound::Included(&candidate),
                )?;
                match found.cmp(&maximum) {
                    Ordering::Greater => {
                        maximum = found.clone();
                    }
                    Ordering::Less | Ordering::Equal => {}
                }
                if found != candidate {
                    all_equal = false;
                }
                outputs[factor_idx] = after;
            }
            if all_equal && maximum == candidate {
                return Some((candidate, outputs));
            }
            candidate = maximum;
        }
    }

    fn terminal_row(&self, factor_idx: usize, node: usize) -> Option<&FlatTerminal> {
        self.access_path(factor_idx)?
            .nodes
            .get(node)?
            .terminal
            .as_ref()
    }

    fn encode_current_row(&self, nodes: &[usize]) -> Result<Vec<u8>, String> {
        let factor_counts = nodes
            .iter()
            .enumerate()
            .map(|(factor_idx, &node)| {
                self.terminal_row(factor_idx, node)
                    .map(|terminal| terminal.count)
                    .ok_or_else(|| {
                        "flat indexed query reached a non-terminal factor state".to_string()
                    })
            })
            .collect::<Result<Vec<_>, _>>()?;
        let bindings = self
            .plan
            .output_slots
            .iter()
            .map(|slot| {
                self.assignments
                    .get(slot)
                    .cloned()
                    .map(|value| (*slot, value))
                    .ok_or_else(|| format!("flat indexed query did not bind query slot {slot}"))
            })
            .collect::<Result<Vec<_>, _>>()?;
        encode_counted_multi_ref_row(&factor_counts, &bindings)
    }

    fn next_terminal_nodes(&mut self) -> Option<Vec<usize>> {
        if self.done {
            return None;
        }
        if self.plan.variable_order.is_empty() {
            if self.ground_emitted {
                self.done = true;
                return None;
            }
            self.ground_emitted = true;
            return Some(self.root_nodes.clone());
        }

        loop {
            if self.depth == self.plan.variable_order.len() {
                let nodes = self
                    .frames
                    .last()
                    .map(|frame| frame.output_nodes.clone())
                    .unwrap_or_else(|| self.root_nodes.clone());
                self.depth = self.depth.saturating_sub(1);
                return Some(nodes);
            }

            if self.frames.len() == self.depth {
                let input_nodes = if self.depth == 0 {
                    self.root_nodes.clone()
                } else {
                    self.frames[self.depth - 1].output_nodes.clone()
                };
                self.frames.push(CursorFrame {
                    input_nodes,
                    last_value: None,
                    output_nodes: Vec::new(),
                });
            }
            let input_nodes = self.frames[self.depth].input_nodes.clone();
            let last_value = self.frames[self.depth].last_value.clone();
            if let Some((value, output_nodes)) =
                self.seek_join_value(&input_nodes, self.depth, last_value.as_ref())
            {
                let slot = self.plan.variable_order[self.depth];
                self.assignments.insert(slot, value.clone());
                self.frames[self.depth].last_value = Some(value);
                self.frames[self.depth].output_nodes = output_nodes;
                self.frames.truncate(self.depth + 1);
                self.depth += 1;
                self.stats.max_frame_cells = self
                    .stats
                    .max_frame_cells
                    .max(self.frames.len().saturating_mul(self.plan.factors.len()));
                continue;
            }

            self.frames.pop();
            if self.depth == 0 {
                self.done = true;
                return None;
            }
            self.depth -= 1;
        }
    }

    fn row_multiplicity(&self, nodes: &[usize]) -> Result<u64, String> {
        nodes
            .iter()
            .enumerate()
            .try_fold(1u64, |product, (factor_idx, &node)| {
                let count = self
                    .terminal_row(factor_idx, node)
                    .map(|terminal| u64::from(terminal.count))
                    .ok_or_else(|| {
                        "flat indexed query reached a non-terminal factor state".to_string()
                    })?;
                product.checked_mul(count).ok_or_else(|| {
                    "flat indexed query multiplicity exceeds u64 aggregate capacity".to_string()
                })
            })
    }

    pub fn next_packet_row(&mut self) -> Result<Option<Vec<u8>>, String> {
        let Some(nodes) = self.next_terminal_nodes() else {
            return Ok(None);
        };
        let row = self.encode_current_row(&nodes)?;
        self.stats.rows_emitted = self.stats.rows_emitted.saturating_add(1);
        Ok(Some(row))
    }

    /// Consumes the remaining join traversal while summing exact bag
    /// multiplicities. No binding row or expression packet is reconstructed.
    pub fn count_remaining(&mut self) -> Result<u64, String> {
        let mut total = 0u64;
        while let Some(nodes) = self.next_terminal_nodes() {
            let multiplicity = self.row_multiplicity(&nodes)?;
            total = total.checked_add(multiplicity).ok_or_else(|| {
                "flat indexed query count exceeds u64 aggregate capacity".to_string()
            })?;
            self.stats.rows_aggregated = self.stats.rows_aggregated.saturating_add(1);
        }
        Ok(total)
    }
}

pub struct FlatSemiNaiveQueryCursor {
    variants: Vec<FlatCountedQueryCursor>,
    next_variant: usize,
    stats: FlatCountedCursorStats,
}

impl FlatSemiNaiveQueryCursor {
    pub fn new(
        known_index: Arc<FlatCountedQueryIndex>,
        old_space: &Space,
        old_index: &mut Arc<FlatCountedQueryIndex>,
        delta_space: &Space,
        delta_index: &mut Arc<FlatCountedQueryIndex>,
        key: &[u8],
    ) -> Result<Self, String> {
        let plan = known_index
            .plan(key)
            .cloned()
            .ok_or_else(|| "semi-naive query plan is unavailable".to_string())?;
        Arc::make_mut(old_index).prepare_source_for_plan(old_space, &plan)?;
        Arc::make_mut(delta_index).prepare_source_for_plan(delta_space, &plan)?;

        let mut variants = Vec::with_capacity(plan.factors.len());
        for pivot in 0..plan.factors.len() {
            if delta_index.access_path(&plan.factors[pivot]).is_none() {
                continue;
            }
            let factor_indexes = (0..plan.factors.len())
                .map(|factor_idx| {
                    if factor_idx < pivot {
                        old_index.clone()
                    } else if factor_idx == pivot {
                        delta_index.clone()
                    } else {
                        known_index.clone()
                    }
                })
                .collect::<Vec<_>>();
            variants.push(FlatCountedQueryCursor::new_with_factor_indexes(
                known_index.clone(),
                key,
                factor_indexes,
            )?);
        }
        Ok(Self {
            variants,
            next_variant: 0,
            stats: FlatCountedCursorStats::default(),
        })
    }

    pub fn stats(&self) -> &FlatCountedCursorStats {
        &self.stats
    }

    fn refresh_stats(&mut self) {
        let mut aggregate = FlatCountedCursorStats::default();
        for variant in &self.variants {
            let stats = variant.stats();
            aggregate.trie_seeks = aggregate.trie_seeks.saturating_add(stats.trie_seeks);
            aggregate.trie_descents = aggregate.trie_descents.saturating_add(stats.trie_descents);
            aggregate.rows_emitted = aggregate.rows_emitted.saturating_add(stats.rows_emitted);
            aggregate.rows_aggregated = aggregate
                .rows_aggregated
                .saturating_add(stats.rows_aggregated);
            aggregate.max_frame_cells = aggregate.max_frame_cells.max(stats.max_frame_cells);
        }
        self.stats = aggregate;
    }

    pub fn next_packet_row(&mut self) -> Result<Option<Vec<u8>>, String> {
        while self.next_variant < self.variants.len() {
            if let Some(row) = self.variants[self.next_variant].next_packet_row()? {
                self.refresh_stats();
                return Ok(Some(row));
            }
            self.next_variant += 1;
        }
        self.refresh_stats();
        Ok(None)
    }

    pub fn count_remaining(&mut self) -> Result<u64, String> {
        let mut total = 0u64;
        while self.next_variant < self.variants.len() {
            total = total
                .checked_add(self.variants[self.next_variant].count_remaining()?)
                .ok_or_else(|| {
                    "semi-naive query count exceeds u64 aggregate capacity".to_string()
                })?;
            self.next_variant += 1;
        }
        self.refresh_stats();
        Ok(total)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{bridge_parse_single_expr, counted_insert_expr, counted_remove_one_expr};
    use std::time::Instant;

    fn parse(space: &mut Space, text: &str) -> Vec<u8> {
        bridge_parse_single_expr(space, text.as_bytes()).expect("test expression should parse")
    }

    fn prepared_key(admission: FlatCountedQueryAdmission) -> Vec<u8> {
        match admission {
            FlatCountedQueryAdmission::Prepared { key, .. } => key,
            FlatCountedQueryAdmission::Unsupported { reason } => {
                panic!("query should be admitted: {reason}")
            }
        }
    }

    fn read_u32(row: &[u8], offset: &mut usize) -> u32 {
        let value = u32::from_be_bytes(row[*offset..*offset + 4].try_into().unwrap());
        *offset += 4;
        value
    }

    fn read_u16(row: &[u8], offset: &mut usize) -> u16 {
        let value = u16::from_be_bytes(row[*offset..*offset + 2].try_into().unwrap());
        *offset += 2;
        value
    }

    fn decode_ground_symbol(packet: &[u8]) -> String {
        assert_eq!(packet.first().copied(), Some(0x01));
        let len = u32::from_be_bytes(packet[1..5].try_into().unwrap()) as usize;
        assert_eq!(packet.len(), 5 + len);
        String::from_utf8(packet[5..].to_vec()).expect("test binding should be a UTF-8 symbol")
    }

    fn decode_row(row: &[u8], factor_count: usize) -> (Vec<u32>, BTreeMap<u16, String>) {
        let mut offset = 0usize;
        let counts = (0..factor_count)
            .map(|_| read_u32(row, &mut offset))
            .collect::<Vec<_>>();
        let binding_count = read_u32(row, &mut offset);
        let mut bindings = BTreeMap::new();
        for _ in 0..binding_count {
            let slot = read_u16(row, &mut offset);
            let env = row[offset];
            offset += 1;
            let ground = row[offset];
            offset += 1;
            assert_eq!(env, 0);
            assert_eq!(ground, 1);
            let len = read_u32(row, &mut offset) as usize;
            let bytes = &row[offset..offset + len];
            offset += len;
            bindings.insert(slot, decode_ground_symbol(bytes));
        }
        assert_eq!(offset, row.len());
        (counts, bindings)
    }

    #[test]
    fn flat_join_preserves_shared_variables_and_factor_multiplicity() {
        let mut space = Space::new();
        let ab = parse(&mut space, "(edge a b)");
        let bc = parse(&mut space, "(edge b c)");
        counted_insert_expr(&mut space, &ab).unwrap();
        counted_insert_expr(&mut space, &ab).unwrap();
        counted_insert_expr(&mut space, &bc).unwrap();
        counted_insert_expr(&mut space, &bc).unwrap();
        counted_insert_expr(&mut space, &bc).unwrap();

        let query = parse(&mut space, "(, (edge $x $y) (edge $y $z))");
        let mut index = FlatCountedQueryIndex::default();
        let key = prepared_key(index.prepare(&space, &query).unwrap());
        let mut cursor = FlatCountedQueryCursor::new(Arc::new(index), &key).unwrap();
        let row = cursor
            .next_packet_row()
            .unwrap()
            .expect("one joined row should exist");
        let (counts, bindings) = decode_row(&row, 2);

        assert_eq!(counts, vec![2, 3]);
        assert_eq!(bindings.get(&0).unwrap(), "a");
        assert_eq!(bindings.get(&1).unwrap(), "b");
        assert_eq!(bindings.get(&2).unwrap(), "c");
        assert!(cursor.next_packet_row().unwrap().is_none());
    }

    #[test]
    fn flat_join_enforces_repeated_variables() {
        let mut space = Space::new();
        for fact in ["(pair a a)", "(pair a b)", "(pair b b)"] {
            let expr = parse(&mut space, fact);
            counted_insert_expr(&mut space, &expr).unwrap();
        }
        let query = parse(&mut space, "(, (pair $x $x))");
        let mut index = FlatCountedQueryIndex::default();
        let key = prepared_key(index.prepare(&space, &query).unwrap());
        let mut cursor = FlatCountedQueryCursor::new(Arc::new(index), &key).unwrap();
        let mut values = Vec::new();
        while let Some(row) = cursor.next_packet_row().unwrap() {
            values.push(decode_row(&row, 1).1[&0].clone());
        }
        assert_eq!(values, vec!["a".to_string(), "b".to_string()]);
    }

    #[test]
    fn flat_join_avoids_cartesian_candidate_enumeration() {
        let mut space = Space::new();
        for idx in 0..200 {
            let left = parse(&mut space, &format!("(left l{idx} k{idx})"));
            let right = parse(&mut space, &format!("(right k{idx} r{idx})"));
            counted_insert_expr(&mut space, &left).unwrap();
            counted_insert_expr(&mut space, &right).unwrap();
        }
        let query = parse(&mut space, "(, (left $x $k) (right $k $y))");
        let mut index = FlatCountedQueryIndex::default();
        let key = prepared_key(index.prepare(&space, &query).unwrap());
        let mut cursor = FlatCountedQueryCursor::new(Arc::new(index), &key).unwrap();
        let mut rows = 0usize;
        while cursor.next_packet_row().unwrap().is_some() {
            rows += 1;
        }
        assert_eq!(rows, 200);
        assert!(
            cursor.stats().trie_seeks < 4_000,
            "join should seek through indexed domains, not enumerate 40,000 products: {:?}",
            cursor.stats()
        );
        assert!(cursor.stats().max_frame_cells <= 6);
    }

    #[test]
    fn warm_first_row_work_and_cursor_state_do_not_scale_with_result_count() {
        fn measure(row_count: usize) -> (FlatCountedCursorStats, u128) {
            let mut space = Space::new();
            for idx in 0..row_count {
                let fact = parse(&mut space, &format!("(item value{idx})"));
                counted_insert_expr(&mut space, &fact).unwrap();
            }
            let query = parse(&mut space, "(, (item $value))");
            let mut index = FlatCountedQueryIndex::default();
            let key = prepared_key(index.prepare(&space, &query).unwrap());
            let index = Arc::new(index);

            let mut durations = Vec::with_capacity(101);
            let mut representative = None;
            for _ in 0..101 {
                let start = Instant::now();
                let mut cursor = FlatCountedQueryCursor::new(index.clone(), &key).unwrap();
                assert!(cursor.next_packet_row().unwrap().is_some());
                durations.push(start.elapsed().as_nanos());
                representative = Some(cursor.stats().clone());
            }
            durations.sort_unstable();
            (representative.unwrap(), durations[durations.len() / 2])
        }

        let (small, small_ns) = measure(64);
        let (large, large_ns) = measure(4_096);

        assert_eq!(small.rows_emitted, 1);
        assert_eq!(large.rows_emitted, 1);
        assert_eq!(small.trie_seeks, large.trie_seeks);
        assert_eq!(small.trie_descents, large.trie_descents);
        assert_eq!(small.max_frame_cells, large.max_frame_cells);
        assert!(large.max_frame_cells <= 1);
        println!(
            "first-row warm median: rows=64 {small_ns}ns; \
             rows=4096 {large_ns}ns; seeks={}; frame-cells={}",
            large.trie_seeks, large.max_frame_cells
        );
    }

    #[test]
    fn flat_join_count_pushdown_preserves_bag_multiplicity_without_rows() {
        let mut space = Space::new();
        for _ in 0..2 {
            let left = parse(&mut space, "(left a k)");
            counted_insert_expr(&mut space, &left).unwrap();
        }
        for _ in 0..3 {
            let right = parse(&mut space, "(right k b)");
            counted_insert_expr(&mut space, &right).unwrap();
        }
        let unmatched = parse(&mut space, "(right other c)");
        counted_insert_expr(&mut space, &unmatched).unwrap();

        let query = parse(&mut space, "(, (left $x $k) (right $k $y))");
        let mut index = FlatCountedQueryIndex::default();
        let key = prepared_key(index.prepare(&space, &query).unwrap());
        let mut cursor = FlatCountedQueryCursor::new(Arc::new(index), &key).unwrap();

        assert_eq!(cursor.count_remaining().unwrap(), 6);
        assert_eq!(cursor.stats().rows_aggregated, 1);
        assert_eq!(
            cursor.stats().rows_emitted,
            0,
            "COUNT pushdown must not reconstruct result rows"
        );
        assert!(cursor.next_packet_row().unwrap().is_none());
    }

    #[test]
    fn semi_naive_variants_partition_tuples_by_first_delta_factor() {
        let mut old = Space::new();
        let mut delta = Space::new();
        let mut known = Space::new();
        for fact in ["(edge a b)", "(edge b c)"] {
            let expr = parse(&mut delta, fact);
            counted_insert_expr(&mut delta, &expr).unwrap();
            let expr = parse(&mut known, fact);
            counted_insert_expr(&mut known, &expr).unwrap();
        }
        let query = parse(&mut known, "(, (edge $x $y) (edge $y $z))");
        let mut known_index = FlatCountedQueryIndex::default();
        let key = prepared_key(known_index.prepare(&known, &query).unwrap());
        let mut old_index = Arc::new(FlatCountedQueryIndex::default());
        let mut delta_index = Arc::new(FlatCountedQueryIndex::default());
        let mut cursor = FlatSemiNaiveQueryCursor::new(
            Arc::new(known_index),
            &old,
            &mut old_index,
            &delta,
            &mut delta_index,
            &key,
        )
        .unwrap();

        let row = cursor
            .next_packet_row()
            .unwrap()
            .expect("one all-delta join should be emitted exactly once");
        let (_counts, bindings) = decode_row(&row, 2);
        assert_eq!(bindings[&0], "a");
        assert_eq!(bindings[&1], "b");
        assert_eq!(bindings[&2], "c");
        assert!(cursor.next_packet_row().unwrap().is_none());
        assert_eq!(cursor.stats().rows_emitted, 1);

        let old_ab = parse(&mut old, "(edge a b)");
        let old_bc = parse(&mut old, "(edge b c)");
        counted_insert_expr(&mut old, &old_ab).unwrap();
        counted_insert_expr(&mut old, &old_bc).unwrap();
        let delta_cd = parse(&mut delta, "(edge c d)");
        counted_insert_expr(&mut delta, &delta_cd).unwrap();
        let known_cd = parse(&mut known, "(edge c d)");
        counted_insert_expr(&mut known, &known_cd).unwrap();

        let mut known_index = FlatCountedQueryIndex::default();
        let key = prepared_key(known_index.prepare(&known, &query).unwrap());
        let mut old_index = Arc::new(FlatCountedQueryIndex::default());
        let mut delta_only = Space::new();
        let delta_only_cd = parse(&mut delta_only, "(edge c d)");
        counted_insert_expr(&mut delta_only, &delta_only_cd).unwrap();
        let mut delta_index = Arc::new(FlatCountedQueryIndex::default());
        let mut cursor = FlatSemiNaiveQueryCursor::new(
            Arc::new(known_index),
            &old,
            &mut old_index,
            &delta_only,
            &mut delta_index,
            &key,
        )
        .unwrap();
        let row = cursor.next_packet_row().unwrap().unwrap();
        let (_counts, bindings) = decode_row(&row, 2);
        assert_eq!(bindings[&0], "b");
        assert_eq!(bindings[&1], "c");
        assert_eq!(bindings[&2], "d");
        assert!(cursor.next_packet_row().unwrap().is_none());
    }

    #[test]
    fn semi_naive_transitive_closure_uses_frontier_not_known_set() {
        const NODE_COUNT: usize = 32;

        let mut known = Space::new();
        let mut old = Space::new();
        for node in 0..NODE_COUNT - 1 {
            let text = format!("(edge n{node} n{})", node + 1);
            let known_edge = parse(&mut known, &text);
            counted_insert_expr(&mut known, &known_edge).unwrap();
            let old_edge = parse(&mut old, &text);
            counted_insert_expr(&mut old, &old_edge).unwrap();
        }

        let query = parse(&mut known, "(, (reach $x $y) (edge $y $z))");
        let mut known_index = Arc::new(FlatCountedQueryIndex::default());
        let mut old_index = Arc::new(FlatCountedQueryIndex::default());
        let mut frontier = (0..NODE_COUNT - 1)
            .map(|node| (node, node + 1))
            .collect::<Vec<_>>();
        let mut reached = BTreeSet::<(usize, usize)>::new();
        let mut semi_naive_rows = 0u64;
        let mut semi_naive_seeks = 0u64;
        let mut full_rederivation_rows = 0u64;
        let mut rounds = 0usize;

        while !frontier.is_empty() {
            rounds += 1;
            let mut delta = Space::new();
            for &(from, to) in &frontier {
                assert!(reached.insert((from, to)));
                let text = format!("(reach n{from} n{to})");
                let known_reach = parse(&mut known, &text);
                counted_insert_expr(&mut known, &known_reach).unwrap();
                Arc::make_mut(&mut known_index)
                    .observe_expr(&known, &known_reach)
                    .unwrap();
                let delta_reach = parse(&mut delta, &text);
                counted_insert_expr(&mut delta, &delta_reach).unwrap();
            }

            let key = prepared_key(
                Arc::make_mut(&mut known_index)
                    .prepare(&known, &query)
                    .unwrap(),
            );
            let mut delta_index = Arc::new(FlatCountedQueryIndex::default());
            let mut cursor = FlatSemiNaiveQueryCursor::new(
                known_index.clone(),
                &old,
                &mut old_index,
                &delta,
                &mut delta_index,
                &key,
            )
            .unwrap();
            let mut next = BTreeSet::<(usize, usize)>::new();
            while let Some(row) = cursor.next_packet_row().unwrap() {
                semi_naive_rows += 1;
                let (_counts, bindings) = decode_row(&row, 2);
                let from = bindings[&0]
                    .strip_prefix('n')
                    .unwrap()
                    .parse::<usize>()
                    .unwrap();
                let to = bindings[&2]
                    .strip_prefix('n')
                    .unwrap()
                    .parse::<usize>()
                    .unwrap();
                if !reached.contains(&(from, to)) {
                    next.insert((from, to));
                }
            }
            semi_naive_seeks = semi_naive_seeks.saturating_add(cursor.stats().trie_seeks);

            /*
             * The oracle strategy would re-run the rule over every known
             * reach fact this round. Count its successful join rows without
             * executing that deliberately superlinear evaluator.
             */
            full_rederivation_rows = full_rederivation_rows.saturating_add(
                reached
                    .iter()
                    .filter(|(_, to)| *to + 1 < NODE_COUNT)
                    .count() as u64,
            );

            for &(from, to) in &frontier {
                let text = format!("(reach n{from} n{to})");
                let old_reach = parse(&mut old, &text);
                counted_insert_expr(&mut old, &old_reach).unwrap();
                Arc::make_mut(&mut old_index)
                    .observe_expr(&old, &old_reach)
                    .unwrap();
            }
            frontier = next.into_iter().collect();
        }

        let expected_reach = (NODE_COUNT * (NODE_COUNT - 1) / 2) as u64;
        let expected_derived = ((NODE_COUNT - 1) * (NODE_COUNT - 2) / 2) as u64;
        assert_eq!(reached.len() as u64, expected_reach);
        assert_eq!(semi_naive_rows, expected_derived);
        assert_eq!(rounds, NODE_COUNT - 1);
        assert!(
            semi_naive_rows.saturating_mul(4) < full_rederivation_rows,
            "frontier work must stay output-quadratic instead of repeatedly \
             re-deriving the known set: semi={semi_naive_rows}, \
             full={full_rederivation_rows}"
        );
        assert!(
            semi_naive_seeks < full_rederivation_rows.saturating_mul(16),
            "indexed frontier traversal unexpectedly lost its work bound: \
             seeks={semi_naive_seeks}, full={full_rederivation_rows}"
        );
        assert_eq!(known_index.stats().catalog_builds, 1);
        assert_eq!(
            known_index.stats().incremental_updates,
            expected_reach - (NODE_COUNT - 1) as u64,
            "after the one cold build, new closure facts must update the \
             maintained catalog rather than trigger a rebuild"
        );
    }

    #[test]
    fn index_updates_incrementally_and_cursor_keeps_snapshot() {
        let mut space = Space::new();
        let a = parse(&mut space, "(item a)");
        let b = parse(&mut space, "(item b)");
        counted_insert_expr(&mut space, &a).unwrap();
        let query = parse(&mut space, "(, (item $x))");

        let mut index = Arc::new(FlatCountedQueryIndex::default());
        let key = {
            let mutable = Arc::make_mut(&mut index);
            prepared_key(mutable.prepare(&space, &query).unwrap())
        };
        let builds_before = index.stats().access_path_builds;
        let mut old_cursor = FlatCountedQueryCursor::new(index.clone(), &key).unwrap();

        counted_insert_expr(&mut space, &b).unwrap();
        {
            let mutable = Arc::make_mut(&mut index);
            mutable.observe_expr(&space, &b).unwrap();
            let rebuilt_key = prepared_key(mutable.prepare(&space, &query).unwrap());
            assert_eq!(rebuilt_key, key);
        }
        assert_eq!(index.stats().access_path_builds, builds_before);
        assert_eq!(index.stats().incremental_updates, 1);

        let mut old_values = Vec::new();
        while let Some(row) = old_cursor.next_packet_row().unwrap() {
            old_values.push(decode_row(&row, 1).1[&0].clone());
        }
        assert_eq!(old_values, vec!["a".to_string()]);

        let mut new_cursor = FlatCountedQueryCursor::new(index.clone(), &key).unwrap();
        let mut new_values = Vec::new();
        while let Some(row) = new_cursor.next_packet_row().unwrap() {
            new_values.push(decode_row(&row, 1).1[&0].clone());
        }
        assert_eq!(new_values, vec!["a".to_string(), "b".to_string()]);

        counted_remove_one_expr(&mut space, &a).unwrap();
        {
            let mutable = Arc::make_mut(&mut index);
            mutable.observe_expr(&space, &a).unwrap();
            let _ = mutable.prepare(&space, &query).unwrap();
        }
        assert_eq!(index.stats().access_path_builds, builds_before);
        let mut after_remove = FlatCountedQueryCursor::new(index, &key).unwrap();
        let row = after_remove.next_packet_row().unwrap().unwrap();
        assert_eq!(decode_row(&row, 1).1[&0], "b");
        assert!(after_remove.next_packet_row().unwrap().is_none());
    }

    #[test]
    fn renamed_query_variables_share_the_alpha_canonical_plan_key() {
        let mut space = Space::new();
        let fact = parse(&mut space, "(edge a b)");
        counted_insert_expr(&mut space, &fact).unwrap();
        let query_x = parse(&mut space, "(, (edge $x $y))");
        let query_named = parse(&mut space, "(, (edge $left $right))");
        let mut index = FlatCountedQueryIndex::default();

        let x_key = prepared_key(index.prepare(&space, &query_x).unwrap());
        let named_key = prepared_key(index.prepare(&space, &query_named).unwrap());

        assert_eq!(x_key, named_key);
        assert_eq!(index.stats().plan_builds, 1);
        assert_eq!(index.stats().plan_cache_hits, 1);
    }

    #[test]
    fn nested_query_variables_decline_but_non_ground_facts_form_a_residual_partition() {
        let mut space = Space::new();
        let ground = parse(&mut space, "(typed f (arrow a b))");
        counted_insert_expr(&mut space, &ground).unwrap();
        let nested = parse(&mut space, "(, (typed $f (arrow $a $b)))");
        let mut index = FlatCountedQueryIndex::default();
        let admission = index.prepare(&space, &nested).unwrap();
        assert!(matches!(
            admission,
            FlatCountedQueryAdmission::Unsupported { .. }
        ));

        let nonground = parse(&mut space, "(typed $f any)");
        counted_insert_expr(&mut space, &nonground).unwrap();
        let query = parse(&mut space, "(, (typed $x any))");
        let mut index = FlatCountedQueryIndex::default();
        let admission = index.prepare(&space, &query).unwrap();
        assert!(matches!(
            admission,
            FlatCountedQueryAdmission::Prepared {
                has_residual: true,
                has_exact_partition: false,
                ..
            }
        ));
        let key = prepared_key(admission);
        let mut residual = index
            .residual_cursor(&space, &query, &key)
            .unwrap()
            .expect("matching non-ground rows require a residual cursor");
        assert!(residual.next_packet_row(&space).unwrap().is_some());
        assert!(residual.next_packet_row(&space).unwrap().is_none());
    }

    #[test]
    fn exact_and_residual_partitions_are_disjoint_and_preserve_counts() {
        let mut space = Space::new();
        for _ in 0..2 {
            let exact = parse(&mut space, "(edge a ground)");
            counted_insert_expr(&mut space, &exact).unwrap();
        }
        for _ in 0..3 {
            let residual = parse(&mut space, "(edge $x residual)");
            counted_insert_expr(&mut space, &residual).unwrap();
        }
        let query = parse(&mut space, "(, (edge a $value))");
        let mut index = FlatCountedQueryIndex::default();
        let admission = index.prepare(&space, &query).unwrap();
        assert!(matches!(
            admission,
            FlatCountedQueryAdmission::Prepared {
                has_residual: true,
                has_exact_partition: true,
                ..
            }
        ));
        let key = prepared_key(admission);
        let mut exact = FlatCountedQueryCursor::new(Arc::new(index.clone()), &key).unwrap();
        let mut residual = index
            .residual_cursor(&space, &query, &key)
            .unwrap()
            .expect("same-head stored variables require a residual cursor");

        assert_eq!(exact.count_remaining().unwrap(), 2);
        assert_eq!(residual.count_remaining().unwrap(), 3);
        assert_eq!(residual.rows_aggregated(), 1);
    }

    #[test]
    fn residual_conjunction_includes_mixed_products_without_replaying_all_ground_products() {
        let mut space = Space::new();
        for _ in 0..2 {
            let exact_left = parse(&mut space, "(left a k)");
            counted_insert_expr(&mut space, &exact_left).unwrap();
        }
        for _ in 0..3 {
            let residual_left = parse(&mut space, "(left $x k)");
            counted_insert_expr(&mut space, &residual_left).unwrap();
        }
        for _ in 0..5 {
            let right = parse(&mut space, "(right k b)");
            counted_insert_expr(&mut space, &right).unwrap();
        }
        let query = parse(&mut space, "(, (left $x $key) (right $key $value))");
        let mut index = FlatCountedQueryIndex::default();
        let admission = index.prepare(&space, &query).unwrap();
        let key = prepared_key(admission);
        let mut exact = FlatCountedQueryCursor::new(Arc::new(index.clone()), &key).unwrap();
        let mut residual = index
            .residual_cursor(&space, &query, &key)
            .unwrap()
            .expect("mixed conjunction requires a residual cursor");

        assert_eq!(exact.count_remaining().unwrap(), 10);
        assert_eq!(residual.count_remaining().unwrap(), 15);
    }

    #[test]
    fn variable_head_rows_enter_the_residual_partition() {
        let mut space = Space::new();
        let exact = parse(&mut space, "(edge a ground)");
        let wildcard = parse(&mut space, "($relation a residual)");
        counted_insert_expr(&mut space, &exact).unwrap();
        counted_insert_expr(&mut space, &wildcard).unwrap();
        let query = parse(&mut space, "(, (edge a $value))");
        let mut index = FlatCountedQueryIndex::default();
        let admission = index.prepare(&space, &query).unwrap();
        let key = prepared_key(admission);
        let mut residual = index
            .residual_cursor(&space, &query, &key)
            .unwrap()
            .expect("variable-headed rows require a residual cursor");

        assert!(residual.next_packet_row(&space).unwrap().is_some());
        assert!(residual.next_packet_row(&space).unwrap().is_none());
    }
}
