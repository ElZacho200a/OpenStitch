import json
import tempfile
import unittest
from pathlib import Path
from benchmark_report import parse, markdown, main

VALID = """segment (8 couleurs)                         froid    109.02 ms | chaud min    95.50 med   101.42 ms | pic   210.5 Mo
order ByColor n=100                          froid      0.12 ms | chaud min     0.10 med     0.10 ms | pic     0.0 Mo
empreinte: deadbeef
"""

class BenchmarkReportTests(unittest.TestCase):
    def test_parse_real_benchmark_line_format(self):
        rows = parse(VALID)
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0]['name'], 'segment (8 couleurs)')
        self.assertEqual(rows[0]['median_ms'], 101.42)
        self.assertEqual(rows[1]['peak_mb'], 0.0)

    def test_reject_empty_and_duplicate_samples(self):
        with self.assertRaisesRegex(ValueError, 'no benchmark'):
            parse('not a benchmark')
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            parse(VALID + VALID.splitlines()[0])

    def test_stable_markdown_and_escaping(self):
        rows = parse(VALID)
        self.assertEqual(markdown(rows), markdown(rows))
        rows[0]['name'] = 'bad|pipe'
        self.assertIn('bad\\|pipe', markdown(rows))

    def test_files_written_with_stable_schema(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            log = d/'bench.log'
            log.write_text(VALID)
            self.assertEqual(main([str(log), '--json', str(d/'bench.json'), '--markdown', str(d/'bench.md')]), 0)
            obj = json.loads((d/'bench.json').read_text())
            self.assertEqual(obj['schema'], 1)
            self.assertEqual(obj['measurements'][0]['median_ms'], 101.42)
            self.assertIn('| Phase |', (d/'bench.md').read_text())

if __name__ == '__main__':
    unittest.main()
