; Runtime parsing observes the rational growth ceiling before arithmetic.

!(pragma! prefer-rationals True)

!(pragma! max-rational-digits 5)

!(parse "1/3")

!(parse "999/1000")
