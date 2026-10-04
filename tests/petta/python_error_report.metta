; An uncaught Python error is reported as SWI-PeTTa reports it: the
; exception's type and message, then its Python stack.  Credentials in the
; report are screened out.
!(import! &self ./python_error_report.py)
!(println! before)
!(py-call (python_error_report.refuse))
!(println! after)
