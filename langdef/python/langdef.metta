(gslt-language-pipeline-v1
  (name PythonV1)
  (presentation python)
  (refuse python:refuse)
  (stage ast "ast.metta" python:ast-of-cst)
  (stage scope "scope.metta" python:scope-of-ast)
  (stage core "core.metta" python:core-of-main ast)
  (stage run "run.metta" python:run core))
