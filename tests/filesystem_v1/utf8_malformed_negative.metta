!(import! &self str)
!(let $bad (str:unescape metta "a\\xffb")
  (let $initial (str:utf8-new strict) (str:utf8-feed $initial $bad True)))
