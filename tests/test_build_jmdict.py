from contextlib import closing
from pathlib import Path
import sqlite3
import tempfile
import unittest
from unittest.mock import patch

from tools import build_jmdict


XML = """<JMdict><entry><ent_seq>1</ent_seq>
<k_ele><keb>表</keb></k_ele><k_ele><keb>裏</keb></k_ele>
<r_ele><reb>おもて</reb><re_restr>表</re_restr></r_ele>
<r_ele><reb>うら</reb><re_restr>裏</re_restr></r_ele>
<r_ele><reb>かな</reb><re_nokanji/></r_ele>
<sense><pos>verb</pos><stagk>表</stagk><gloss>to Run</gloss><gloss xml:lang="fre">courir</gloss>
<example><ex_srce exsrc_type="tat">42</ex_srce><ex_sent xml:lang="jpn">例文。</ex_sent>
<ex_sent xml:lang="eng">Example.</ex_sent></example></sense>
<sense><stagr>うら</stagr><gloss>back</gloss></sense>
<sense><stagr>かな</stagr><gloss>kana</gloss></sense>
<sense><pos>adverb (fukushi)</pos><stagr>うら</stagr><gloss>to date</gloss></sense>
</entry></JMdict>"""


class JmdictBuildTest(unittest.TestCase):
    def test_import_restrictions_and_keys(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "source.xml"
            output = Path(folder) / "jmdict.sqlite"
            source.write_text(XML, encoding="utf-8")
            build_jmdict.build(source, output)
            with closing(sqlite3.connect(output)) as db:
                self.assertEqual(build_jmdict.verify(db), {"senses": 4, "forms": 4, "glosses": 4})
                self.assertEqual(db.execute("""
                    SELECT s.position,s.pos,f.word,f.reading,g.text,g.folded,g.bare_verb
                    FROM senses s JOIN forms f ON f.sense=s.id JOIN glosses g ON g.sense=s.id
                    ORDER BY s.position
                """).fetchall(), [
                    (1, "verb", "表", "おもて", "to Run", "to run", "run"),
                    (2, "verb", "裏", "うら", "back", "back", None),
                    (3, "verb", "かな", "かな", "kana", "kana", None),
                    (4, "adverb (fukushi)", "裏", "うら", "to date", "to date", None),
                ])
                self.assertEqual(db.execute("SELECT * FROM sense_examples").fetchall(),
                                 [(1, 0, "例文。", "Example.", "https://tatoeba.org/sentences/show/42")])
                self.assertEqual(build_jmdict.bare_verb("to study", "noun or participle which takes the aux. verb suru"),
                                 "study")
                self.assertIsNone(build_jmdict.bare_verb("to date", "expressions (phrases, clauses, etc.)"))
                db.execute("PRAGMA user_version=2")
                with self.assertRaisesRegex(ValueError, "version"):
                    build_jmdict.verify(db)

    def test_replacement_retries_and_keeps_existing_output_on_failure(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "source.xml"
            output = Path(folder) / "jmdict.sqlite"
            source.write_text(XML, encoding="utf-8")
            output.write_bytes(b"original")
            with patch.object(Path, "replace", side_effect=PermissionError("locked")) as replace, \
                    patch.object(build_jmdict.time, "sleep"):
                with self.assertRaisesRegex(PermissionError, "locked"):
                    build_jmdict.build(source, output)
                self.assertEqual(replace.call_count, 20)
            self.assertEqual(output.read_bytes(), b"original")
            self.assertEqual(list(Path(folder).glob("*.tmp")), [])
