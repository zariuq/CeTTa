; Stable and typed PeTTa raise a catchable missing-source error.
!(test (let $caught (catch (import! &self no_such_directory/no_such_module))
            (car-atom $caught))
       Error)
