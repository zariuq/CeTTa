; A missing source raises a catchable existence error whichever way the
; import names it: a bare name, a path, or a library reference.  A library
; that exists still imports.
!(test (let* (($bare (car-atom (catch (import! &self no_such_module_name))))
              ($path (car-atom (catch (import! &self no_such_directory/no_such_module))))
              ($library (car-atom (catch (import! &self (library no_such_library_name))))))
         ($bare $path $library))
       (Error Error Error))
!(test (import! &self (library lib_tabling)) true)
