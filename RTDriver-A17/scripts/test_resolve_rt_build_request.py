import unittest
from resolve_rt_build_request import resolve


class Tests(unittest.TestCase):
    def test_explicit_modes(self):
        for name in ("diag", "port"):
            self.assertEqual(resolve([['A', f'RTDriver-A17/build-requests/rt-{name}-one.json']],
                             'abc', {'source_commit': 'abc', 'mode': f'kernel_and_rt_{name}'}),
                             f'kernel_and_rt_{name}')

    def test_rejects_ambiguous_modified_and_mismatched_requests(self):
        path = 'RTDriver-A17/build-requests/rt-port-one.json'
        request = {'source_commit': 'abc', 'mode': 'kernel_and_rt_port'}
        for changes, body in (([], request), ([['M', path]], request),
            ([['A', path], ['M', 'code.c']], request),
            ([['A', path]], dict(request, source_commit='other')),
            ([['A', path]], dict(request, mode='kernel_and_rt_diag'))):
            with self.assertRaises(ValueError):
                resolve(changes, 'abc', body)
