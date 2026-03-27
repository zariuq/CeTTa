; Removing a type annotation must also evict it from the type index.
!(add-atom &self (: foo Bar))
!(remove-atom &self (: foo Bar))
!(get-type foo)
