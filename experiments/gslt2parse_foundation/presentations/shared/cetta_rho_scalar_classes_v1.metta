; Scalar relations shared by the native strict-rho and cost-rho surfaces.
; Input is decoded to Unicode scalars. Identifiers deliberately remain ASCII,
; matching the native reader, while comment bodies may contain any scalar.
(gslt-presentation-v1 CeTTaRhoScalarClassesV1
  (signature)
  (equations)
  (rewrites
    (rule member-cetta-rho-whitespace-scalar-9 (head (member cetta-rho-whitespace-scalar (cp 9))) (body))
    (rule member-cetta-rho-whitespace-scalar-10 (head (member cetta-rho-whitespace-scalar (cp 10))) (body))
    (rule member-cetta-rho-whitespace-scalar-11 (head (member cetta-rho-whitespace-scalar (cp 11))) (body))
    (rule member-cetta-rho-whitespace-scalar-12 (head (member cetta-rho-whitespace-scalar (cp 12))) (body))
    (rule member-cetta-rho-whitespace-scalar-13 (head (member cetta-rho-whitespace-scalar (cp 13))) (body))
    (rule member-cetta-rho-whitespace-scalar-32 (head (member cetta-rho-whitespace-scalar (cp 32))) (body))
    (rule member-cetta-rho-newline-scalar-10 (head (member cetta-rho-newline-scalar (cp 10))) (body))

    (rule member-cetta-rho-non-newline-scalar
      (head (member cetta-rho-non-newline-scalar ?codepoint))
      (body (different ?codepoint (cp 10))))
    (rule member-cetta-rho-block-non-star-scalar
      (head (member cetta-rho-block-non-star-scalar ?codepoint))
      (body (different ?codepoint (cp 42))))
    (rule member-cetta-rho-block-non-slash-scalar
      (head (member cetta-rho-block-non-slash-scalar ?codepoint))
      (body (different ?codepoint (cp 47))))

    ; ParserPack class clauses are point/exception relations. These finite
    ; classes keep keyword priority executable without a host-side class
    ; intersection.
    (rule member-cetta-rho-ascii-lower-not-f-97
      (head (member cetta-rho-ascii-lower-not-f (cp 97)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-f-98
      (head (member cetta-rho-ascii-lower-not-f (cp 98)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-f-99
      (head (member cetta-rho-ascii-lower-not-f (cp 99)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-f-100
      (head (member cetta-rho-ascii-lower-not-f (cp 100)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-f-101
      (head (member cetta-rho-ascii-lower-not-f (cp 101)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-f-103
      (head (member cetta-rho-ascii-lower-not-f (cp 103)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-f-104
      (head (member cetta-rho-ascii-lower-not-f (cp 104)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-f-105
      (head (member cetta-rho-ascii-lower-not-f (cp 105)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-f-106
      (head (member cetta-rho-ascii-lower-not-f (cp 106)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-f-107
      (head (member cetta-rho-ascii-lower-not-f (cp 107)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-f-108
      (head (member cetta-rho-ascii-lower-not-f (cp 108)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-f-109
      (head (member cetta-rho-ascii-lower-not-f (cp 109)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-f-110
      (head (member cetta-rho-ascii-lower-not-f (cp 110)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-f-111
      (head (member cetta-rho-ascii-lower-not-f (cp 111)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-f-112
      (head (member cetta-rho-ascii-lower-not-f (cp 112)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-f-113
      (head (member cetta-rho-ascii-lower-not-f (cp 113)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-f-114
      (head (member cetta-rho-ascii-lower-not-f (cp 114)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-f-115
      (head (member cetta-rho-ascii-lower-not-f (cp 115)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-f-116
      (head (member cetta-rho-ascii-lower-not-f (cp 116)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-f-117
      (head (member cetta-rho-ascii-lower-not-f (cp 117)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-f-118
      (head (member cetta-rho-ascii-lower-not-f (cp 118)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-f-119
      (head (member cetta-rho-ascii-lower-not-f (cp 119)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-f-120
      (head (member cetta-rho-ascii-lower-not-f (cp 120)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-f-121
      (head (member cetta-rho-ascii-lower-not-f (cp 121)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-f-122
      (head (member cetta-rho-ascii-lower-not-f (cp 122)))
      (body))

    (rule member-cetta-rho-ascii-lower-not-o-97
      (head (member cetta-rho-ascii-lower-not-o (cp 97)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-o-98
      (head (member cetta-rho-ascii-lower-not-o (cp 98)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-o-99
      (head (member cetta-rho-ascii-lower-not-o (cp 99)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-o-100
      (head (member cetta-rho-ascii-lower-not-o (cp 100)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-o-101
      (head (member cetta-rho-ascii-lower-not-o (cp 101)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-o-102
      (head (member cetta-rho-ascii-lower-not-o (cp 102)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-o-103
      (head (member cetta-rho-ascii-lower-not-o (cp 103)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-o-104
      (head (member cetta-rho-ascii-lower-not-o (cp 104)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-o-105
      (head (member cetta-rho-ascii-lower-not-o (cp 105)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-o-106
      (head (member cetta-rho-ascii-lower-not-o (cp 106)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-o-107
      (head (member cetta-rho-ascii-lower-not-o (cp 107)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-o-108
      (head (member cetta-rho-ascii-lower-not-o (cp 108)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-o-109
      (head (member cetta-rho-ascii-lower-not-o (cp 109)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-o-110
      (head (member cetta-rho-ascii-lower-not-o (cp 110)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-o-112
      (head (member cetta-rho-ascii-lower-not-o (cp 112)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-o-113
      (head (member cetta-rho-ascii-lower-not-o (cp 113)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-o-114
      (head (member cetta-rho-ascii-lower-not-o (cp 114)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-o-115
      (head (member cetta-rho-ascii-lower-not-o (cp 115)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-o-116
      (head (member cetta-rho-ascii-lower-not-o (cp 116)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-o-117
      (head (member cetta-rho-ascii-lower-not-o (cp 117)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-o-118
      (head (member cetta-rho-ascii-lower-not-o (cp 118)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-o-119
      (head (member cetta-rho-ascii-lower-not-o (cp 119)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-o-120
      (head (member cetta-rho-ascii-lower-not-o (cp 120)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-o-121
      (head (member cetta-rho-ascii-lower-not-o (cp 121)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-o-122
      (head (member cetta-rho-ascii-lower-not-o (cp 122)))
      (body))

    (rule member-cetta-rho-ascii-lower-not-r-97
      (head (member cetta-rho-ascii-lower-not-r (cp 97)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-r-98
      (head (member cetta-rho-ascii-lower-not-r (cp 98)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-r-99
      (head (member cetta-rho-ascii-lower-not-r (cp 99)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-r-100
      (head (member cetta-rho-ascii-lower-not-r (cp 100)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-r-101
      (head (member cetta-rho-ascii-lower-not-r (cp 101)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-r-102
      (head (member cetta-rho-ascii-lower-not-r (cp 102)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-r-103
      (head (member cetta-rho-ascii-lower-not-r (cp 103)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-r-104
      (head (member cetta-rho-ascii-lower-not-r (cp 104)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-r-105
      (head (member cetta-rho-ascii-lower-not-r (cp 105)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-r-106
      (head (member cetta-rho-ascii-lower-not-r (cp 106)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-r-107
      (head (member cetta-rho-ascii-lower-not-r (cp 107)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-r-108
      (head (member cetta-rho-ascii-lower-not-r (cp 108)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-r-109
      (head (member cetta-rho-ascii-lower-not-r (cp 109)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-r-110
      (head (member cetta-rho-ascii-lower-not-r (cp 110)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-r-111
      (head (member cetta-rho-ascii-lower-not-r (cp 111)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-r-112
      (head (member cetta-rho-ascii-lower-not-r (cp 112)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-r-113
      (head (member cetta-rho-ascii-lower-not-r (cp 113)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-r-115
      (head (member cetta-rho-ascii-lower-not-r (cp 115)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-r-116
      (head (member cetta-rho-ascii-lower-not-r (cp 116)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-r-117
      (head (member cetta-rho-ascii-lower-not-r (cp 117)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-r-118
      (head (member cetta-rho-ascii-lower-not-r (cp 118)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-r-119
      (head (member cetta-rho-ascii-lower-not-r (cp 119)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-r-120
      (head (member cetta-rho-ascii-lower-not-r (cp 120)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-r-121
      (head (member cetta-rho-ascii-lower-not-r (cp 121)))
      (body))
    (rule member-cetta-rho-ascii-lower-not-r-122
      (head (member cetta-rho-ascii-lower-not-r (cp 122)))
      (body))))
