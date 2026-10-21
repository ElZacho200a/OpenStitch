"""Check GitHub issue and pull request templates for essential fields."""
from pathlib import Path
import unittest
import yaml

ROOT = Path(__file__).resolve().parents[2]
ISSUE_DIR = ROOT / '.github' / 'ISSUE_TEMPLATE'

class TemplateTests(unittest.TestCase):
    def test_issue_forms_have_unique_ids_and_required_descriptions(self):
        for filename in ('bug.yml', 'feature.yml'):
            with self.subTest(filename=filename):
                form = yaml.safe_load((ISSUE_DIR / filename).read_text(encoding='utf-8'))
                self.assertTrue(form['name'])
                self.assertTrue(form['description'])
                self.assertTrue(form['title'])
                ids = [field['id'] for field in form['body'] if 'id' in field]
                self.assertEqual(len(ids), len(set(ids)))
                for field in form['body']:
                    self.assertIn(field['type'], ('textarea', 'input', 'dropdown', 'markdown'))
                    if field['type'] != 'markdown':
                        self.assertTrue(field['attributes']['label'])

    def test_bug_requires_reproduction_and_version(self):
        form = yaml.safe_load((ISSUE_DIR / 'bug.yml').read_text(encoding='utf-8'))
        fields = {f.get('id'): f for f in form['body']}
        for key in ('reproduce', 'expected', 'actual', 'version', 'platform'):
            self.assertTrue(fields[key]['validations']['required'])

    def test_feature_requires_acceptance(self):
        form = yaml.safe_load((ISSUE_DIR / 'feature.yml').read_text(encoding='utf-8'))
        fields = {f.get('id'): f for f in form['body']}
        for key in ('problem', 'proposal', 'acceptance'):
            self.assertTrue(fields[key]['validations']['required'])

    def test_pr_template_and_contributing_reference_real_files(self):
        contribution = (ROOT / 'CONTRIBUTING.md').read_text(encoding='utf-8')
        template = (ROOT / '.github' / 'PULL_REQUEST_TEMPLATE.md').read_text(encoding='utf-8')
        for path in ('CLAUDE.md', 'GUARDRAILS.md', 'docs/source/contributing.md'):
            self.assertIn(path, contribution)
            self.assertTrue((ROOT / path).exists(), path)
        for term in ('QTest', 'clang-format', 'ICommand', 'roadmap'):
            self.assertIn(term, template)

if __name__ == '__main__':
    unittest.main()
