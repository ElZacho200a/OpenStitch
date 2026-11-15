"""Keep the black-box gradient fixtures on the current shared-row engine."""
import ast
import copy
from pathlib import Path
import unittest


def fixture_helpers():
    # The executable QA script clears its output directory at import time.
    source = Path(__file__).resolve().parents[2] / "scripts" / "qa_pr336_gradients.py"
    tree = ast.parse(source.read_text(encoding="utf-8"))
    names = {"bbox", "tag", "shape_curve", "shared_density_stops", "gradient", "make_variant"}
    nodes = [node for node in tree.body
             if (isinstance(node, ast.FunctionDef) and node.name in names) or
             (isinstance(node, ast.Assign) and any(
                 isinstance(target, ast.Name) and target.id == "COLORS"
                 for target in node.targets))]
    namespace = {"copy": copy}
    exec(compile(ast.Module(body=nodes, type_ignores=[]), str(source), "exec"), namespace)
    return namespace


def base_document():
    return {"objectIdLast": 2,
            "vectorObjects": [{"paths": [{"outer": {"nodes": [
                {"pos": [0, 0]}, {"pos": [40000, 0]},
                {"pos": [40000, 20000]}, {"pos": [0, 20000]}]}}]}],
            "embroideryObjects": [{"id": 2, "params": {"type": "directional"}}]}


class TestSharedRowFixtures(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.helpers = fixture_helpers()

    def variant(self, count, **settings):
        doc = base_document()
        self.helpers["make_variant"](doc, "multi_5_linear", count, **settings)
        return doc["embroideryObjects"][1:]

    def test_current_multicolor_layers_partition_one_lattice(self):
        for count in (2, 3, 5, 8):
            with self.subTest(count=count):
                layers = self.variant(count)
                gradients = [o["params"]["densityGradient"] for o in layers]
                for g in gradients:
                    self.assertTrue(g["sharedRows"])
                    self.assertTrue(all(len(s) == 3 for s in g["densityStops"]))
                for stops in zip(*(g["densityStops"] for g in gradients)):
                    self.assertAlmostEqual(sum(stop[1] for stop in stops), 1.0)
                    lower = 0.0
                    for position, weight, start in stops:
                        self.assertGreaterEqual(position, 0.0)
                        self.assertLessEqual(position, 1.0)
                        self.assertAlmostEqual(start, lower)
                        lower += weight

    def test_nonlinear_profiles_are_sampled_and_remain_complementary(self):
        for curve in range(1, 9):
            for count in (2, 8):
                with self.subTest(curve=curve, count=count):
                    profiles = [self.helpers["shared_density_stops"](count, i, curve, True)
                                for i in range(count)]
                    self.assertGreater(len(profiles[0]), count)
                    positions = [stop[0] for stop in profiles[0]]
                    self.assertEqual(positions[0], 0.0)
                    self.assertEqual(positions[-1], 1.0)
                    self.assertTrue(all(a < b for a, b in zip(positions, positions[1:])))
                    for stops in zip(*profiles):
                        self.assertAlmostEqual(sum(stop[1] for stop in stops), 1.0)
                        self.assertTrue(all(0.0 <= s[2] <= s[2] + s[1] <= 1.0
                                            for s in stops))

    def test_overlay_uses_one_full_base_and_one_lighter_shared_lattice(self):
        layers = self.variant(8, comp=False)
        gradients = [o["params"]["densityGradient"] for o in layers]
        self.assertTrue(all(s[1:] == [1.0, 0.0] for s in gradients[0]["densityStops"]))
        self.assertEqual(gradients[0]["spacingFrom"], 420)
        self.assertTrue(all(g["spacingFrom"] == 700 for g in gradients[1:]))
        for stops in zip(*(g["densityStops"] for g in gradients)):
            self.assertLessEqual(sum(s[1] for s in stops[1:]), 1.0)
            lower = 0.0
            for _, weight, start in stops[1:]:
                self.assertAlmostEqual(start, lower)
                lower += weight

    def test_legacy_pair_and_satin_keep_their_own_generators(self):
        for settings in ({"legacy": True}, {"satin": True}):
            with self.subTest(settings=settings):
                doc = base_document()
                self.helpers["make_variant"](doc, "legacy_or_satin", 2, **settings)
                for obj in doc["embroideryObjects"]:
                    self.assertNotIn("sharedRows", obj["params"].get("densityGradient", {}))


if __name__ == "__main__":
    unittest.main()
