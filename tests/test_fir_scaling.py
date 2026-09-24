"""Synthetic mathematical and evidence controls for the FIR scaling report."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from report_fir_scaling import BLOCKS, TAPS, TARGET, fit, scaling, write_outputs


def fixture():
    return {'qualified':True, 'errors':[], 'profile':'synthetic',
            'metadata':{'build_id':'synthetic','tcm_requested':1},
            'cases':[{'block':b, 'taps':n, 'stable':True, 'overhead_resolved':True,
                      'candidate':{'median_cycles':0.7*b*n+2*b+3*n+4},
                      'baseline':{'median_cycles':0.8*b*n+2*b+3*n+4}}
                     for b in BLOCKS for n in TAPS]}


class ScalingTests(unittest.TestCase):
    def test_exact_polynomial_and_complete_region(self):
        result = scaling(fixture())
        self.assertEqual(len(result['points']),28)
        self.assertEqual(len(result['onset_neighbors']),9)
        self.assertEqual(len(result['rows_increasing_taps']),4)
        self.assertEqual(len(result['columns_increasing_block']),7)
        self.assertEqual(result['fit']['rank'],4)
        for key, expected in zip('abcd',(0.7,2,3,4)):
            self.assertAlmostEqual(result['fit']['coefficients'][key],expected,places=8)
        self.assertLess(result['fit']['residual_rms_cycles'],1e-8)
        first = result['points'][0]
        self.assertEqual((first['block'],first['taps']),(128,16))
        self.assertAlmostEqual(first['target_gap'],first['candidate_cycles']/2048-TARGET)
        self.assertTrue(result['all_case_parity'])

    def test_outlier_is_retained_and_parity_is_separate(self):
        report = fixture()
        row = report['cases'][-1]
        row['candidate']['median_cycles'] += 10000
        result = scaling(report)
        self.assertFalse(result['all_case_parity'])
        self.assertEqual(result['all_case_parity_failures'],1)
        self.assertEqual(result['fit']['point_count'],28)
        self.assertEqual(len(result['fit']['residuals']),28)
        self.assertGreater(result['fit']['residual_rms_cycles'],100)
        self.assertNotAlmostEqual(result['fit']['coefficients']['a'],0.7,places=3)
        self.assertEqual(result['points'][-1]['candidate_cycles'],row['candidate']['median_cycles'])

    def test_invalid_evidence_rejected(self):
        mutations = [lambda r:r.update(qualified=False),
                     lambda r:r['errors'].append('bad image'),
                     lambda r:r['metadata'].update(tcm_requested=0),
                     lambda r:r['cases'].pop(),
                     lambda r:r['cases'].__setitem__(1,copy.deepcopy(r['cases'][0])),
                     lambda r:r['cases'][0].update(stable=False),
                     lambda r:r['cases'][0].update(overhead_resolved=False),
                     lambda r:r['cases'][0]['candidate'].update(median_cycles=float('nan')),
                     lambda r:r['cases'][0]['baseline'].update(median_cycles=0)]
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                report = fixture()
                mutate(report)
                with self.assertRaises(ValueError):
                    scaling(report)

    def test_rank_deficiency_rejected(self):
        with self.assertRaises(ValueError):
            fit([{'block':128,'taps':16,'candidate_cycles':1400}]*28)

    def test_outputs_include_residuals_and_onset(self):
        result = scaling(fixture())
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            write_outputs(result,output)
            self.assertEqual(len((output/'scaling.csv').read_text().splitlines()),29)
            reread = json.loads((output/'scaling.json').read_text())
            self.assertEqual(len(reread['fit']['residuals']),28)
            self.assertIn('127 | 15',(output/'scaling.md').read_text())


if __name__ == '__main__':
    unittest.main()
