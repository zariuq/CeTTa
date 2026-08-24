; Generated proof-producing realization of seven Hopper HO programs.
; The generic combinator blocks keep predicate arguments first-class.
; Specialization preserves their proof constructors and source identity.

(= (hopper:table1:first-order:package)
  (compile:rule-package hopper-table1-first-order-ho-v1
    (rm-package
      (rm-block list-empty hopper:list:empty
        (quote (rel:list:empty-proof $element))
        rm-nil
        (quote (rel:list:empty $element (list:nil $element))))
      (rm-block list-head hopper:list:head
        (quote (rel:list:head-proof $element $head $tail))
        rm-nil
        (quote (rel:list:head $element
          (list:cons $element $head $tail) $head)))
      (rm-block list-tail hopper:list:tail
        (quote (rel:list:tail-proof $element $head $tail))
        rm-nil
        (quote (rel:list:tail $element
          (list:cons $element $head $tail) $tail)))
      (rm-block atom-list-tail hopper:atom-list:tail
        (quote (hopper:atom-list:tail:proof $head $tail
          (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (rel:list:tail hopper:atom
              (list:cons hopper:atom $head $tail) $tail)))
          rm-nil)
        (quote (hopper:atom-list:tail
          (list:cons hopper:atom $head $tail) $tail)))
      (rm-block atom-list-head hopper:atom-list:head
        (quote (hopper:atom-list:head:proof $head $tail
          (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (rel:list:head hopper:atom
              (list:cons hopper:atom $head $tail) $head)))
          rm-nil)
        (quote (hopper:atom-list:head
          (list:cons hopper:atom $head $tail) $head)))
      (rm-block list-member-here hopper:list:member-here
        (quote (rel:list:member-here $element $head $tail))
        rm-nil
        (quote (rel:list:member $element
          (list:cons $element $head $tail) $head)))
      (rm-block list-member-there hopper:list:member-there
        (quote (rel:list:member-there $element $head $tail
          $member (unquote $tail-evidence)))
        (rm-cons
          (rm-premise $tail-evidence
            (quote (rel:list:member $element $tail $member)))
          rm-nil)
        (quote (rel:list:member $element
          (list:cons $element $head $tail) $member)))
      (rm-block nat-zero hopper:nat:zero
        (quote hopper:nat:proof:zero)
        rm-nil
        (quote (hopper:nat:zero hopper:nat:n0)))
      (rm-block nat-succ-0-1 hopper:nat:succ-0-1
        (quote hopper:nat:proof:succ-0-1)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n0 hopper:nat:n1)))
      (rm-block nat-succ-1-2 hopper:nat:succ-1-2
        (quote hopper:nat:proof:succ-1-2)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n1 hopper:nat:n2)))
      (rm-block nat-succ-2-3 hopper:nat:succ-2-3
        (quote hopper:nat:proof:succ-2-3)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n2 hopper:nat:n3)))
      (rm-block nat-succ-3-4 hopper:nat:succ-3-4
        (quote hopper:nat:proof:succ-3-4)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n3 hopper:nat:n4)))
      (rm-block nat-succ-4-5 hopper:nat:succ-4-5
        (quote hopper:nat:proof:succ-4-5)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n4 hopper:nat:n5)))
      (rm-block nat-succ-5-6 hopper:nat:succ-5-6
        (quote hopper:nat:proof:succ-5-6)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n5 hopper:nat:n6)))
      (rm-block nat-succ-6-7 hopper:nat:succ-6-7
        (quote hopper:nat:proof:succ-6-7)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n6 hopper:nat:n7)))
      (rm-block nat-succ-7-8 hopper:nat:succ-7-8
        (quote hopper:nat:proof:succ-7-8)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n7 hopper:nat:n8)))
      (rm-block nat-succ-8-9 hopper:nat:succ-8-9
        (quote hopper:nat:proof:succ-8-9)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n8 hopper:nat:n9)))
      (rm-block nat-succ-9-10 hopper:nat:succ-9-10
        (quote hopper:nat:proof:succ-9-10)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n9 hopper:nat:n10)))
      (rm-block nat-succ-10-11 hopper:nat:succ-10-11
        (quote hopper:nat:proof:succ-10-11)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n10 hopper:nat:n11)))
      (rm-block nat-succ-11-12 hopper:nat:succ-11-12
        (quote hopper:nat:proof:succ-11-12)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n11 hopper:nat:n12)))
      (rm-block nat-succ-12-13 hopper:nat:succ-12-13
        (quote hopper:nat:proof:succ-12-13)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n12 hopper:nat:n13)))
      (rm-block nat-succ-13-14 hopper:nat:succ-13-14
        (quote hopper:nat:proof:succ-13-14)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n13 hopper:nat:n14)))
      (rm-block nat-succ-14-15 hopper:nat:succ-14-15
        (quote hopper:nat:proof:succ-14-15)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n14 hopper:nat:n15)))
      (rm-block nat-succ-15-16 hopper:nat:succ-15-16
        (quote hopper:nat:proof:succ-15-16)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n15 hopper:nat:n16)))
      (rm-block nat-succ-16-17 hopper:nat:succ-16-17
        (quote hopper:nat:proof:succ-16-17)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n16 hopper:nat:n17)))
      (rm-block nat-succ-17-18 hopper:nat:succ-17-18
        (quote hopper:nat:proof:succ-17-18)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n17 hopper:nat:n18)))
      (rm-block nat-succ-18-19 hopper:nat:succ-18-19
        (quote hopper:nat:proof:succ-18-19)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n18 hopper:nat:n19)))
      (rm-block nat-succ-19-20 hopper:nat:succ-19-20
        (quote hopper:nat:proof:succ-19-20)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n19 hopper:nat:n20)))
      (rm-block nat-succ-20-21 hopper:nat:succ-20-21
        (quote hopper:nat:proof:succ-20-21)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n20 hopper:nat:n21)))
      (rm-block nat-succ-21-22 hopper:nat:succ-21-22
        (quote hopper:nat:proof:succ-21-22)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n21 hopper:nat:n22)))
      (rm-block nat-succ-22-23 hopper:nat:succ-22-23
        (quote hopper:nat:proof:succ-22-23)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n22 hopper:nat:n23)))
      (rm-block nat-succ-23-24 hopper:nat:succ-23-24
        (quote hopper:nat:proof:succ-23-24)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n23 hopper:nat:n24)))
      (rm-block nat-succ-24-25 hopper:nat:succ-24-25
        (quote hopper:nat:proof:succ-24-25)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n24 hopper:nat:n25)))
      (rm-block nat-succ-25-26 hopper:nat:succ-25-26
        (quote hopper:nat:proof:succ-25-26)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n25 hopper:nat:n26)))
      (rm-block nat-succ-26-27 hopper:nat:succ-26-27
        (quote hopper:nat:proof:succ-26-27)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n26 hopper:nat:n27)))
      (rm-block nat-succ-27-28 hopper:nat:succ-27-28
        (quote hopper:nat:proof:succ-27-28)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n27 hopper:nat:n28)))
      (rm-block nat-succ-28-29 hopper:nat:succ-28-29
        (quote hopper:nat:proof:succ-28-29)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n28 hopper:nat:n29)))
      (rm-block nat-succ-29-30 hopper:nat:succ-29-30
        (quote hopper:nat:proof:succ-29-30)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n29 hopper:nat:n30)))
      (rm-block nat-succ-30-31 hopper:nat:succ-30-31
        (quote hopper:nat:proof:succ-30-31)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n30 hopper:nat:n31)))
      (rm-block nat-succ-31-32 hopper:nat:succ-31-32
        (quote hopper:nat:proof:succ-31-32)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n31 hopper:nat:n32)))
      (rm-block nat-succ-32-33 hopper:nat:succ-32-33
        (quote hopper:nat:proof:succ-32-33)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n32 hopper:nat:n33)))
      (rm-block nat-succ-33-34 hopper:nat:succ-33-34
        (quote hopper:nat:proof:succ-33-34)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n33 hopper:nat:n34)))
      (rm-block nat-succ-34-35 hopper:nat:succ-34-35
        (quote hopper:nat:proof:succ-34-35)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n34 hopper:nat:n35)))
      (rm-block nat-succ-35-36 hopper:nat:succ-35-36
        (quote hopper:nat:proof:succ-35-36)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n35 hopper:nat:n36)))
      (rm-block nat-succ-36-37 hopper:nat:succ-36-37
        (quote hopper:nat:proof:succ-36-37)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n36 hopper:nat:n37)))
      (rm-block nat-succ-37-38 hopper:nat:succ-37-38
        (quote hopper:nat:proof:succ-37-38)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n37 hopper:nat:n38)))
      (rm-block nat-succ-38-39 hopper:nat:succ-38-39
        (quote hopper:nat:proof:succ-38-39)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n38 hopper:nat:n39)))
      (rm-block nat-succ-39-40 hopper:nat:succ-39-40
        (quote hopper:nat:proof:succ-39-40)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n39 hopper:nat:n40)))
      (rm-block nat-succ-40-41 hopper:nat:succ-40-41
        (quote hopper:nat:proof:succ-40-41)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n40 hopper:nat:n41)))
      (rm-block nat-succ-41-42 hopper:nat:succ-41-42
        (quote hopper:nat:proof:succ-41-42)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n41 hopper:nat:n42)))
      (rm-block nat-succ-42-43 hopper:nat:succ-42-43
        (quote hopper:nat:proof:succ-42-43)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n42 hopper:nat:n43)))
      (rm-block nat-succ-43-44 hopper:nat:succ-43-44
        (quote hopper:nat:proof:succ-43-44)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n43 hopper:nat:n44)))
      (rm-block nat-succ-44-45 hopper:nat:succ-44-45
        (quote hopper:nat:proof:succ-44-45)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n44 hopper:nat:n45)))
      (rm-block nat-succ-45-46 hopper:nat:succ-45-46
        (quote hopper:nat:proof:succ-45-46)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n45 hopper:nat:n46)))
      (rm-block nat-succ-46-47 hopper:nat:succ-46-47
        (quote hopper:nat:proof:succ-46-47)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n46 hopper:nat:n47)))
      (rm-block nat-succ-47-48 hopper:nat:succ-47-48
        (quote hopper:nat:proof:succ-47-48)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n47 hopper:nat:n48)))
      (rm-block nat-succ-48-49 hopper:nat:succ-48-49
        (quote hopper:nat:proof:succ-48-49)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n48 hopper:nat:n49)))
      (rm-block nat-succ-49-50 hopper:nat:succ-49-50
        (quote hopper:nat:proof:succ-49-50)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n49 hopper:nat:n50)))
      (rm-block nat-succ-50-51 hopper:nat:succ-50-51
        (quote hopper:nat:proof:succ-50-51)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n50 hopper:nat:n51)))
      (rm-block nat-succ-51-52 hopper:nat:succ-51-52
        (quote hopper:nat:proof:succ-51-52)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n51 hopper:nat:n52)))
      (rm-block nat-succ-52-53 hopper:nat:succ-52-53
        (quote hopper:nat:proof:succ-52-53)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n52 hopper:nat:n53)))
      (rm-block nat-succ-53-54 hopper:nat:succ-53-54
        (quote hopper:nat:proof:succ-53-54)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n53 hopper:nat:n54)))
      (rm-block nat-succ-54-55 hopper:nat:succ-54-55
        (quote hopper:nat:proof:succ-54-55)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n54 hopper:nat:n55)))
      (rm-block nat-succ-55-56 hopper:nat:succ-55-56
        (quote hopper:nat:proof:succ-55-56)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n55 hopper:nat:n56)))
      (rm-block nat-succ-56-57 hopper:nat:succ-56-57
        (quote hopper:nat:proof:succ-56-57)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n56 hopper:nat:n57)))
      (rm-block nat-succ-57-58 hopper:nat:succ-57-58
        (quote hopper:nat:proof:succ-57-58)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n57 hopper:nat:n58)))
      (rm-block nat-succ-58-59 hopper:nat:succ-58-59
        (quote hopper:nat:proof:succ-58-59)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n58 hopper:nat:n59)))
      (rm-block nat-succ-59-60 hopper:nat:succ-59-60
        (quote hopper:nat:proof:succ-59-60)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n59 hopper:nat:n60)))
      (rm-block nat-succ-60-61 hopper:nat:succ-60-61
        (quote hopper:nat:proof:succ-60-61)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n60 hopper:nat:n61)))
      (rm-block nat-succ-61-62 hopper:nat:succ-61-62
        (quote hopper:nat:proof:succ-61-62)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n61 hopper:nat:n62)))
      (rm-block nat-succ-62-63 hopper:nat:succ-62-63
        (quote hopper:nat:proof:succ-62-63)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n62 hopper:nat:n63)))
      (rm-block nat-succ-63-64 hopper:nat:succ-63-64
        (quote hopper:nat:proof:succ-63-64)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n63 hopper:nat:n64)))
      (rm-block nat-succ-64-65 hopper:nat:succ-64-65
        (quote hopper:nat:proof:succ-64-65)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n64 hopper:nat:n65)))
      (rm-block nat-succ-65-66 hopper:nat:succ-65-66
        (quote hopper:nat:proof:succ-65-66)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n65 hopper:nat:n66)))
      (rm-block nat-succ-66-67 hopper:nat:succ-66-67
        (quote hopper:nat:proof:succ-66-67)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n66 hopper:nat:n67)))
      (rm-block nat-succ-67-68 hopper:nat:succ-67-68
        (quote hopper:nat:proof:succ-67-68)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n67 hopper:nat:n68)))
      (rm-block nat-succ-68-69 hopper:nat:succ-68-69
        (quote hopper:nat:proof:succ-68-69)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n68 hopper:nat:n69)))
      (rm-block nat-succ-69-70 hopper:nat:succ-69-70
        (quote hopper:nat:proof:succ-69-70)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n69 hopper:nat:n70)))
      (rm-block nat-succ-70-71 hopper:nat:succ-70-71
        (quote hopper:nat:proof:succ-70-71)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n70 hopper:nat:n71)))
      (rm-block nat-succ-71-72 hopper:nat:succ-71-72
        (quote hopper:nat:proof:succ-71-72)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n71 hopper:nat:n72)))
      (rm-block nat-succ-72-73 hopper:nat:succ-72-73
        (quote hopper:nat:proof:succ-72-73)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n72 hopper:nat:n73)))
      (rm-block nat-succ-73-74 hopper:nat:succ-73-74
        (quote hopper:nat:proof:succ-73-74)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n73 hopper:nat:n74)))
      (rm-block nat-succ-74-75 hopper:nat:succ-74-75
        (quote hopper:nat:proof:succ-74-75)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n74 hopper:nat:n75)))
      (rm-block nat-succ-75-76 hopper:nat:succ-75-76
        (quote hopper:nat:proof:succ-75-76)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n75 hopper:nat:n76)))
      (rm-block nat-succ-76-77 hopper:nat:succ-76-77
        (quote hopper:nat:proof:succ-76-77)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n76 hopper:nat:n77)))
      (rm-block nat-succ-77-78 hopper:nat:succ-77-78
        (quote hopper:nat:proof:succ-77-78)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n77 hopper:nat:n78)))
      (rm-block nat-succ-78-79 hopper:nat:succ-78-79
        (quote hopper:nat:proof:succ-78-79)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n78 hopper:nat:n79)))
      (rm-block nat-succ-79-80 hopper:nat:succ-79-80
        (quote hopper:nat:proof:succ-79-80)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n79 hopper:nat:n80)))
      (rm-block nat-succ-80-81 hopper:nat:succ-80-81
        (quote hopper:nat:proof:succ-80-81)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n80 hopper:nat:n81)))
      (rm-block nat-succ-81-82 hopper:nat:succ-81-82
        (quote hopper:nat:proof:succ-81-82)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n81 hopper:nat:n82)))
      (rm-block nat-succ-82-83 hopper:nat:succ-82-83
        (quote hopper:nat:proof:succ-82-83)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n82 hopper:nat:n83)))
      (rm-block nat-succ-83-84 hopper:nat:succ-83-84
        (quote hopper:nat:proof:succ-83-84)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n83 hopper:nat:n84)))
      (rm-block nat-succ-84-85 hopper:nat:succ-84-85
        (quote hopper:nat:proof:succ-84-85)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n84 hopper:nat:n85)))
      (rm-block nat-succ-85-86 hopper:nat:succ-85-86
        (quote hopper:nat:proof:succ-85-86)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n85 hopper:nat:n86)))
      (rm-block nat-succ-86-87 hopper:nat:succ-86-87
        (quote hopper:nat:proof:succ-86-87)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n86 hopper:nat:n87)))
      (rm-block nat-succ-87-88 hopper:nat:succ-87-88
        (quote hopper:nat:proof:succ-87-88)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n87 hopper:nat:n88)))
      (rm-block nat-succ-88-89 hopper:nat:succ-88-89
        (quote hopper:nat:proof:succ-88-89)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n88 hopper:nat:n89)))
      (rm-block nat-succ-89-90 hopper:nat:succ-89-90
        (quote hopper:nat:proof:succ-89-90)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n89 hopper:nat:n90)))
      (rm-block nat-succ-90-91 hopper:nat:succ-90-91
        (quote hopper:nat:proof:succ-90-91)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n90 hopper:nat:n91)))
      (rm-block nat-succ-91-92 hopper:nat:succ-91-92
        (quote hopper:nat:proof:succ-91-92)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n91 hopper:nat:n92)))
      (rm-block nat-succ-92-93 hopper:nat:succ-92-93
        (quote hopper:nat:proof:succ-92-93)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n92 hopper:nat:n93)))
      (rm-block nat-succ-93-94 hopper:nat:succ-93-94
        (quote hopper:nat:proof:succ-93-94)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n93 hopper:nat:n94)))
      (rm-block nat-succ-94-95 hopper:nat:succ-94-95
        (quote hopper:nat:proof:succ-94-95)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n94 hopper:nat:n95)))
      (rm-block nat-succ-95-96 hopper:nat:succ-95-96
        (quote hopper:nat:proof:succ-95-96)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n95 hopper:nat:n96)))
      (rm-block nat-succ-96-97 hopper:nat:succ-96-97
        (quote hopper:nat:proof:succ-96-97)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n96 hopper:nat:n97)))
      (rm-block nat-succ-97-98 hopper:nat:succ-97-98
        (quote hopper:nat:proof:succ-97-98)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n97 hopper:nat:n98)))
      (rm-block nat-succ-98-99 hopper:nat:succ-98-99
        (quote hopper:nat:proof:succ-98-99)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n98 hopper:nat:n99)))
      (rm-block nat-succ-99-100 hopper:nat:succ-99-100
        (quote hopper:nat:proof:succ-99-100)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n99 hopper:nat:n100)))
      (rm-block nat-succ-100-101 hopper:nat:succ-100-101
        (quote hopper:nat:proof:succ-100-101)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n100 hopper:nat:n101)))
      (rm-block nat-succ-101-102 hopper:nat:succ-101-102
        (quote hopper:nat:proof:succ-101-102)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n101 hopper:nat:n102)))
      (rm-block nat-pred hopper:nat:pred
        (quote (hopper:nat:proof:pred-from-succ
          $later $earlier (unquote $successor-evidence)))
        (rm-cons
          (rm-premise $successor-evidence
            (quote (hopper:nat:succ $earlier $later)))
          rm-nil)
        (quote (hopper:nat:pred $later $earlier)))
      (rm-block nat-even-zero hopper:nat:even-zero
        (quote hopper:nat:proof:even-zero)
        rm-nil
        (quote (hopper:nat:even hopper:nat:n0)))
      (rm-block nat-even-step hopper:nat:even-step
        (quote (hopper:nat:proof:even-step
          $earlier $middle $value
          (unquote $earlier-evidence)
          (unquote $first-successor)
          (unquote $second-successor)))
        (rm-cons
          (rm-premise $earlier-evidence
            (quote (hopper:nat:even $earlier)))
          (rm-cons
            (rm-premise $first-successor
              (quote (hopper:nat:succ $earlier $middle)))
            (rm-cons
              (rm-premise $second-successor
                (quote (hopper:nat:succ $middle $value)))
              rm-nil)))
        (quote (hopper:nat:even $value)))
      (rm-block nat-geq-zero hopper:nat:geq-zero
        (quote (hopper:nat:proof:geq-zero $value))
        rm-nil
        (quote (hopper:nat:geq $value hopper:nat:n0)))
      (rm-block nat-geq-step hopper:nat:geq-step
        (quote (hopper:nat:proof:geq-step
          $left-earlier $left-later $right-earlier $right-later
          (unquote $left-successor) (unquote $right-successor)
          (unquote $earlier-evidence)))
        (rm-cons
          (rm-premise $left-successor
            (quote (hopper:nat:succ $left-earlier $left-later)))
          (rm-cons
            (rm-premise $right-successor
              (quote (hopper:nat:succ $right-earlier $right-later)))
            (rm-cons
              (rm-premise $earlier-evidence
                (quote (hopper:nat:geq $left-earlier $right-earlier)))
              rm-nil)))
        (quote (hopper:nat:geq $left-later $right-later)))
      (rm-block iterate-zero rel:iterate-zero
        (quote (rel:iterate:zero $value $counter $step
          $predecessor $zero $source))
        rm-nil
        (quote (rel:iterate $value $counter $step $predecessor
          $zero $source $zero $source)))
      (rm-block iterate-step rel:iterate-step
        (quote (rel:iterate:step $value $counter $step
          $predecessor $zero $source $next $later $earlier $target
          (unquote $predecessor-evidence)
          (unquote $step-evidence)
          (unquote $recursive-evidence)))
        (rm-cons
          (rm-premise $predecessor-evidence
            (quote ($predecessor $later $earlier)))
          (rm-cons
            (rm-premise $step-evidence
              (quote ($step $source $next)))
            (rm-cons
              (rm-premise $recursive-evidence
                (quote (rel:iterate $value $counter $step
                  $predecessor $zero $next $earlier $target)))
              rm-nil)))
        (quote (rel:iterate $value $counter $step $predecessor
          $zero $source $later $target)))
      (rm-block all-nil rel:all-nil
        (quote (rel:all:nil $element $predicate))
        rm-nil
        (quote (rel:all $element $predicate (list:nil $element))))
      (rm-block all-cons rel:all-cons
        (quote (rel:all:cons $element $predicate $head $tail
          (unquote $head-evidence) (unquote $tail-evidence)))
        (rm-cons
          (rm-premise $head-evidence (quote ($predicate $head)))
          (rm-cons
            (rm-premise $tail-evidence
              (quote (rel:all $element $predicate $tail)))
            rm-nil))
        (quote (rel:all $element $predicate
          (list:cons $element $head $tail))))
      (rm-block any-here rel:any-here
        (quote (rel:any:here $element $target $predicate
          $values $answer (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence (quote ($predicate $values $answer)))
          rm-nil)
        (quote (rel:any $element $target $predicate $values $answer)))
      (rm-block any-there rel:any-there
        (quote (rel:any:there $element $target $predicate
          $head $tail $answer (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (rel:any $element $target $predicate $tail $answer)))
          rm-nil)
        (quote (rel:any $element $target $predicate
          (list:cons $element $head $tail) $answer)))
      (rm-block case-list-nil rel:case-list-nil
        (quote (rel:case-list:nil $element $target $nil-case
          $cons-case $answer (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence (quote ($nil-case $answer)))
          rm-nil)
        (quote (rel:case-list $element $target $nil-case $cons-case
          (list:nil $element) $answer)))
      (rm-block case-list-cons rel:case-list-cons
        (quote (rel:case-list:cons $element $target $nil-case
          $cons-case $head $tail $answer (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote ($cons-case $head $tail $answer)))
          rm-nil)
        (quote (rel:case-list $element $target $nil-case $cons-case
          (list:cons $element $head $tail) $answer)))
      (rm-block fold-nil rel:fold-nil
        (quote (rel:fold:nil $element $accumulator $step $before))
        rm-nil
        (quote (rel:fold $element $accumulator $step $before
          (list:nil $element) $before)))
      (rm-block fold-cons rel:fold-cons
        (quote (rel:fold:cons $element $accumulator $step
          $before $head $tail $next $after
          (unquote $step-evidence) (unquote $tail-evidence)))
        (rm-cons
          (rm-premise $step-evidence
            (quote ($step $before $head $next)))
          (rm-cons
            (rm-premise $tail-evidence
              (quote (rel:fold $element $accumulator $step
                $next $tail $after)))
            rm-nil))
        (quote (rel:fold $element $accumulator $step $before
          (list:cons $element $head $tail) $after)))
      (rm-block find-dup-recursive hopper:find-dup:recursive
        (quote (hopper:find-dup:proof:recursive $head $tail $answer
          (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (rel:case-list hopper:atom hopper:atom
              hopper:find-dup:nil-case hopper:find-dup:cons-case
              $tail $answer)))
          rm-nil)
        (quote (hopper:find-dup:cons-case $head $tail $answer)))
      (rm-block find-dup-here hopper:find-dup:duplicate
        (quote (hopper:find-dup:proof:duplicate $head $tail
          (unquote $member-evidence)))
        (rm-cons
          (rm-premise $member-evidence
            (quote (rel:list:member hopper:atom $tail $head)))
          rm-nil)
        (quote (hopper:find-dup:cons-case $head $tail $head)))
      (rm-block length-step hopper:length:step
        (quote (hopper:length:step-proof $before $head $after
          (unquote $successor-evidence)))
        (rm-cons
          (rm-premise $successor-evidence
            (quote (hopper:nat:succ $before $after)))
          rm-nil)
        (quote (hopper:length:step $before $head $after)))
      (rm-block sorted-step hopper:sorted:step
        (quote (hopper:sorted:step-proof $before $head $after
          (unquote $successor-evidence) (unquote $order-evidence)))
        (rm-cons
          (rm-premise $successor-evidence
            (quote (hopper:nat:succ $head $after)))
          (rm-cons
            (rm-premise $order-evidence
              (quote (hopper:nat:geq $after $before)))
            rm-nil))
        (quote (hopper:sorted:step $before $head $after)))
      (rm-block reverse-step hopper:reverse:step
        (quote (hopper:reverse:step-proof $before $head
          (rel:list:head-proof hopper:atom $head $before)
          (rel:list:tail-proof hopper:atom $head $before)))
        rm-nil
        (quote (hopper:reverse:step $before $head
          (list:cons hopper:atom $head $before))))
      (rm-block target-dropk hopper:target:dropk
        (quote (hopper:dropk:proof $count $source $target
          (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (rel:iterate (list hopper:atom) hopper:nat
              hopper:atom-list:tail hopper:nat:pred hopper:nat:n0
              $source $count $target)))
          rm-nil)
        (quote (hopper:dropk:f $count $source $target)))
      (rm-block target-all-even hopper:target:all-even
        (quote (hopper:all-even:proof $values (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (rel:all hopper:nat hopper:nat:even $values)))
          rm-nil)
        (quote (hopper:all-even:f $values)))
      (rm-block target-find-dup hopper:target:find-dup
        (quote (hopper:find-dup:proof $values $answer
          (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (rel:case-list hopper:atom hopper:atom
              hopper:find-dup:nil-case hopper:find-dup:cons-case
              $values $answer)))
          rm-nil)
        (quote (hopper:find-dup:f $values $answer)))
      (rm-block target-length hopper:target:length
        (quote (hopper:length:proof $values $length
          (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (rel:fold hopper:atom hopper:nat hopper:length:step
              hopper:nat:n0 $values $length)))
          rm-nil)
        (quote (hopper:length:f $values $length)))
      (rm-block target-member hopper:target:member
        (quote (hopper:member:proof $values $answer
          (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (rel:any hopper:atom hopper:atom
              hopper:atom-list:head $values $answer)))
          rm-nil)
        (quote (hopper:member:f $values $answer)))
      (rm-block target-sorted hopper:target:sorted
        (quote (hopper:sorted:proof $values $after
          (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (rel:fold hopper:nat hopper:nat hopper:sorted:step
              hopper:nat:n0 $values $after)))
          rm-nil)
        (quote (hopper:sorted:f $values)))
      (rm-block target-reverse hopper:target:reverse
        (quote (hopper:reverse:proof $source $target
          (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (rel:fold hopper:atom (list hopper:atom)
              hopper:reverse:step (list:nil hopper:atom)
              $source $target)))
          rm-nil)
        (quote (hopper:reverse:f $source $target)))
    )))
