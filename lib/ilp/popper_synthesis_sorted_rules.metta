; Popper's authored synthesis-sorted program over the exact
; reachable image of its extensional geq/2 background table.

(= (popper:sorted:package)
  (compile:rule-package popper-synthesis-sorted-v1
    (rm-package
      (rm-block list-empty popper:sorted:list-empty
        (quote (popper:list:proof:empty $element))
        rm-nil
        (quote (popper:list:empty $element (list:nil $element))))
      (rm-block list-head popper:sorted:list-head
        (quote (popper:list:proof:head $element $head $tail))
        rm-nil
        (quote (popper:list:head $element
          (list:cons $element $head $tail) $head)))
      (rm-block list-tail popper:sorted:list-tail
        (quote (popper:list:proof:tail $element $head $tail))
        rm-nil
        (quote (popper:list:tail $element
          (list:cons $element $head $tail) $tail)))
      (rm-block geq-2-1 popper:sorted:geq-2-1
        (quote popper:sorted:geq:proof:2-ge-1)
        rm-nil
        (quote (popper:sorted:geq popper:n2 popper:n1)))
      (rm-block geq-4-2 popper:sorted:geq-4-2
        (quote popper:sorted:geq:proof:4-ge-2)
        rm-nil
        (quote (popper:sorted:geq popper:n4 popper:n2)))
      (rm-block geq-4-4 popper:sorted:geq-4-4
        (quote popper:sorted:geq:proof:4-ge-4)
        rm-nil
        (quote (popper:sorted:geq popper:n4 popper:n4)))
      (rm-block geq-5-3 popper:sorted:geq-5-3
        (quote popper:sorted:geq:proof:5-ge-3)
        rm-nil
        (quote (popper:sorted:geq popper:n5 popper:n3)))
      (rm-block geq-6-2 popper:sorted:geq-6-2
        (quote popper:sorted:geq:proof:6-ge-2)
        rm-nil
        (quote (popper:sorted:geq popper:n6 popper:n2)))
      (rm-block geq-6-4 popper:sorted:geq-6-4
        (quote popper:sorted:geq:proof:6-ge-4)
        rm-nil
        (quote (popper:sorted:geq popper:n6 popper:n4)))
      (rm-block geq-8-6 popper:sorted:geq-8-6
        (quote popper:sorted:geq:proof:8-ge-6)
        rm-nil
        (quote (popper:sorted:geq popper:n8 popper:n6)))
      (rm-block geq-9-5 popper:sorted:geq-9-5
        (quote popper:sorted:geq:proof:9-ge-5)
        rm-nil
        (quote (popper:sorted:geq popper:n9 popper:n5)))
      (rm-block geq-10-6 popper:sorted:geq-10-6
        (quote popper:sorted:geq:proof:10-ge-6)
        rm-nil
        (quote (popper:sorted:geq popper:n10 popper:n6)))
      (rm-block geq-11-6 popper:sorted:geq-11-6
        (quote popper:sorted:geq:proof:11-ge-6)
        rm-nil
        (quote (popper:sorted:geq popper:n11 popper:n6)))
      (rm-block geq-11-7 popper:sorted:geq-11-7
        (quote popper:sorted:geq:proof:11-ge-7)
        rm-nil
        (quote (popper:sorted:geq popper:n11 popper:n7)))
      (rm-block geq-12-5 popper:sorted:geq-12-5
        (quote popper:sorted:geq:proof:12-ge-5)
        rm-nil
        (quote (popper:sorted:geq popper:n12 popper:n5)))
      (rm-block geq-12-11 popper:sorted:geq-12-11
        (quote popper:sorted:geq:proof:12-ge-11)
        rm-nil
        (quote (popper:sorted:geq popper:n12 popper:n11)))
      (rm-block geq-13-12 popper:sorted:geq-13-12
        (quote popper:sorted:geq:proof:13-ge-12)
        rm-nil
        (quote (popper:sorted:geq popper:n13 popper:n12)))
      (rm-block geq-14-11 popper:sorted:geq-14-11
        (quote popper:sorted:geq:proof:14-ge-11)
        rm-nil
        (quote (popper:sorted:geq popper:n14 popper:n11)))
      (rm-block geq-14-12 popper:sorted:geq-14-12
        (quote popper:sorted:geq:proof:14-ge-12)
        rm-nil
        (quote (popper:sorted:geq popper:n14 popper:n12)))
      (rm-block geq-15-8 popper:sorted:geq-15-8
        (quote popper:sorted:geq:proof:15-ge-8)
        rm-nil
        (quote (popper:sorted:geq popper:n15 popper:n8)))
      (rm-block geq-17-11 popper:sorted:geq-17-11
        (quote popper:sorted:geq:proof:17-ge-11)
        rm-nil
        (quote (popper:sorted:geq popper:n17 popper:n11)))
      (rm-block geq-17-14 popper:sorted:geq-17-14
        (quote popper:sorted:geq:proof:17-ge-14)
        rm-nil
        (quote (popper:sorted:geq popper:n17 popper:n14)))
      (rm-block geq-17-15 popper:sorted:geq-17-15
        (quote popper:sorted:geq:proof:17-ge-15)
        rm-nil
        (quote (popper:sorted:geq popper:n17 popper:n15)))
      (rm-block geq-18-13 popper:sorted:geq-18-13
        (quote popper:sorted:geq:proof:18-ge-13)
        rm-nil
        (quote (popper:sorted:geq popper:n18 popper:n13)))
      (rm-block geq-18-17 popper:sorted:geq-18-17
        (quote popper:sorted:geq:proof:18-ge-17)
        rm-nil
        (quote (popper:sorted:geq popper:n18 popper:n17)))
      (rm-block geq-19-18 popper:sorted:geq-19-18
        (quote popper:sorted:geq:proof:19-ge-18)
        rm-nil
        (quote (popper:sorted:geq popper:n19 popper:n18)))
      (rm-block geq-20-9 popper:sorted:geq-20-9
        (quote popper:sorted:geq:proof:20-ge-9)
        rm-nil
        (quote (popper:sorted:geq popper:n20 popper:n9)))
      (rm-block geq-20-15 popper:sorted:geq-20-15
        (quote popper:sorted:geq:proof:20-ge-15)
        rm-nil
        (quote (popper:sorted:geq popper:n20 popper:n15)))
      (rm-block geq-22-14 popper:sorted:geq-22-14
        (quote popper:sorted:geq:proof:22-ge-14)
        rm-nil
        (quote (popper:sorted:geq popper:n22 popper:n14)))
      (rm-block geq-22-17 popper:sorted:geq-22-17
        (quote popper:sorted:geq:proof:22-ge-17)
        rm-nil
        (quote (popper:sorted:geq popper:n22 popper:n17)))
      (rm-block geq-22-22 popper:sorted:geq-22-22
        (quote popper:sorted:geq:proof:22-ge-22)
        rm-nil
        (quote (popper:sorted:geq popper:n22 popper:n22)))
      (rm-block geq-23-19 popper:sorted:geq-23-19
        (quote popper:sorted:geq:proof:23-ge-19)
        rm-nil
        (quote (popper:sorted:geq popper:n23 popper:n19)))
      (rm-block geq-24-18 popper:sorted:geq-24-18
        (quote popper:sorted:geq:proof:24-ge-18)
        rm-nil
        (quote (popper:sorted:geq popper:n24 popper:n18)))
      (rm-block geq-24-20 popper:sorted:geq-24-20
        (quote popper:sorted:geq:proof:24-ge-20)
        rm-nil
        (quote (popper:sorted:geq popper:n24 popper:n20)))
      (rm-block geq-25-17 popper:sorted:geq-25-17
        (quote popper:sorted:geq:proof:25-ge-17)
        rm-nil
        (quote (popper:sorted:geq popper:n25 popper:n17)))
      (rm-block geq-25-23 popper:sorted:geq-25-23
        (quote popper:sorted:geq:proof:25-ge-23)
        rm-nil
        (quote (popper:sorted:geq popper:n25 popper:n23)))
      (rm-block geq-25-24 popper:sorted:geq-25-24
        (quote popper:sorted:geq:proof:25-ge-24)
        rm-nil
        (quote (popper:sorted:geq popper:n25 popper:n24)))
      (rm-block geq-26-25 popper:sorted:geq-26-25
        (quote popper:sorted:geq:proof:26-ge-25)
        rm-nil
        (quote (popper:sorted:geq popper:n26 popper:n25)))
      (rm-block geq-28-22 popper:sorted:geq-28-22
        (quote popper:sorted:geq:proof:28-ge-22)
        rm-nil
        (quote (popper:sorted:geq popper:n28 popper:n22)))
      (rm-block geq-28-26 popper:sorted:geq-28-26
        (quote popper:sorted:geq:proof:28-ge-26)
        rm-nil
        (quote (popper:sorted:geq popper:n28 popper:n26)))
      (rm-block geq-29-28 popper:sorted:geq-29-28
        (quote popper:sorted:geq:proof:29-ge-28)
        rm-nil
        (quote (popper:sorted:geq popper:n29 popper:n28)))
      (rm-block geq-30-25 popper:sorted:geq-30-25
        (quote popper:sorted:geq:proof:30-ge-25)
        rm-nil
        (quote (popper:sorted:geq popper:n30 popper:n25)))
      (rm-block geq-31-25 popper:sorted:geq-31-25
        (quote popper:sorted:geq:proof:31-ge-25)
        rm-nil
        (quote (popper:sorted:geq popper:n31 popper:n25)))
      (rm-block geq-33-22 popper:sorted:geq-33-22
        (quote popper:sorted:geq:proof:33-ge-22)
        rm-nil
        (quote (popper:sorted:geq popper:n33 popper:n22)))
      (rm-block geq-33-26 popper:sorted:geq-33-26
        (quote popper:sorted:geq:proof:33-ge-26)
        rm-nil
        (quote (popper:sorted:geq popper:n33 popper:n26)))
      (rm-block geq-34-30 popper:sorted:geq-34-30
        (quote popper:sorted:geq:proof:34-ge-30)
        rm-nil
        (quote (popper:sorted:geq popper:n34 popper:n30)))
      (rm-block geq-35-29 popper:sorted:geq-35-29
        (quote popper:sorted:geq:proof:35-ge-29)
        rm-nil
        (quote (popper:sorted:geq popper:n35 popper:n29)))
      (rm-block geq-35-31 popper:sorted:geq-35-31
        (quote popper:sorted:geq:proof:35-ge-31)
        rm-nil
        (quote (popper:sorted:geq popper:n35 popper:n31)))
      (rm-block geq-36-33 popper:sorted:geq-36-33
        (quote popper:sorted:geq:proof:36-ge-33)
        rm-nil
        (quote (popper:sorted:geq popper:n36 popper:n33)))
      (rm-block geq-36-35 popper:sorted:geq-36-35
        (quote popper:sorted:geq:proof:36-ge-35)
        rm-nil
        (quote (popper:sorted:geq popper:n36 popper:n35)))
      (rm-block geq-37-35 popper:sorted:geq-37-35
        (quote popper:sorted:geq:proof:37-ge-35)
        rm-nil
        (quote (popper:sorted:geq popper:n37 popper:n35)))
      (rm-block geq-37-36 popper:sorted:geq-37-36
        (quote popper:sorted:geq:proof:37-ge-36)
        rm-nil
        (quote (popper:sorted:geq popper:n37 popper:n36)))
      (rm-block geq-38-10 popper:sorted:geq-38-10
        (quote popper:sorted:geq:proof:38-ge-10)
        rm-nil
        (quote (popper:sorted:geq popper:n38 popper:n10)))
      (rm-block geq-38-20 popper:sorted:geq-38-20
        (quote popper:sorted:geq:proof:38-ge-20)
        rm-nil
        (quote (popper:sorted:geq popper:n38 popper:n20)))
      (rm-block geq-38-36 popper:sorted:geq-38-36
        (quote popper:sorted:geq:proof:38-ge-36)
        rm-nil
        (quote (popper:sorted:geq popper:n38 popper:n36)))
      (rm-block geq-39-28 popper:sorted:geq-39-28
        (quote popper:sorted:geq:proof:39-ge-28)
        rm-nil
        (quote (popper:sorted:geq popper:n39 popper:n28)))
      (rm-block geq-39-37 popper:sorted:geq-39-37
        (quote popper:sorted:geq:proof:39-ge-37)
        rm-nil
        (quote (popper:sorted:geq popper:n39 popper:n37)))
      (rm-block geq-39-38 popper:sorted:geq-39-38
        (quote popper:sorted:geq:proof:39-ge-38)
        rm-nil
        (quote (popper:sorted:geq popper:n39 popper:n38)))
      (rm-block geq-40-39 popper:sorted:geq-40-39
        (quote popper:sorted:geq:proof:40-ge-39)
        rm-nil
        (quote (popper:sorted:geq popper:n40 popper:n39)))
      (rm-block geq-41-33 popper:sorted:geq-41-33
        (quote popper:sorted:geq:proof:41-ge-33)
        rm-nil
        (quote (popper:sorted:geq popper:n41 popper:n33)))
      (rm-block geq-41-37 popper:sorted:geq-41-37
        (quote popper:sorted:geq:proof:41-ge-37)
        rm-nil
        (quote (popper:sorted:geq popper:n41 popper:n37)))
      (rm-block geq-43-40 popper:sorted:geq-43-40
        (quote popper:sorted:geq:proof:43-ge-40)
        rm-nil
        (quote (popper:sorted:geq popper:n43 popper:n40)))
      (rm-block geq-44-34 popper:sorted:geq-44-34
        (quote popper:sorted:geq:proof:44-ge-34)
        rm-nil
        (quote (popper:sorted:geq popper:n44 popper:n34)))
      (rm-block geq-45-39 popper:sorted:geq-45-39
        (quote popper:sorted:geq:proof:45-ge-39)
        rm-nil
        (quote (popper:sorted:geq popper:n45 popper:n39)))
      (rm-block geq-45-43 popper:sorted:geq-45-43
        (quote popper:sorted:geq:proof:45-ge-43)
        rm-nil
        (quote (popper:sorted:geq popper:n45 popper:n43)))
      (rm-block geq-45-45 popper:sorted:geq-45-45
        (quote popper:sorted:geq:proof:45-ge-45)
        rm-nil
        (quote (popper:sorted:geq popper:n45 popper:n45)))
      (rm-block geq-46-38 popper:sorted:geq-46-38
        (quote popper:sorted:geq:proof:46-ge-38)
        rm-nil
        (quote (popper:sorted:geq popper:n46 popper:n38)))
      (rm-block geq-47-39 popper:sorted:geq-47-39
        (quote popper:sorted:geq:proof:47-ge-39)
        rm-nil
        (quote (popper:sorted:geq popper:n47 popper:n39)))
      (rm-block geq-48-45 popper:sorted:geq-48-45
        (quote popper:sorted:geq:proof:48-ge-45)
        rm-nil
        (quote (popper:sorted:geq popper:n48 popper:n45)))
      (rm-block geq-49-41 popper:sorted:geq-49-41
        (quote popper:sorted:geq:proof:49-ge-41)
        rm-nil
        (quote (popper:sorted:geq popper:n49 popper:n41)))
      (rm-block geq-49-42 popper:sorted:geq-49-42
        (quote popper:sorted:geq:proof:49-ge-42)
        rm-nil
        (quote (popper:sorted:geq popper:n49 popper:n42)))
      (rm-block geq-49-44 popper:sorted:geq-49-44
        (quote popper:sorted:geq:proof:49-ge-44)
        rm-nil
        (quote (popper:sorted:geq popper:n49 popper:n44)))
      (rm-block geq-49-45 popper:sorted:geq-49-45
        (quote popper:sorted:geq:proof:49-ge-45)
        rm-nil
        (quote (popper:sorted:geq popper:n49 popper:n45)))
      (rm-block geq-49-49 popper:sorted:geq-49-49
        (quote popper:sorted:geq:proof:49-ge-49)
        rm-nil
        (quote (popper:sorted:geq popper:n49 popper:n49)))
      (rm-block geq-50-41 popper:sorted:geq-50-41
        (quote popper:sorted:geq:proof:50-ge-41)
        rm-nil
        (quote (popper:sorted:geq popper:n50 popper:n41)))
      (rm-block geq-50-49 popper:sorted:geq-50-49
        (quote popper:sorted:geq:proof:50-ge-49)
        rm-nil
        (quote (popper:sorted:geq popper:n50 popper:n49)))
      (rm-block geq-51-48 popper:sorted:geq-51-48
        (quote popper:sorted:geq:proof:51-ge-48)
        rm-nil
        (quote (popper:sorted:geq popper:n51 popper:n48)))
      (rm-block geq-51-49 popper:sorted:geq-51-49
        (quote popper:sorted:geq:proof:51-ge-49)
        rm-nil
        (quote (popper:sorted:geq popper:n51 popper:n49)))
      (rm-block geq-52-38 popper:sorted:geq-52-38
        (quote popper:sorted:geq:proof:52-ge-38)
        rm-nil
        (quote (popper:sorted:geq popper:n52 popper:n38)))
      (rm-block geq-52-50 popper:sorted:geq-52-50
        (quote popper:sorted:geq:proof:52-ge-50)
        rm-nil
        (quote (popper:sorted:geq popper:n52 popper:n50)))
      (rm-block geq-52-51 popper:sorted:geq-52-51
        (quote popper:sorted:geq:proof:52-ge-51)
        rm-nil
        (quote (popper:sorted:geq popper:n52 popper:n51)))
      (rm-block geq-53-47 popper:sorted:geq-53-47
        (quote popper:sorted:geq:proof:53-ge-47)
        rm-nil
        (quote (popper:sorted:geq popper:n53 popper:n47)))
      (rm-block geq-53-51 popper:sorted:geq-53-51
        (quote popper:sorted:geq:proof:53-ge-51)
        rm-nil
        (quote (popper:sorted:geq popper:n53 popper:n51)))
      (rm-block geq-54-53 popper:sorted:geq-54-53
        (quote popper:sorted:geq:proof:54-ge-53)
        rm-nil
        (quote (popper:sorted:geq popper:n54 popper:n53)))
      (rm-block geq-56-52 popper:sorted:geq-56-52
        (quote popper:sorted:geq:proof:56-ge-52)
        rm-nil
        (quote (popper:sorted:geq popper:n56 popper:n52)))
      (rm-block geq-56-54 popper:sorted:geq-56-54
        (quote popper:sorted:geq:proof:56-ge-54)
        rm-nil
        (quote (popper:sorted:geq popper:n56 popper:n54)))
      (rm-block geq-57-52 popper:sorted:geq-57-52
        (quote popper:sorted:geq:proof:57-ge-52)
        rm-nil
        (quote (popper:sorted:geq popper:n57 popper:n52)))
      (rm-block geq-57-56 popper:sorted:geq-57-56
        (quote popper:sorted:geq:proof:57-ge-56)
        rm-nil
        (quote (popper:sorted:geq popper:n57 popper:n56)))
      (rm-block geq-59-57 popper:sorted:geq-59-57
        (quote popper:sorted:geq:proof:59-ge-57)
        rm-nil
        (quote (popper:sorted:geq popper:n59 popper:n57)))
      (rm-block geq-60-12 popper:sorted:geq-60-12
        (quote popper:sorted:geq:proof:60-ge-12)
        rm-nil
        (quote (popper:sorted:geq popper:n60 popper:n12)))
      (rm-block geq-60-52 popper:sorted:geq-60-52
        (quote popper:sorted:geq:proof:60-ge-52)
        rm-nil
        (quote (popper:sorted:geq popper:n60 popper:n52)))
      (rm-block geq-60-56 popper:sorted:geq-60-56
        (quote popper:sorted:geq:proof:60-ge-56)
        rm-nil
        (quote (popper:sorted:geq popper:n60 popper:n56)))
      (rm-block geq-60-57 popper:sorted:geq-60-57
        (quote popper:sorted:geq:proof:60-ge-57)
        rm-nil
        (quote (popper:sorted:geq popper:n60 popper:n57)))
      (rm-block geq-60-59 popper:sorted:geq-60-59
        (quote popper:sorted:geq:proof:60-ge-59)
        rm-nil
        (quote (popper:sorted:geq popper:n60 popper:n59)))
      (rm-block geq-61-49 popper:sorted:geq-61-49
        (quote popper:sorted:geq:proof:61-ge-49)
        rm-nil
        (quote (popper:sorted:geq popper:n61 popper:n49)))
      (rm-block geq-61-60 popper:sorted:geq-61-60
        (quote popper:sorted:geq:proof:61-ge-60)
        rm-nil
        (quote (popper:sorted:geq popper:n61 popper:n60)))
      (rm-block geq-61-61 popper:sorted:geq-61-61
        (quote popper:sorted:geq:proof:61-ge-61)
        rm-nil
        (quote (popper:sorted:geq popper:n61 popper:n61)))
      (rm-block geq-62-60 popper:sorted:geq-62-60
        (quote popper:sorted:geq:proof:62-ge-60)
        rm-nil
        (quote (popper:sorted:geq popper:n62 popper:n60)))
      (rm-block geq-62-61 popper:sorted:geq-62-61
        (quote popper:sorted:geq:proof:62-ge-61)
        rm-nil
        (quote (popper:sorted:geq popper:n62 popper:n61)))
      (rm-block geq-63-62 popper:sorted:geq-63-62
        (quote popper:sorted:geq:proof:63-ge-62)
        rm-nil
        (quote (popper:sorted:geq popper:n63 popper:n62)))
      (rm-block geq-64-54 popper:sorted:geq-64-54
        (quote popper:sorted:geq:proof:64-ge-54)
        rm-nil
        (quote (popper:sorted:geq popper:n64 popper:n54)))
      (rm-block geq-64-62 popper:sorted:geq-64-62
        (quote popper:sorted:geq:proof:64-ge-62)
        rm-nil
        (quote (popper:sorted:geq popper:n64 popper:n62)))
      (rm-block geq-64-63 popper:sorted:geq-64-63
        (quote popper:sorted:geq:proof:64-ge-63)
        rm-nil
        (quote (popper:sorted:geq popper:n64 popper:n63)))
      (rm-block geq-66-62 popper:sorted:geq-66-62
        (quote popper:sorted:geq:proof:66-ge-62)
        rm-nil
        (quote (popper:sorted:geq popper:n66 popper:n62)))
      (rm-block geq-66-64 popper:sorted:geq-66-64
        (quote popper:sorted:geq:proof:66-ge-64)
        rm-nil
        (quote (popper:sorted:geq popper:n66 popper:n64)))
      (rm-block geq-66-66 popper:sorted:geq-66-66
        (quote popper:sorted:geq:proof:66-ge-66)
        rm-nil
        (quote (popper:sorted:geq popper:n66 popper:n66)))
      (rm-block geq-67-66 popper:sorted:geq-67-66
        (quote popper:sorted:geq:proof:67-ge-66)
        rm-nil
        (quote (popper:sorted:geq popper:n67 popper:n66)))
      (rm-block geq-68-46 popper:sorted:geq-68-46
        (quote popper:sorted:geq:proof:68-ge-46)
        rm-nil
        (quote (popper:sorted:geq popper:n68 popper:n46)))
      (rm-block geq-68-66 popper:sorted:geq-68-66
        (quote popper:sorted:geq:proof:68-ge-66)
        rm-nil
        (quote (popper:sorted:geq popper:n68 popper:n66)))
      (rm-block geq-70-60 popper:sorted:geq-70-60
        (quote popper:sorted:geq:proof:70-ge-60)
        rm-nil
        (quote (popper:sorted:geq popper:n70 popper:n60)))
      (rm-block geq-70-64 popper:sorted:geq-70-64
        (quote popper:sorted:geq:proof:70-ge-64)
        rm-nil
        (quote (popper:sorted:geq popper:n70 popper:n64)))
      (rm-block geq-70-66 popper:sorted:geq-70-66
        (quote popper:sorted:geq:proof:70-ge-66)
        rm-nil
        (quote (popper:sorted:geq popper:n70 popper:n66)))
      (rm-block geq-70-68 popper:sorted:geq-70-68
        (quote popper:sorted:geq:proof:70-ge-68)
        rm-nil
        (quote (popper:sorted:geq popper:n70 popper:n68)))
      (rm-block geq-71-30 popper:sorted:geq-71-30
        (quote popper:sorted:geq:proof:71-ge-30)
        rm-nil
        (quote (popper:sorted:geq popper:n71 popper:n30)))
      (rm-block geq-71-70 popper:sorted:geq-71-70
        (quote popper:sorted:geq:proof:71-ge-70)
        rm-nil
        (quote (popper:sorted:geq popper:n71 popper:n70)))
      (rm-block geq-74-20 popper:sorted:geq-74-20
        (quote popper:sorted:geq:proof:74-ge-20)
        rm-nil
        (quote (popper:sorted:geq popper:n74 popper:n20)))
      (rm-block geq-74-50 popper:sorted:geq-74-50
        (quote popper:sorted:geq:proof:74-ge-50)
        rm-nil
        (quote (popper:sorted:geq popper:n74 popper:n50)))
      (rm-block geq-74-53 popper:sorted:geq-74-53
        (quote popper:sorted:geq:proof:74-ge-53)
        rm-nil
        (quote (popper:sorted:geq popper:n74 popper:n53)))
      (rm-block geq-75-67 popper:sorted:geq-75-67
        (quote popper:sorted:geq:proof:75-ge-67)
        rm-nil
        (quote (popper:sorted:geq popper:n75 popper:n67)))
      (rm-block geq-75-70 popper:sorted:geq-75-70
        (quote popper:sorted:geq:proof:75-ge-70)
        rm-nil
        (quote (popper:sorted:geq popper:n75 popper:n70)))
      (rm-block geq-75-75 popper:sorted:geq-75-75
        (quote popper:sorted:geq:proof:75-ge-75)
        rm-nil
        (quote (popper:sorted:geq popper:n75 popper:n75)))
      (rm-block geq-76-75 popper:sorted:geq-76-75
        (quote popper:sorted:geq:proof:76-ge-75)
        rm-nil
        (quote (popper:sorted:geq popper:n76 popper:n75)))
      (rm-block geq-77-75 popper:sorted:geq-77-75
        (quote popper:sorted:geq:proof:77-ge-75)
        rm-nil
        (quote (popper:sorted:geq popper:n77 popper:n75)))
      (rm-block geq-78-74 popper:sorted:geq-78-74
        (quote popper:sorted:geq:proof:78-ge-74)
        rm-nil
        (quote (popper:sorted:geq popper:n78 popper:n74)))
      (rm-block geq-78-77 popper:sorted:geq-78-77
        (quote popper:sorted:geq:proof:78-ge-77)
        rm-nil
        (quote (popper:sorted:geq popper:n78 popper:n77)))
      (rm-block geq-79-68 popper:sorted:geq-79-68
        (quote popper:sorted:geq:proof:79-ge-68)
        rm-nil
        (quote (popper:sorted:geq popper:n79 popper:n68)))
      (rm-block geq-79-74 popper:sorted:geq-79-74
        (quote popper:sorted:geq:proof:79-ge-74)
        rm-nil
        (quote (popper:sorted:geq popper:n79 popper:n74)))
      (rm-block geq-79-76 popper:sorted:geq-79-76
        (quote popper:sorted:geq:proof:79-ge-76)
        rm-nil
        (quote (popper:sorted:geq popper:n79 popper:n76)))
      (rm-block geq-79-78 popper:sorted:geq-79-78
        (quote popper:sorted:geq:proof:79-ge-78)
        rm-nil
        (quote (popper:sorted:geq popper:n79 popper:n78)))
      (rm-block geq-80-79 popper:sorted:geq-80-79
        (quote popper:sorted:geq:proof:80-ge-79)
        rm-nil
        (quote (popper:sorted:geq popper:n80 popper:n79)))
      (rm-block geq-81-80 popper:sorted:geq-81-80
        (quote popper:sorted:geq:proof:81-ge-80)
        rm-nil
        (quote (popper:sorted:geq popper:n81 popper:n80)))
      (rm-block geq-82-79 popper:sorted:geq-82-79
        (quote popper:sorted:geq:proof:82-ge-79)
        rm-nil
        (quote (popper:sorted:geq popper:n82 popper:n79)))
      (rm-block geq-83-79 popper:sorted:geq-83-79
        (quote popper:sorted:geq:proof:83-ge-79)
        rm-nil
        (quote (popper:sorted:geq popper:n83 popper:n79)))
      (rm-block geq-83-81 popper:sorted:geq-83-81
        (quote popper:sorted:geq:proof:83-ge-81)
        rm-nil
        (quote (popper:sorted:geq popper:n83 popper:n81)))
      (rm-block geq-84-70 popper:sorted:geq-84-70
        (quote popper:sorted:geq:proof:84-ge-70)
        rm-nil
        (quote (popper:sorted:geq popper:n84 popper:n70)))
      (rm-block geq-85-70 popper:sorted:geq-85-70
        (quote popper:sorted:geq:proof:85-ge-70)
        rm-nil
        (quote (popper:sorted:geq popper:n85 popper:n70)))
      (rm-block geq-85-71 popper:sorted:geq-85-71
        (quote popper:sorted:geq:proof:85-ge-71)
        rm-nil
        (quote (popper:sorted:geq popper:n85 popper:n71)))
      (rm-block geq-85-83 popper:sorted:geq-85-83
        (quote popper:sorted:geq:proof:85-ge-83)
        rm-nil
        (quote (popper:sorted:geq popper:n85 popper:n83)))
      (rm-block geq-87-79 popper:sorted:geq-87-79
        (quote popper:sorted:geq:proof:87-ge-79)
        rm-nil
        (quote (popper:sorted:geq popper:n87 popper:n79)))
      (rm-block geq-87-85 popper:sorted:geq-87-85
        (quote popper:sorted:geq:proof:87-ge-85)
        rm-nil
        (quote (popper:sorted:geq popper:n87 popper:n85)))
      (rm-block geq-88-82 popper:sorted:geq-88-82
        (quote popper:sorted:geq:proof:88-ge-82)
        rm-nil
        (quote (popper:sorted:geq popper:n88 popper:n82)))
      (rm-block geq-88-85 popper:sorted:geq-88-85
        (quote popper:sorted:geq:proof:88-ge-85)
        rm-nil
        (quote (popper:sorted:geq popper:n88 popper:n85)))
      (rm-block geq-89-87 popper:sorted:geq-89-87
        (quote popper:sorted:geq:proof:89-ge-87)
        rm-nil
        (quote (popper:sorted:geq popper:n89 popper:n87)))
      (rm-block geq-89-88 popper:sorted:geq-89-88
        (quote popper:sorted:geq:proof:89-ge-88)
        rm-nil
        (quote (popper:sorted:geq popper:n89 popper:n88)))
      (rm-block geq-90-78 popper:sorted:geq-90-78
        (quote popper:sorted:geq:proof:90-ge-78)
        rm-nil
        (quote (popper:sorted:geq popper:n90 popper:n78)))
      (rm-block geq-90-84 popper:sorted:geq-90-84
        (quote popper:sorted:geq:proof:90-ge-84)
        rm-nil
        (quote (popper:sorted:geq popper:n90 popper:n84)))
      (rm-block geq-90-87 popper:sorted:geq-90-87
        (quote popper:sorted:geq:proof:90-ge-87)
        rm-nil
        (quote (popper:sorted:geq popper:n90 popper:n87)))
      (rm-block geq-90-89 popper:sorted:geq-90-89
        (quote popper:sorted:geq:proof:90-ge-89)
        rm-nil
        (quote (popper:sorted:geq popper:n90 popper:n89)))
      (rm-block geq-91-90 popper:sorted:geq-91-90
        (quote popper:sorted:geq:proof:91-ge-90)
        rm-nil
        (quote (popper:sorted:geq popper:n91 popper:n90)))
      (rm-block geq-92-91 popper:sorted:geq-92-91
        (quote popper:sorted:geq:proof:92-ge-91)
        rm-nil
        (quote (popper:sorted:geq popper:n92 popper:n91)))
      (rm-block geq-93-90 popper:sorted:geq-93-90
        (quote popper:sorted:geq:proof:93-ge-90)
        rm-nil
        (quote (popper:sorted:geq popper:n93 popper:n90)))
      (rm-block geq-95-85 popper:sorted:geq-95-85
        (quote popper:sorted:geq:proof:95-ge-85)
        rm-nil
        (quote (popper:sorted:geq popper:n95 popper:n85)))
      (rm-block geq-95-88 popper:sorted:geq-95-88
        (quote popper:sorted:geq:proof:95-ge-88)
        rm-nil
        (quote (popper:sorted:geq popper:n95 popper:n88)))
      (rm-block geq-96-93 popper:sorted:geq-96-93
        (quote popper:sorted:geq:proof:96-ge-93)
        rm-nil
        (quote (popper:sorted:geq popper:n96 popper:n93)))
      (rm-block geq-98-83 popper:sorted:geq-98-83
        (quote popper:sorted:geq:proof:98-ge-83)
        rm-nil
        (quote (popper:sorted:geq popper:n98 popper:n83)))
      (rm-block geq-98-93 popper:sorted:geq-98-93
        (quote popper:sorted:geq:proof:98-ge-93)
        rm-nil
        (quote (popper:sorted:geq popper:n98 popper:n93)))
      (rm-block geq-98-95 popper:sorted:geq-98-95
        (quote popper:sorted:geq:proof:98-ge-95)
        rm-nil
        (quote (popper:sorted:geq popper:n98 popper:n95)))
      (rm-block geq-99-95 popper:sorted:geq-99-95
        (quote popper:sorted:geq:proof:99-ge-95)
        rm-nil
        (quote (popper:sorted:geq popper:n99 popper:n95)))
      (rm-block sorted-two popper:sorted:two
        (quote (popper:sorted:proof:two $first $second
          (unquote $first-tail-evidence)
          (unquote $second-tail-evidence)
          (unquote $empty-evidence)))
        (rm-cons
          (rm-premise $first-tail-evidence
            (quote (popper:list:tail popper:numeral
              (list:cons popper:numeral $first
                (list:cons popper:numeral $second
                  (list:nil popper:numeral)))
              (list:cons popper:numeral $second
                (list:nil popper:numeral)))))
          (rm-cons
            (rm-premise $second-tail-evidence
              (quote (popper:list:tail popper:numeral
                (list:cons popper:numeral $second
                  (list:nil popper:numeral))
                (list:nil popper:numeral))))
            (rm-cons
              (rm-premise $empty-evidence
                (quote (popper:list:empty popper:numeral
                  (list:nil popper:numeral))))
              rm-nil)))
        (quote (popper:sorted:target
          (list:cons popper:numeral $first
            (list:cons popper:numeral $second
              (list:nil popper:numeral))))))
      (rm-block sorted-step popper:sorted:step
        (quote (popper:sorted:proof:step $current $next $rest
          (unquote $head-evidence) (unquote $tail-evidence)
          (unquote $next-head-evidence)
          (unquote $order-evidence)
          (unquote $recursive-evidence)))
        (rm-cons
          (rm-premise $head-evidence
            (quote (popper:list:head popper:numeral
              (list:cons popper:numeral $current
                (list:cons popper:numeral $next $rest))
              $current)))
          (rm-cons
            (rm-premise $tail-evidence
              (quote (popper:list:tail popper:numeral
                (list:cons popper:numeral $current
                  (list:cons popper:numeral $next $rest))
                (list:cons popper:numeral $next $rest))))
            (rm-cons
              (rm-premise $next-head-evidence
                (quote (popper:list:head popper:numeral
                  (list:cons popper:numeral $next $rest) $next)))
              (rm-cons
                (rm-premise $order-evidence
                  (quote (popper:sorted:geq $next $current)))
                (rm-cons
                  (rm-premise $recursive-evidence
                    (quote (popper:sorted:target
                      (list:cons popper:numeral $next $rest))))
                  rm-nil)))))
        (quote (popper:sorted:target
          (list:cons popper:numeral $current
            (list:cons popper:numeral $next $rest)))))
    )))
