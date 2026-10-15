; PeTTa 1.1 raises an error for an import that no file answers.
!(test (let $caught (catch (import! &self no_such_directory/no_such_module))
            (car-atom $caught))
       Error)
