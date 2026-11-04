#!/usr/bin/env python3
"""Reject context-owner defects against one coherent native object cohort."""
import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--cppflags", required=True)
    parser.add_argument("--cflags", required=True)
    parser.add_argument("--ldflags", required=True)
    parser.add_argument("--objects", nargs="+", default=[])
    parser.add_argument("--only-prefix", default="")
    parser.add_argument("--consumer-wraps", default="")
    args = parser.parse_args()
    compiler = shlex.split(args.cc)
    flags = shlex.split(args.cppflags) + shlex.split(args.cflags) + ["-Iinclude"]
    link_flags = shlex.split(args.ldflags) + ["-Wl,--wrap=grounded_call"]
    cases = [
        ("he-catalog-prefix", "src/he_type_policy.c",
         "memset(tables, 0, sizeof(*tables));\n            g_he_module_space_no_deps_symbol = SYMBOL_ID_NONE;",
         "tables->initialized = true;\n            g_he_module_space_no_deps_symbol = SYMBOL_ID_NONE;",
         "!fixture.tables->initialized && !fixture.tables->loaded && !fixture.tables->arena.head"),
        ("he-catalog-variable-capacity", "src/parser.c",
         "scope->spellings = spellings;\n        VarId *ids = realloc(scope->ids, sizeof(*ids) * (size_t)cap);",
         "scope->spellings = spellings;\n        scope->cap = cap;\n        VarId *ids = realloc(scope->ids, sizeof(*ids) * (size_t)cap);",
         "var_id_is_none(parser_var_scope_id(&scope, x)) && scope.cap == 0u && scope.len == 0u"),
        ("he-catalog-name-capacity", "src/parser.c",
         "scope->names = names;\n        VarId *ids = realloc(scope->name_ids, sizeof(*ids) * (size_t)cap);",
         "scope->names = names;\n        scope->name_cap = cap;\n        VarId *ids = realloc(scope->name_ids, sizeof(*ids) * (size_t)cap);",
         "var_id_is_none(parser_var_scope_name_id(&scope, key)) && scope.name_cap == 0u && scope.name_len == 0u"),
        ("he-catalog-variable-sharing", "src/parser.c",
         "if (scope->spellings[i] == spelling)\n            return scope->ids[i];",
         "if (scope->spellings[i] == spelling)\n            return fresh_var_id();",
         "var_id_eq(arrow->expr.elems[1]->var_id, arrow->expr.elems[2]->var_id)"),
        ("prelude-refusal-fallback", "src/cetta_stdlib.c",
         "if (direct_error == ENOMEM || direct_error == EOVERFLOW ||\n            term_universe_last_error_code(s->native.universe) == TERM_UNIVERSE_ERROR_ALLOCATION_FAILED)",
         "if (false && (direct_error == ENOMEM || direct_error == EOVERFLOW ||\n            term_universe_last_error_code(s->native.universe) == TERM_UNIVERSE_ERROR_ALLOCATION_FAILED))",
         "!stdlib_try_load(&fixture->session.space, &fixture->arena) && errno == ENOMEM"),
        ("prelude-variable-capacity", "src/cetta_stdlib.c",
         "scope->spellings = spellings;\n        VarId *ids = realloc(scope->ids, sizeof(VarId) * next_cap);",
         "scope->spellings = spellings;\n        scope->cap = next_cap;\n        VarId *ids = realloc(scope->ids, sizeof(VarId) * next_cap);",
         "var_id_is_none(blob_var_scope_id(&scope, x)) && scope.cap == 0u && scope.len == 0u"),
        ("prelude-variable-sharing", "src/cetta_stdlib.c",
         "if (scope->spellings[i] == spelling)\n            return scope->ids[i];",
         "if (scope->spellings[i] == spelling)\n            return fresh_var_id();",
         "!var_id_is_none(first) && var_id_eq(first, blob_var_scope_id(&scope, x))"),
        ("prelude-buffer-owner", "src/cetta_stdlib.c",
         "free(atom_ids);\n        if (direct_error == ENOMEM",
         "(void)atom_ids;\n        if (direct_error == ENOMEM",
         "failed prelude retained a decoder buffer"),
        ("startup-library-prefix", "src/runtime_session.c",
         "runtime->libraries_ready = true;\n    if (!cetta_library_context_try_init_for_language_profile(&runtime->libraries, language, profile))\n        return false;",
         "if (!cetta_library_context_try_init_for_language_profile(&runtime->libraries, language, profile))\n        return false;\n    runtime->libraries_ready = true;",
         "constructor failure retained a mutex"),
        ("startup-table-prefix", "src/petta_search_machine.c",
         "arena_free(&shared->arena); free(shared); errno = error; return NULL;",
         "(void)shared; errno = error; return NULL;",
         "constructor failure retained owned storage"),
        ("startup-registry-prefix", "src/name_key.c",
         "if (arena_ready) arena_free(&table->arena);\n    free(table->by_id);\n    free(table->buckets);",
         "if (arena_ready) arena_free(&table->arena);\n    (void)table->by_id;\n    (void)table->buckets;",
         "constructor failure retained owned storage"),
        ("startup-root-error", "src/embedding.c",
         "int startup_error = rooted ? 0 : errno;", "int startup_error = 0;",
         "cetta_embed_engine_new(&config, &engine) == CETTA_OUT_OF_MEMORY && !engine"),
        ("session-root-rollback", "src/space.c",
         "int error = errno;\n    space_detach_from_universe(s);",
         "int error = errno;\n    (void)s;",
         "universe->observer_len == observers && universe->root_provider_len == providers"),
        ("session-collection-rollback", "src/space.c",
         "if (empty_unpublished) universe->collection_mode = mode;",
         "(void)empty_unpublished; (void)mode;",
         "universe->collection_mode == TERM_UNIVERSE_COLLECTION_RECLAIM"),
        ("session-reserve-prefix", "src/atom.c",
         "ArenaBlock *block = arena_claim_block(a, size, true);\n    if (!block) return false;",
         "ArenaBlock *block = arena_claim_block(a, size, true);\n    if (!block) { a->head = NULL; return false; }",
         "arena.head == head && arena.live_bytes == live && arena.reserved_bytes == reserved"),
        ("symbol-construction-entry-rollback", "src/symbol.c",
         "if (new_chunk) { free(st->entry_chunks[chunk]); st->entry_chunks[chunk] = NULL; }",
         "(void)new_chunk;", "!table.entry_chunks[before >> SYMBOL_ENTRY_CHUNK_BITS]"),
        ("symbol-construction-prefix", "src/symbol.c",
         "error = errno;\n    memset(builtins, 0, sizeof(*builtins));",
         "error = errno;\n    (void)builtins;", "!memcmp(&builtins, &empty, sizeof(builtins))"),
        ("symbol-construction-identity-reuse", "src/symbol.c",
         "&next, next + 1u, memory_order_relaxed, memory_order_relaxed)",
         "&next, next == UINT64_MAX - 1u ? 1u : next + 1u, memory_order_relaxed, memory_order_relaxed)",
         "!symbol_table_try_init(&table) && errno == EOVERFLOW"),
        ("symbol-construction-growth", "src/symbol.c",
         "if (!slots) return false;", "if (!slots) { st->slots = NULL; return false; }",
         "table.slots == old_slots && atomic_load(&table.entry_len) == old_count"),
        ("construction-symbol-cleanup", "src/symbol.c",
         "symbol_table_free(st);\n    errno = error;\n    return false;",
         "(void)st;\n    errno = error;\n    return false;",
         "constructor failure retained a mutex"),
        ("construction-name-cleanup", "src/embedding.c",
         "if (named) cetta_name_domain_free(&engine->names);",
         "(void)named;", "constructor failure retained owned storage"),
        ("construction-statistics-context", "src/embedding.c",
         "if (named) cetta_name_domain_free(&engine->names);\n    cetta_runtime_stats_owner_exchange(previous_stats);",
         "if (named) cetta_name_domain_free(&engine->names);\n    (void)previous_stats;",
         "cetta_runtime_stats_owner_current() == stats"),
        ("construction-mutex-status", "src/embedding.c",
         "cetta_status_t early_status = errno == ENOMEM ? CETTA_OUT_OF_MEMORY : CETTA_ENGINE_ERROR;",
         "cetta_status_t early_status = CETTA_OUT_OF_MEMORY;",
         "cetta_embed_engine_new(&config, &engine) == CETTA_ENGINE_ERROR && !engine"),
        ("construction-statistics-status", "src/stats.c",
         "errno = error;", "errno = ENOMEM;",
         "cetta_embed_engine_new(&config, &engine) == CETTA_ENGINE_ERROR && !engine"),
        ("statistics-owner", "src/embedding.c",
         "scope->stats = cetta_runtime_stats_owner_exchange(engine->stats);",
         "scope->stats = cetta_runtime_stats_owner_exchange(NULL);",
         "statistics_marker(left) == 17"),
        ("mutex-owner", "src/eval.c",
         "CettaLibraryContext *library) {\n    if (!library) {\n        pthread_once(&g_default_named_mutexes_once",
         "CettaLibraryContext *library) {\n    if (true) {\n        pthread_once(&g_default_named_mutexes_once",
         "overlapped"),
        ("native-close", "src/runtime_session.c",
         "cetta_native_handle_cleanup_all(&runtime->libraries);",
         "(void)runtime;", "atomic_load(&native_closes) == 1"),
        ("active-close", "src/embedding.c", "if (!engine->active_entries) {",
         "if (true) {", "atomic_load(&native_closes) == 0"),
        ("worker-statistics-owner", "src/parallel_executor.c",
         "CettaRuntimeStatsOwner *previous_stats = cetta_runtime_stats_owner_exchange(executor->stats);",
         "CettaRuntimeStatsOwner *previous_stats = cetta_runtime_stats_owner_exchange(NULL);",
         "cetta_runtime_stats_owner_current() == owner"),
        ("ready-entry", "src/eval.c",
         "g_petta_value_invocation_atom = call;",
         "g_petta_value_invocation_atom = NULL;",
         "!strcmp(text, expected)"),
        ("ready-handler", "src/eval.c",
         "PETTA_PLAN_DYNAMIC_CALL, false);",
         "PETTA_PLAN_DYNAMIC_CALL, true);",
         "raised == CETTA_LANGUAGE_RAISED"),
        ("named-control-admission", "src/petta_search_machine.c",
         "return (!machine->host.native_named_arity && !machine->host.foreign_named_arity) ||",
         "return true || (!machine->host.native_named_arity && !machine->host.foreign_named_arity) ||",
         "cetta_embed_atom_kind(answer) == CETTA_ATOM_EXPRESSION"),
        ("space-exports", "src/embedding.c",
         "query_open_locked(engine, source, variables, options, output)",
         "query_open_locked(engine, source, NULL, options, output)",
         "cetta_embed_atom_arity(assignments) == 1"),
        ("space-data", "src/embedding.c",
         "atom_expr2(&scratch, atom_symbol_id(&scratch, g_builtin_syms.quote), pattern->value)",
         "pattern->value", "cetta_embed_result_count(observed_effects) == 0"),
        ("space-ownership", "src/embedding.c",
         "if (atom->owner->engine != engine) return CETTA_FOREIGN_ENGINE;\n"
         "    if (atom->value->kind != ATOM_GROUNDED",
         "if (false) return CETTA_FOREIGN_ENGINE;\n"
         "    if (atom->value->kind != ATOM_GROUNDED",
         "cetta_embed_space_from_atom(foreign, space_atom, &alias) == CETTA_FOREIGN_ENGINE"),
        ("space-removal", "src/embedding.c",
         "*removed = space_length64(target) < before;", "*removed = space_length64(target) <= before;",
         "cetta_embed_space_remove_atom(alias, fact, &removed) == CETTA_OK && !removed"),
        ("callback-close", "src/embedding_callbacks.inc",
         "callback->descriptor.close(cursor->host_cursor, g_embedding_deferred_cleanup == callback->engine ? CETTA_CALLBACK_CLOSE_DEFERRED : CETTA_CALLBACK_CLOSE_DIRECT);", "(void)cursor->host_cursor;",
         "stream.closes == 1u && stream.pulls == 1u"),
        ("callback-final", "src/embedding_callbacks.inc",
         ".final = state == CETTA_CALLBACK_RAISED || callback->descriptor.kind == CETTA_CALLBACK_SINGLE",
         ".final = true || state == CETTA_CALLBACK_RAISED || callback->descriptor.kind == CETTA_CALLBACK_SINGLE",
         "cetta_embed_atom_get_integer(atom, &result) == CETTA_OK"),
        ("callback-control", "src/petta_program.c",
         "!cetta_foreign_provider_callable(head_atom);", "true;",
         "caught_count == 1u"),
        ("callback-data", "src/petta_search_machine.c",
         "petta_push_unify(machine, value, choice->as.provider.expected, choice->barrier)",
         "petta_push_solve(machine, value, choice->as.provider.expected, choice->barrier)",
         "atoms_equal(data, argument)"),
        ("callback-affinity", "src/eval.c",
         "if (cetta_foreign_provider_thread_affine(atom))\n        return true;",
         "if (false && cetta_foreign_provider_thread_affine(atom))\n        return true;",
         "cetta_embed_run(cursor->engine, source, sizeof(source) - 1u, NULL, &result) == CETTA_OK"),
        ("buffer-release", "src/tensor.c",
         "if (s->owner_release) s->owner_release(s->owner_context);", "(void)s->owner_context;",
         "atomic_load(&freed) == 1u && atomic_load(&released) == 2u"),
        ("buffer-retain", "src/tensor.c",
         "owner->retain(owner->context);\n        storage->owner_context = owner->context;\n        storage->owner_release = owner->release;",
         "(void)owner->context;\n        storage->owner_context = owner->context;\n        storage->owner_release = owner->release;",
         "atomic_load(&retained) == 1u && atomic_load(&storage->references) == 2u"),
        ("buffer-copy", "src/embedding_buffers.inc",
         "else memcpy(copied, spec->allocation, spec->allocation_bytes);",
         "else memset(copied, 0, spec->allocation_bytes);", "at(buffer, 0u) == 10"),
        ("buffer-bounds", "src/tensor.c",
         "if (lo < 0 || (count ? (size_t)hi >= t->storage->stored : offset > t->storage->stored))",
         "if (false && (lo < 0 || (count ? (size_t)hi >= t->storage->stored : offset > t->storage->stored)))",
         "cetta_embed_buffer_view(buffer, (size_t[]){4}, (ptrdiff_t[]){8}, 1, 0, &view) == CETTA_INVALID_ARGUMENT"),
        ("buffer-boolean-view", "src/tensor.c",
         "if (v->dtype == CETTA_TENSOR_BOOL) {",
         "if (v->dtype == CETTA_TENSOR_BOOL && t->count == t->storage->stored) {",
         "cetta_embed_buffer_view(buffer, (size_t[]){1}, (ptrdiff_t[]){1}, 1u, 1u, &view) == CETTA_INVALID_ARGUMENT"),
        ("cleanup-blocked-release", "src/embedding.c",
         "cetta_embed_engine_retain(engine);\n    if (!pthread_mutex_trylock(&engine->mutex)) {",
         "cetta_embed_engine_retain(engine);\n    if (!pthread_mutex_lock(&engine->mutex)) {",
         "cross-thread cleanup waited on the engine lock"),
        ("cleanup-handoff", "src/embedding.c",
         "if (!atomic_load_explicit(&engine->cleanup, memory_order_acquire)) return;",
         "return;", "cleanup handoff left the external owner retained"),
        ("cleanup-waiting-entry", "src/embedding.c",
         "error = pthread_mutex_timedlock(&engine->mutex, &deadline);",
         "error = pthread_mutex_lock(&engine->mutex);",
         "cross-thread cleanup waited on the engine lock"),
        ("session-alias", "src/runtime_session.c", "if (repeated) continue;",
         "if (false && repeated) continue;", "frees[0] == 1u && frees[1] == 1u && frees[2] == 1u"),
        ("session-registry-retirement", "src/eval.c",
         "eval_cleanup_owned_new_spaces_in_set(&resources->created, registry, root);",
         "eval_cleanup_owned_new_spaces_in_set(&resources->created, NULL, root);",
         "frees[0] == 1u && frees[1] == 1u && frees[2] == 1u"),
        ("session-restoration", "src/runtime_session.c",
         "eval_resources_exchange(own_resources ? NULL : previous_resources);",
         "(void)own_resources; eval_resources_exchange(NULL);",
         "eval_resources_exchange(right->session.resources) == right->session.resources"),
        ("module-resume-context", "src/eval_work.inc",
         "cetta_library_resource_enter(guard->library, work->resource_context, &guard->scope);",
         "cetta_library_resource_enter(guard->library, NULL, &guard->scope);",
         "value == 111"),
        ("module-completed-document", "src/library.c",
         "if (ok && report->status == CETTA_DOCUMENT_COMPLETE &&",
         "if (false && ok && report->status == CETTA_DOCUMENT_COMPLETE &&",
         "a completed file was replayed"),
        ("module-io-status", "src/embedding.c",
         "status = result->system_error == ENOMEM ? CETTA_OUT_OF_MEMORY : CETTA_IO_ERROR;",
         "status = result->system_error == ENOMEM ? CETTA_OUT_OF_MEMORY : CETTA_PARSE_ERROR;",
         "cetta_embed_load(context.engine, missing, NULL, &result) == CETTA_IO_ERROR"),
        ("endian-scalar-load", "src/tensor.c",
         "if (!raw || !storage->swapped) return raw;", "if (true) return raw;",
         "!memcmp(&value, native + i * width, width)"),
        ("endian-view-order", "src/tensor.c",
         "(CettaTensorDenseSpan){.byte_order = tensor->storage->byte_order,", "(CettaTensorDenseSpan){.byte_order = CETTA_TENSOR_BYTE_ORDER_NATIVE,",
         "span.byte_order == foreign_order()"),
        ("endian-packed-matrix", "src/tensor.c",
         "if(cols && !m->storage->sparse && !v->storage->sparse && m->contiguous && v->contiguous &&\n        !m->storage->swapped && !v->storage->swapped &&",
         "if(cols && !m->storage->sparse && !v->storage->sparse && m->contiguous && v->contiguous &&",
         "values[0] == 23 && values[1] == 53"),
        ("endian-packed-selected", "src/tensor.c",
         "        matrix->storage->swapped || query->storage->swapped ||\n", "",
         "values[0] == 53 && values[1] == 23 && values[2] == 53"),
        ("endian-compare", "src/tensor.c",
         "a->contiguous && b->contiguous &&\n        !a->storage->swapped && !b->storage->swapped",
         "a->contiguous && b->contiguous",
         "cetta_tensor_equal(reference, observed)"),
        ("endian-native-copy", "src/tensor.c",
         "if(t->contiguous && !t->storage->swapped) memcpy", "if(t->contiguous) memcpy",
         "!memcmp(native, copied, 8 * width)"),
        ("endian-reinterpret", "src/embedding_buffers.inc",
         "spec.dtype = dtype; spec.byte_order = byte_order;", "spec.dtype = dtype; (void)byte_order;",
         "info.byte_order == CETTA_BYTE_ORDER_NATIVE"),
        ("endian-abi-order", "src/embedding_buffers.inc",
         ".byte_order = (cetta_byte_order_t)span.byte_order,", ".byte_order = CETTA_BYTE_ORDER_NATIVE,",
         "info.byte_order == spec.byte_order"),
        ("sparse-omitted-entry", "src/tensor.c",
         "storage->stored = entries; storage->bytes = entries * width;",
         "storage->stored = entries ? entries - 1u : 0; storage->bytes = entries * width;",
         "cetta_tensor_stored(tensor)==4"),
        ("sparse-stored-zero", "src/tensor.c",
         "storage->coordinates = (size_t *)coordinates; storage->values = (unsigned char *)values;",
         "storage->coordinates = coordinates ? (size_t *)coordinates + 1 : NULL; storage->values = (unsigned char *)values;",
         "present && value.f64 == 0"),
        ("sparse-segment-boundary", "src/tensor.c",
         "if (segments[major] > segments[major + 1] || segments[major + 1] > entries)",
         "if (false)", "==CETTA_TENSOR_INDEX_ERROR && !tensor"),
        ("sparse-csc-permutation", "src/tensor.c",
         "tensor->permutation[i] = sorted[i].occurrence;", "tensor->permutation[i] = i;",
         "!memcmp(observed_keys,(uint64_t[]){0,2,9,11}"),
        ("sparse-index-owner", "src/tensor.c", "tensor->borrowed_indexes = true;",
         "tensor->borrowed_indexes = false;", "borrowed component was freed"),
        ("sparse-refusal-owner", "src/tensor.c",
         "owner->retain(owner->context);\n        storage->owner_context = owner->context; storage->owner_release = owner->release;",
         "(void)owner;\n        storage->owner_context = owner->context; storage->owner_release = owner->release;",
         "retained==before+1"),
        ("sparse-component-lease", "src/embedding_sparse.inc",
         "for (size_t i = 0; i < 4; ++i) cetta_embed_buffer_retain((cetta_buffer_t *)owner->parts[i]);",
         "for (size_t i = 0; i < 1; ++i) cetta_embed_buffer_retain((cetta_buffer_t *)owner->parts[i]);",
         "borrowed component was freed"),
        ("sparse-owner-chain", "src/embedding_sparse.inc",
         "if (original) { cetta_tensor_retain(original); *output = original; return CETTA_OK; }",
         "if (false && original) { cetta_tensor_retain(original); *output = original; return CETTA_OK; }",
         "after.shape==before.shape"),
        ("sparse-export-refusal", "src/embedding_sparse.inc",
         "else sparse_parts_release(&parts);", "else { (void)parts; (void)sparse_parts_release; }", "staging allocation leaked"),
    ]
    cases = [case for case in cases if case[0].startswith(args.only_prefix)]
    if not cases:
        raise RuntimeError("no selected negative controls")
    with tempfile.TemporaryDirectory(prefix="embedding-context-mutants-", dir="runtime") as directory:
        root = Path(directory)
        for name, source, before, after, discriminator in cases:
            text = Path(source).read_text()
            if text.count(before) != 1:
                raise RuntimeError(f"{name}: expected exactly one source anchor")
            changed = root / f"{name}.c"
            body = text.replace(before, after)
            object_source = source
            if source.endswith(".inc"):
                object_source = "src/eval.c" if source == "src/eval_work.inc" else "src/embedding.c"
                owner = Path(object_source).read_text()
                anchor = '#include "' + Path(source).name + '"'
                if owner.count(anchor) != 1:
                    raise RuntimeError(f"{name}: expected exactly one include")
                body = owner.replace(anchor, body)
            changed.write_text(body)
            obj, binary = root / f"{name}.o", root / name
            variant_flags = [f for f in flags if not f.startswith("-O")] + ["-O0"] if name.startswith(("callback-", "buffer-", "cleanup-", "session-", "module-", "endian-", "sparse-", "construction-", "startup-")) else flags
            if name.startswith("symbol-construction-"):
                consumer = root / f"{name}-consumer.c"
                consumer.write_text(Path("tests/support/test_symbol_construction.c").read_text().replace(
                    '"../../src/symbol.c"', '"' + str(changed.resolve()) + '"'))
                subprocess.run(compiler + variant_flags + [str(consumer), "src/scalar_trie.c",
                    "-o", str(binary), *shlex.split(args.ldflags)], check=True)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
                if result.returncode == 0 or discriminator not in result.stderr:
                    raise RuntimeError(f"{name}: missed intended assertion\n{result.stderr}")
                print(f"PASS: {name} rejected at {discriminator}", flush=True)
                continue
            basename = Path(object_source).stem + "."
            objects = [path for path in args.objects if not Path(path).name.startswith(basename)]
            if len(objects) != len(args.objects) - 1:
                raise RuntimeError(f"{name}: expected exactly one replaced object")
            if name.startswith("he-catalog-"):
                consumer = root / f"{name}-consumer.c"
                body = Path("tests/support/test_he_tables_refusal.c").read_text()
                for included in ("src/parser.c", "src/he_type_policy.c"):
                    target = changed if included == source else Path(included)
                    body = body.replace('"../../' + included + '"', '"' + str(target.resolve()) + '"')
                consumer.write_text(body)
                objects = [path for path in args.objects if not Path(path).name.startswith(("parser.", "he_type_policy."))]
                subprocess.run(compiler + variant_flags + [str(consumer), *objects,
                    "-o", str(binary), *shlex.split(args.ldflags),
                    "-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free"], check=True)
            elif name.startswith("prelude-"):
                consumer = root / f"{name}-consumer.c"
                consumer.write_text(Path("tests/support/test_stdlib_refusal.c").read_text().replace(
                    '"../../src/cetta_stdlib.c"', '"' + str(changed.resolve()) + '"'))
                subprocess.run(compiler + variant_flags + [str(consumer), *objects,
                    "-o", str(binary), *shlex.split(args.ldflags)], check=True)
            elif name.startswith("cleanup-"):
                consumer = root / f"{name}-consumer.c"
                consumer.write_text(Path("tests/support/test_embedding_cleanup.c").read_text().replace(
                    '"../../src/embedding.c"', '"' + str(changed.resolve()) + '"'))
                subprocess.run(compiler + variant_flags + [str(consumer), *objects,
                    "-o", str(binary), *shlex.split(args.ldflags),
                    "-Wl,--wrap=pthread_mutex_unlock,--wrap=pthread_mutex_lock,--wrap=pthread_mutex_timedlock"], check=True)
            else:
                subprocess.run(compiler + variant_flags + ["-c", str(changed), "-o", str(obj)], check=True)
                public_consumer = name.startswith(("ready-", "space-", "callback-", "buffer-", "session-", "module-", "endian-", "sparse-", "construction-", "startup-"))
                consumer = "tests/support/test_sparse_interchange.c" if name.startswith("sparse-") else "tests/support/test_tensor_byte_order.c" if name.startswith("endian-") else "tests/test_embedding_modules.c" if name.startswith("module-") else "tests/support/test_runtime_session.c" if name.startswith("session-") else "tests/test_embedding_buffers.c" if name.startswith("buffer-") else "tests/test_embedding_callbacks.c" if name.startswith("callback-") else "tests/test_embedding.c" if public_consumer else "tests/support/test_embedding_context.c"
                consumer_link_flags = (link_flags[:-1] + ["-Wl,--wrap=eval_resources_new,--wrap=space_free,--wrap=malloc,--wrap=realloc"]) if name.startswith("session-") else link_flags[:-1] if public_consumer else link_flags
                if name.startswith(("construction-", "startup-")):
                    consumer = "tests/support/test_embedding_construction.c"
                    consumer_link_flags += ["-Wl,--wrap=" + fn for fn in shlex.split(args.consumer_wraps)]
                if name.startswith("sparse-"):
                    consumer_link_flags = consumer_link_flags + ["-Wl,--wrap=malloc,--wrap=calloc,--wrap=free"]
                consumer_flags = flags + ['-DCETTA_EMBED_TEST_DIRECTORY="' + str(root.resolve()) + '"'] if name.startswith("module-") else flags
                subprocess.run(compiler + consumer_flags + [consumer, str(obj),
                               *objects, "-o", str(binary), *consumer_link_flags], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
            if result.returncode == 0 or discriminator not in result.stderr:
                raise RuntimeError(f"{name}: missed intended assertion\n{result.stderr}")
            print(f"PASS: {name} rejected at {discriminator}", flush=True)
    print(f"Embedding context negative controls: {len(cases)} rejected")


if __name__ == "__main__":
    main()
