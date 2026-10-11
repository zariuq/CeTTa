import builtins
from hyperon.ext import register_atoms

builtins._compatibility_file_count = getattr(builtins, "_compatibility_file_count", 0) + 1
print("python-file-effect", builtins._compatibility_file_count, flush=True)


@register_atoms
def operations():
    builtins._compatibility_hook_count = getattr(builtins, "_compatibility_hook_count", 0) + 1
    print("python-hook-effect", builtins._compatibility_hook_count, flush=True)
    raise RuntimeError("registration refused")
