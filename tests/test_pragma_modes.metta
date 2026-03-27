; HE accepts runtime directives before bare-minimal is enabled.
!(pragma! type-check auto)
!(pragma! dialect mettail)
!(pragma! interpreter bare-minimal)
; In bare-minimal mode, unsupported pragmas stay unreduced instead of becoming ().
!(pragma! max-stack-depth 7)
!(pragma! banana split)
