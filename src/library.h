#ifndef CETTA_LIBRARY_H
#define CETTA_LIBRARY_H

#include "atom.h"
#include "foreign.h"
#include "native_handle.h"
#include "session.h"
#include "space.h"
#include <stdbool.h>
#include <limits.h>

#define CETTA_MAX_MODULE_ROOTS 32
#define CETTA_MAX_IMPORT_DIR_DEPTH 32
#define CETTA_MAX_IMPORTED_FILES 256
#define CETTA_MAX_IMPORT_TRANSACTION_SPACES 32
#define CETTA_MAX_LOADED_MODULES 64
#define CETTA_MAX_CMDLINE_ARGS 256
#define CETTA_MAX_NATIVE_HANDLES 64

typedef struct {
    char display_name[PATH_MAX];
    char canonical_path[PATH_MAX];
    CettaModuleProviderKind provider_kind;
    CettaModuleFormat format;
    Space *space;
    bool loading;
} CettaLoadedModule;

typedef struct CettaLibraryContext {
    CettaEvalSession session;
    uint32_t active_mask;
    char root_dir[4096];
    char script_dir[PATH_MAX];
    char import_dirs[CETTA_MAX_IMPORT_DIR_DEPTH][PATH_MAX];
    uint32_t import_dir_len;
    CettaModuleMount module_mounts[CETTA_MAX_MODULE_ROOTS];
    uint32_t module_mount_len;
    struct {
        Space *space;
        bool loading;
        char path[PATH_MAX];
    } imported_files[CETTA_MAX_IMPORTED_FILES];
    uint32_t imported_file_len;
    struct {
        Space *work_space;
        Space *logical_space;
    } import_space_aliases[CETTA_MAX_IMPORT_TRANSACTION_SPACES];
    uint32_t import_space_alias_len;
    const char *cmdline_args[CETTA_MAX_CMDLINE_ARGS];
    uint32_t cmdline_arg_len;
    CettaLoadedModule loaded_modules[CETTA_MAX_LOADED_MODULES];
    uint32_t loaded_module_len;
    CettaNativeHandleSlot native_handles[CETTA_MAX_NATIVE_HANDLES];
    uint32_t native_handle_len;
    uint64_t native_handle_next_id;
    CettaForeignRuntime *foreign_runtime;
} CettaLibraryContext;

void cetta_library_context_init(CettaLibraryContext *ctx);
void cetta_library_context_init_with_profile(CettaLibraryContext *ctx,
                                             const CettaProfile *profile);
void cetta_library_context_free(CettaLibraryContext *ctx);
void cetta_library_context_set_exec_path(CettaLibraryContext *ctx, const char *argv0);
void cetta_library_context_set_script_path(CettaLibraryContext *ctx, const char *filename);
void cetta_library_context_set_cli_args(CettaLibraryContext *ctx, int argc,
                                        char **argv, int arg_start);
uint32_t cetta_library_module_mount_count(const CettaLibraryContext *ctx);
const CettaModuleMount *cetta_library_module_mount_at(const CettaLibraryContext *ctx,
                                                      uint32_t index);
const CettaModuleMount *cetta_library_find_module_mount(const CettaLibraryContext *ctx,
                                                        const char *namespace_name);
uint32_t cetta_library_loaded_module_count(const CettaLibraryContext *ctx);
const CettaLoadedModule *cetta_library_loaded_module_at(const CettaLibraryContext *ctx,
                                                        uint32_t index);

bool cetta_library_import(CettaLibraryContext *ctx, const char *name,
                          Space *space, Arena *eval_arena,
                          Arena *persistent_arena, Registry *registry,
                          int fuel, Atom **error_out);

bool cetta_library_register_module(CettaLibraryContext *ctx, const char *path,
                                   Arena *eval_arena, Atom **error_out);
bool cetta_library_register_git_module(CettaLibraryContext *ctx, const char *url,
                                       Arena *eval_arena, Atom **error_out);

bool cetta_library_import_module(CettaLibraryContext *ctx, const char *spec,
                                 Space *space, bool target_is_fresh,
                                 Arena *eval_arena,
                                 Arena *persistent_arena, Registry *registry,
                                 int fuel, Atom **error_out);
bool cetta_library_include_module(CettaLibraryContext *ctx, const char *spec,
                                  Space *space, Arena *eval_arena,
                                  Arena *persistent_arena, Registry *registry,
                                  int fuel, Atom **error_out);
Atom *cetta_library_mod_space(CettaLibraryContext *ctx, const char *spec,
                              Arena *eval_arena, Arena *persistent_arena,
                              Registry *registry, int fuel, Atom **error_out);
Atom *cetta_library_module_inventory_space(CettaLibraryContext *ctx,
                                           Arena *eval_arena,
                                           Arena *persistent_arena,
                                           Atom **error_out);
bool cetta_library_print_loaded_modules(CettaLibraryContext *ctx, FILE *out,
                                        Arena *eval_arena, Atom **error_out);

Atom *cetta_library_dispatch_native(CettaLibraryContext *ctx, Space *space,
                                    Arena *a,
                                    Atom *head, Atom **args, uint32_t nargs);

#endif /* CETTA_LIBRARY_H */
