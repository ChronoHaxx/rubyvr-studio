"""Original native-path fixtures: no compiler, game assets, GL or execution."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from dynamic_inventory import inventory

spec = importlib.util.spec_from_file_location('fixture', Path(__file__).with_name('test-dynamic-inventory.py'))
fixture = importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
ledger, write = fixture.ledger, fixture.write


class NativeTraceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        fixture.fixture(self.root)
        write(self.root,'data/event_scripts.s','.include "data/specials.inc"\n')
        write(self.root,'data/specials.inc','.macro def_special ptr\n.endm\ngSpecials::\n def_special Root\n')
        write(self.root,'data/maps/SampleA/scripts.inc','SampleA_Enter::\n special Root\n end\n')
        write(self.root,'src/trace.c','void Root(void) { Helper(); }\nstatic void Helper(void) { SetWeather(4); }\n')

    def tearDown(self):
        self.temp.cleanup()

    def data(self):
        return inventory(self.root)

    def trace(self, symbol='Root', data=None):
        return next(r['detail'] for r in (data or self.data())['records'] if r['id']=='state:native_trace:'+symbol)

    def test_direct_helpers_and_source_association(self):
        d=self.data();t=self.trace(data=d)
        self.assertEqual(t['related_maps'],['MAP_SAMPLE_A'])
        self.assertEqual(t['mutations'][0]['callee'],'SetWeather')
        self.assertEqual([p['function'] for p in t['mutations'][0]['witness']],['Root','Helper'])
        self.assertEqual(t['mutations'][0]['witness'][1]['call'],dict(path='src/trace.c',line=1))
        r=next(r for r in d['records'] if r['category']=='script_native_call')
        self.assertEqual(r['detail']['dispatch_resolution'],'registered_special')
        self.assertEqual(r['detail']['native_trace_id'],'state:native_trace:Root')
        db=ledger.open_db(self.root/'report.sqlite')
        try:
            ledger.sync_rows(db,ledger.dynamic_entries(d),{})
            report=ledger.markdown(ledger.report(db,'MAP_SAMPLE_A','native_mutation_trace'))
            self.assertIn('Root -> Helper -> SetWeather',report)
            self.assertIn('Native mutation witnesses',report)
            self.assertIn('remain unverified',report)
        finally:
            db.close()

    def test_local_static_binding_does_not_cross_translation_units(self):
        write(self.root,'src/other.c','static void Helper(void) { SetWeather(99); }\n')
        t=self.trace();self.assertEqual(len(t['mutations']),1)
        self.assertEqual(t['mutations'][0]['arguments'],'4')
        write(self.root,'src/trace.c','void Root(void) { Helper(); }\n')
        t=self.trace();self.assertFalse(t['mutations'])
        self.assertEqual(t['frontiers'][0]['kind'],'missing_body')

    def test_conditional_definitions_and_cycles_are_retained(self):
        write(self.root,'src/trace.c','''void Root(void) { Variant(); }
#if A
void Variant(void) { Root(); SetWeather(1); }
#else
void Variant(void) { Root(); SetWeather(2); }
#endif
''')
        t=self.trace();self.assertEqual(t['functions_visited'],3)
        self.assertEqual({m['arguments'] for m in t['mutations']},{'1','2'})
        self.assertTrue(all(m['witness'][1]['ambiguous'] for m in t['mutations']))

    def test_literal_task_callback_is_marked_deferred(self):
        write(self.root,'src/trace.c','''void Root(void) { CreateTask(TaskDoor, 8); }
static void TaskDoor(int id) { MapGridSetMetatileIdAt(7, 9, 123); }
int CreateTask(TaskFunc callback, int priority) { callback(0); return 0; }
''')
        t=self.trace();self.assertEqual(t['mutations'][0]['callee'],'MapGridSetMetatileIdAt')
        self.assertEqual(t['mutations'][0]['witness'][1]['via'],'registered_callback_candidate')
        self.assertTrue(any(e['target']=='callback' and e['kind']=='local_or_pointer_call' for e in t['frontiers']))

    def test_macros_pointer_tables_and_unknown_callback_are_frontiers(self):
        write(self.root,'include/macros.h','#define HiddenChange() SetWeather(8)\n')
        write(self.root,'src/trace.c','''void Root(TaskFunc Helper) {
  Helper(); HiddenChange(); callbacks[index](0); object->callback(0); object.callback = Later;
  CreateTask(dynamicCallback, 8); SetMainCallback2(NULL);
}
void Helper(void) { SetWeather(99); }
''')
        t=self.trace();self.assertFalse(t['mutations'])
        kinds={e['kind'] for e in t['frontiers']}
        self.assertTrue({'local_or_pointer_call','unexpanded_macro','indirect_call','unresolved_callback','callback_assignment'}<=kinds)
        self.assertFalse(any(e['target']=='NULL' for e in t['frontiers']))
        write(self.root,'src/trace.c','void Root(TaskFunc SetWeather) { SetWeather(9); }\n')
        t=self.trace();self.assertFalse(t['mutations'])
        self.assertTrue(any(e['target']=='SetWeather' and e['kind']=='local_or_pointer_call' for e in t['frontiers']))

    def test_nested_call_inside_pointer_index_is_not_dropped(self):
        write(self.root,'src/trace.c','''void Root(void) { callbacks[SelectIndex()](0); }
int SelectIndex(void) { SetWeather(7); return 0; }
''')
        t=self.trace();self.assertEqual(t['mutations'][0]['arguments'],'7')
        self.assertTrue(any(e['kind']=='indirect_call' for e in t['frontiers']))

    def test_declarations_comments_strings_and_control_words_are_not_routes(self):
        write(self.root,'src/trace.c','''void Root(void) {
  TaskFunc Helper(void);
  void SetWeather(int value);
  const char *text = "Helper()"; // Helper();
  if (flag) { } else if (other) { }
}
void Helper(void) { SetWeather(99); }
''')
        t=self.trace();self.assertFalse(t['mutations']);self.assertEqual(t['functions_visited'],1)
        self.assertFalse(t['frontiers'])

    def test_unregistered_and_unavailable_script_targets_remain_explicit(self):
        write(self.root,'data/maps/SampleA/scripts.inc','SampleA_Enter::\n special NotRegistered\n callnative Missing\n end\n')
        p=self.root/'include/macros/event.inc';p.write_text(p.read_text()+'\n.macro callnative\n.endm\n')
        write(self.root,'src/trace.c','void NotRegistered(void) { SetWeather(99); }\n')
        d=self.data();calls=[r['detail'] for r in d['records'] if r['category']=='script_native_call']
        self.assertEqual(calls[0]['native_resolution'],'unresolved_reference')
        special=next(r for r in calls if r['command']=='special')
        self.assertNotIn('native_trace_id',special)
        self.assertEqual(special['dispatch_resolution'],'unregistered_special')
        self.assertEqual(self.trace('Missing',d)['resolution'],'missing_body')

    def test_whitespace_retains_identity_and_helpers_invalidate_only_dynamic_reviews(self):
        d=self.data();ids={r['id'] for r in d['records']}
        p=self.root/'src/trace.c';p.write_text('\n'+p.read_text())
        self.assertEqual(ids,{r['id'] for r in self.data()['records']})
        db=ledger.open_db(self.root/'ledger.sqlite')
        static=ledger.entry('original-static','family','Original fixture','source','recipe')
        try:
            ledger.sync_rows(db,[static]+ledger.dynamic_entries(self.data()),{})
            for id in ('original-static','state:native_trace:Root'):
                ledger.review(db,id,'visual','accepted','synthetic://fixture','Original synthetic review')
            p.write_text(p.read_text().replace('SetWeather(4)','SetWeather(5)'))
            ledger.sync_rows(db,[static]+ledger.dynamic_entries(self.data()),{})
            reviews={r['entry_id']:dict(r) for r in db.execute('SELECT * FROM reviews')}
            self.assertEqual(reviews['original-static']['invalidated'],'')
            self.assertIn('source dependency',reviews['state:native_trace:Root']['invalidated'])
            self.assertEqual(reviews['state:native_trace:Root']['result'],'accepted')
            row=db.execute("SELECT * FROM entries WHERE id='state:native_trace:Root'").fetchone()
            self.assertEqual((row['implementation'],row['disposition']),('pending_runtime','unresolved'))
            self.assertEqual(db.execute("SELECT count(*) FROM reviews WHERE stage!='visual'").fetchone()[0],0)
        finally:
            db.close()

    def test_changing_adapter_during_scan_is_rejected(self):
        with patch('dynamic_inventory.adapter_hash',side_effect=['before','changed']):
            with self.assertRaisesRegex(ValueError,'adapter changed'):
                self.data()


if __name__=='__main__':
    unittest.main(verbosity=2)
