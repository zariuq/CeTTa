!(import! &self langdef)

!(let $compiler
    (langdef:load-deterministic-equations
      (LangDef:EquationSources "langdef/bnf/regular_token_value_compiler_v1.metta"))
   (let $answers
      (langdef:run-deterministic-equations-data $compiler
        (LangDef:DeterministicCall rtv1:compile pair
          (rx-seq (rx-char (cp 97)) (rx-char (cp 98)))))
     (let $_ (langdef:close-deterministic-equations $compiler)
       $answers)))
