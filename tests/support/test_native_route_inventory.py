#!/usr/bin/env python3
"""Structural controls for the route inventory."""
import unittest
from check_native_route_inventory import route_table
from native_observer_cases import FAMILIES


class RouteInventoryControls(unittest.TestCase):
    def test_every_generated_family_has_one_entry(self):
        routes = route_table()
        self.assertEqual(set(FAMILIES), set(routes) - {"match-existence-partition"})
        self.assertEqual(len(routes), 14)

    def test_evidence_kinds_stay_distinct(self):
        routes = route_table()
        self.assertEqual(routes["scalar-fold"]["evidence"]["kind"], "named-return")
        self.assertEqual(routes["grouped-count"]["evidence"]["kind"], "inlined-checkpoint")
        self.assertEqual(routes["row-takeover"]["evidence"]["kind"], "operation-trace")
        self.assertEqual(routes["match-existence-partition"]["evidence"]["kind"],
                         "partitioned-caller-chain")
        self.assertTrue(all("offset" in item for item in routes["dead-rule"]["evidence"]["checkpoints"]))

    def test_languages_and_drivers_are_explicit(self):
        routes = route_table()
        self.assertEqual({route["language"] for route in routes.values()}, {"petta", "he"})
        self.assertEqual(routes["match-existence"]["driver"], "check_native_observers.py")
        self.assertEqual(routes["match-existence-partition"]["driver"], "check_existence_domains.py")

    def test_every_entry_names_a_source_anchor(self):
        for route in route_table().values():
            self.assertTrue(route["source"].startswith("src/"))
            self.assertTrue(route["anchor"])


if __name__ == "__main__":
    unittest.main()
