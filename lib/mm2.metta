(@doc mm2:program-new
  (@desc "Allocates a fresh MORK-backed MM2 program handle for host-side CeTTa orchestration. The canonical host-side MM2 namespace is mm2:, and dotted mm2. is accepted as reader sugar and optional display sugar in CLI result output. Use --lang mm2 for raw .mm2 files, or import the mm2 library when you want to build and run MM2 programs from ordinary CeTTa code.")
  (@params ())
  (@return "Fresh opaque MM2 program handle"))

(= (mm2:program-new)
   (function
     (case (__cetta_lib_mm2_program_new ())
       (($result (return $result))))))

(@doc mm2:program-clear!
  (@desc "Removes every exec rule currently stored in an MM2 program handle so the handle can be reused.")
  (@params (
    (@param "Target MM2 program handle")))
  (@return "Unit on success"))

(= (mm2:program-clear! $program)
   (function
     (case (__cetta_lib_mm2_program_clear $program)
       (($result (return $result))))))

(@doc mm2:program-add!
  (@desc "Adds one quoted or raw MM2 exec atom to a program handle. Host-side CeTTa code should usually quote MM2 syntax so ordinary HE evaluation does not try to interpret it first.")
  (@params (
    (@param "Target MM2 program handle")
    (@param "MM2 program atom, usually quoted in host-side CeTTa")))
  (@return "Unit on success"))

(= (mm2:program-add! $program $atom)
   (function
     (case (__cetta_lib_mm2_program_add $program $atom)
       (($result (return $result))))))

(@doc mm2:load-file!
  (@desc "Parses a raw .mm2 file, lowers it through CeTTa's MM2 surface capture, sends exec rules to the program handle, and sends non-exec facts to the context handle. This is the host-side bridge from the mm2: namespace into direct MM2 files. The linked MORK runtime currently covers the core language plus practical sink extensions such as +, -, count, and head.")
  (@params (
    (@param "Target MM2 program handle")
    (@param "Target MM2 context handle")
    (@param "Path to a .mm2 file, resolved relative to the current script")))
  (@return "Unit on success"))

(= (mm2:load-file! $program $context $path)
   (function
     (case (__cetta_lib_mm2_load_file $program $context $path)
       (($result (return $result))))))

(@doc mm2:program-size
  (@desc "Returns the number of exec rules currently stored in an MM2 program handle.")
  (@params (
    (@param "Target MM2 program handle")))
  (@return "Number of stored exec rules"))

(= (mm2:program-size $program)
   (function
     (case (__cetta_lib_mm2_program_size $program)
       (($result (return $result))))))

(@doc mm2:program-atoms
  (@desc "Dumps the current MM2 program contents back into quoted surface syntax so host-side CeTTa code can inspect them without re-evaluating MM2 forms.")
  (@params (
    (@param "Target MM2 program handle")))
  (@return "Expression of quoted MM2 program atoms"))

(= (mm2:program-atoms $program)
   (function
     (case (__cetta_lib_mm2_program_atoms $program)
       (($result (return $result))))))

(@doc mm2:context-new
  (@desc "Allocates a fresh MORK-backed MM2 context handle for host-side CeTTa orchestration.")
  (@params ())
  (@return "Fresh opaque MM2 context handle"))

(= (mm2:context-new)
   (function
     (case (__cetta_lib_mm2_context_new ())
       (($result (return $result))))))

(@doc mm2:context-clear!
  (@desc "Removes every current fact and loaded program chunk from an MM2 context handle so it can be reused from a clean state.")
  (@params (
    (@param "Target MM2 context handle")))
  (@return "Unit on success"))

(= (mm2:context-clear! $context)
   (function
     (case (__cetta_lib_mm2_context_clear $context)
       (($result (return $result))))))

(@doc mm2:context-load-program!
  (@desc "Attaches a program handle's exec rules to a context handle so later step/run calls can execute them against the current facts.")
  (@params (
    (@param "Target MM2 context handle")
    (@param "Source MM2 program handle")))
  (@return "Unit on success"))

(= (mm2:context-load-program! $context $program)
   (function
     (case (__cetta_lib_mm2_context_load_program $context $program)
       (($result (return $result))))))

(@doc mm2:context-add!
  (@desc "Adds one quoted or raw MM2 fact atom to a context handle. Host-side CeTTa code should usually quote MM2 syntax so ordinary HE evaluation leaves it inert until the MM2 bridge consumes it.")
  (@params (
    (@param "Target MM2 context handle")
    (@param "MM2 fact atom, usually quoted in host-side CeTTa")))
  (@return "Unit on success"))

(= (mm2:context-add! $context $atom)
   (function
     (case (__cetta_lib_mm2_context_add $context $atom)
       (($result (return $result))))))

(@doc mm2:context-remove!
  (@desc "Removes one exact MM2 fact from a context handle.")
  (@params (
    (@param "Target MM2 context handle")
    (@param "Exact MM2 fact atom to remove")))
  (@return "Unit on success"))

(= (mm2:context-remove! $context $atom)
   (function
     (case (__cetta_lib_mm2_context_remove $context $atom)
       (($result (return $result))))))

(@doc mm2:context-step!
  (@desc "Executes at most one MM2 step in the target context and returns how many steps actually fired.")
  (@params (
    (@param "Target MM2 context handle")))
  (@return "0 or 1 depending on whether a step fired"))

(= (mm2:context-step! $context)
   (function
     (case (__cetta_lib_mm2_context_step $context)
       (($result (return $result))))))

(@doc mm2:context-run!
  (@desc "Runs MM2 execution on the target context. The one-argument form uses CeTTa's large default step bound for practical exhaustion; the two-argument form uses an explicit non-negative limit. This is the host-side equivalent of running a raw file with --lang mm2.")
  (@params (
    (@param "Target MM2 context handle")
    (@param "Optional non-negative maximum step count")))
  (@return "Number of steps actually performed"))

(= (mm2:context-run! $context)
   (function
     (case (__cetta_lib_mm2_context_run $context)
       (($result (return $result))))))

(= (mm2:context-run! $context $steps)
   (function
     (case (__cetta_lib_mm2_context_run $context $steps)
       (($result (return $result))))))

(@doc mm2:context-size
  (@desc "Returns the number of current facts in an MM2 context after any loaded program effects have been applied.")
  (@params (
    (@param "Target MM2 context handle")))
  (@return "Number of current MM2 facts"))

(= (mm2:context-size $context)
   (function
     (case (__cetta_lib_mm2_context_size $context)
       (($result (return $result))))))

(@doc mm2:context-atoms
  (@desc "Dumps the current MM2 context contents back into quoted surface syntax so host-side CeTTa code can inspect results without re-evaluating MM2 forms.")
  (@params (
    (@param "Target MM2 context handle")))
  (@return "Expression of quoted MM2 context atoms"))

(= (mm2:context-atoms $context)
   (function
     (case (__cetta_lib_mm2_context_atoms $context)
       (($result (return $result))))))
