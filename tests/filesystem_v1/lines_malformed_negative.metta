!(import! &self str)
!(let $bad (str:unescape metta "valid\\xffline") (str:line-segments $bad False))
