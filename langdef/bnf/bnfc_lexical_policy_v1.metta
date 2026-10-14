(gslt-presentation-v1 BnfcLexicalPolicyV1
  (signature
    (operator metta-equation 2)
    (operator bnfc-v1:upper 1)
    (operator bnfc-v1:lower 1)
    (operator bnfc-v1:letter 1)
    (operator bnfc-v1:digit 0)
    (operator bnfc-v1:any 1)
    (operator bnfc-v1:ident 1)
    (operator bnfc-v1:whitespace 1)
    (operator bnfc-v1:builtin 2)
    (operator bnfc-v1:plus 1)
    (operator bnfc-v1:quoted-string 0)
    (operator bnfc-v1:quoted-char 0)
    (operator bnfc-documented 0)
    (operator bnfc-2.9.6.3-c-flex-2.6.4 0)
    (operator Ident 0)
    (operator Integer 0)
    (operator Double 0)
    (operator String 0)
    (operator Char 0)
    (operator rx-eps 0)
    (operator rx-any 0)
    (operator rx-char 1)
    (operator rx-range 2)
    (operator rx-alt 2)
    (operator rx-seq 2)
    (operator rx-star 1)
    (operator rx-minus 2)
    (operator cp 1))
  (equations)
  (rewrites
    (rule upper-documented
      (head (metta-equation (bnfc-v1:upper bnfc-documented)
        (rx-alt (rx-range (cp 65) (cp 90))
          (rx-alt (rx-range (cp 192) (cp 214)) (rx-range (cp 216) (cp 222)))))) (body))
    (rule lower-documented
      (head (metta-equation (bnfc-v1:lower bnfc-documented)
        (rx-alt (rx-range (cp 97) (cp 122))
          (rx-alt (rx-range (cp 223) (cp 246)) (rx-range (cp 248) (cp 255)))))) (body))
    (rule upper-c-flex
      (head (metta-equation (bnfc-v1:upper bnfc-2.9.6.3-c-flex-2.6.4)
        (rx-range (cp 65) (cp 90)))) (body))
    (rule lower-c-flex
      (head (metta-equation (bnfc-v1:lower bnfc-2.9.6.3-c-flex-2.6.4)
        (rx-range (cp 97) (cp 122)))) (body))
    (rule letter
      (head (metta-equation (bnfc-v1:letter ?policy)
        (rx-alt (bnfc-v1:upper ?policy) (bnfc-v1:lower ?policy)))) (body))
    (rule digit
      (head (metta-equation (bnfc-v1:digit) (rx-range (cp 48) (cp 57)))) (body))
    (rule any-documented
      (head (metta-equation (bnfc-v1:any bnfc-documented) rx-any)) (body))
    (rule any-c-flex
      (head (metta-equation (bnfc-v1:any bnfc-2.9.6.3-c-flex-2.6.4)
        (rx-alt (rx-range (cp 0) (cp 9)) (rx-range (cp 11) (cp 255))))) (body))
    (rule plus
      (head (metta-equation (bnfc-v1:plus ?regex) (rx-seq ?regex (rx-star ?regex)))) (body))
    (rule ident
      (head (metta-equation (bnfc-v1:ident ?policy)
        (rx-seq (bnfc-v1:letter ?policy)
          (rx-star (rx-alt (bnfc-v1:letter ?policy)
            (rx-alt (bnfc-v1:digit) (rx-alt (rx-char (cp 95)) (rx-char (cp 39))))))))) (body))
    (rule whitespace-documented
      (head (metta-equation (bnfc-v1:whitespace bnfc-documented)
        (bnfc-v1:plus (rx-alt (rx-char (cp 32)) (rx-range (cp 9) (cp 13)))))) (body))
    (rule whitespace-c-flex
      (head (metta-equation (bnfc-v1:whitespace bnfc-2.9.6.3-c-flex-2.6.4)
        (bnfc-v1:plus (rx-alt (rx-char (cp 32))
          (rx-alt (rx-range (cp 9) (cp 10)) (rx-range (cp 12) (cp 13))))))) (body))
    (rule builtin-ident
      (head (metta-equation (bnfc-v1:builtin Ident ?policy) (bnfc-v1:ident ?policy))) (body))
    (rule builtin-integer
      (head (metta-equation (bnfc-v1:builtin Integer ?policy) (bnfc-v1:plus (bnfc-v1:digit)))) (body))
    (rule builtin-double
      (head (metta-equation (bnfc-v1:builtin Double ?policy)
        (rx-seq (bnfc-v1:plus (bnfc-v1:digit))
          (rx-seq (rx-char (cp 46))
            (rx-seq (bnfc-v1:plus (bnfc-v1:digit))
              (rx-alt rx-eps
                (rx-seq (rx-char (cp 101))
                  (rx-seq (rx-alt rx-eps (rx-char (cp 45))) (bnfc-v1:plus (bnfc-v1:digit)))))))))) (body))
    (rule builtin-string-documented
      (head (metta-equation (bnfc-v1:builtin String bnfc-documented) (bnfc-v1:quoted-string))) (body))
    (rule builtin-char-documented
      (head (metta-equation (bnfc-v1:builtin Char bnfc-documented) (bnfc-v1:quoted-char))) (body))
    (rule builtin-string-c-flex
      (head (metta-equation (bnfc-v1:builtin String bnfc-2.9.6.3-c-flex-2.6.4) (rx-char (cp 34)))) (body))
    (rule builtin-char-c-flex
      (head (metta-equation (bnfc-v1:builtin Char bnfc-2.9.6.3-c-flex-2.6.4) (rx-char (cp 39)))) (body))
    (rule quoted-string
      (head (metta-equation (bnfc-v1:quoted-string)
        (rx-seq (rx-char (cp 34))
          (rx-seq
            (rx-star (rx-alt
              (rx-minus rx-any (rx-alt (rx-char (cp 34))
                (rx-alt (rx-char (cp 92)) (rx-char (cp 10)))))
              (rx-seq (rx-char (cp 92))
                (rx-alt (rx-char (cp 34)) (rx-alt (rx-char (cp 92))
                  (rx-alt (rx-char (cp 39)) (rx-alt (rx-char (cp 110))
                    (rx-alt (rx-char (cp 116)) (rx-alt (rx-char (cp 114)) (rx-char (cp 102)))))))))))
            (rx-char (cp 34)))))) (body))
    (rule quoted-char
      (head (metta-equation (bnfc-v1:quoted-char)
        (rx-seq (rx-char (cp 39))
          (rx-seq
            (rx-alt (rx-minus rx-any (rx-alt (rx-char (cp 39)) (rx-char (cp 92))))
              (rx-seq (rx-char (cp 92))
                (rx-alt (rx-char (cp 92)) (rx-alt (rx-char (cp 39))
                  (rx-alt (rx-char (cp 110)) (rx-alt (rx-char (cp 116))
                    (rx-alt (rx-char (cp 114)) (rx-char (cp 102)))))))))
            (rx-char (cp 39)))))) (body))))
