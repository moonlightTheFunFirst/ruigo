import gzip
import json
from pathlib import Path
import sqlite3
import tempfile
import unittest
from contextlib import closing
from unittest.mock import patch

from tools import import_kaikki


class KaikkiImportTest(unittest.TestCase):
    def test_spelling_references_are_matched_per_sense(self):
        with closing(sqlite3.connect(":memory:")) as db:
            db.executescript(import_kaikki.SCHEMA)
            forms = json.dumps([{"form": word, "tags": [tag]} for word, tag in [
                ("はし", "transliteration"), ("キョウ", "transliteration"), ("べつ", "kanji")]])
            db.executemany("INSERT INTO entries VALUES (?,?,?,'ja','noun',?)", [
                (1, 1, "箸", forms), (2, 2, "はし", "[]"), (3, 3, "はし", "[]"),
                (4, 4, "キョウ", "[]"), (5, 5, "橋", forms), (6, 6, "別", forms),
                (7, 7, "べつ", "[]")])
            db.executemany("INSERT INTO senses VALUES (?,?,?,?,'ja')", [
                (1, 1, 1, json.dumps(["「はし」の漢字表記。"], ensure_ascii=False)),
                (2, 1, 2, json.dumps(["べつの漢字表記"], ensure_ascii=False)),
                (3, 5, 1, json.dumps(["川を渡る構造物。はしの漢字表記。"], ensure_ascii=False)),
                (4, 6, 1, json.dumps(["キョウの漢字表記。"], ensure_ascii=False))])
            from collections import Counter
            stats = Counter()
            import_kaikki.build_references(db, stats)
            self.assertEqual(db.execute("SELECT * FROM spelling_references ORDER BY 1,2").fetchall(),
                             [(1, 2), (1, 3), (4, 4)])
            self.assertEqual(stats["reference_mismatch_senses"], 1)
            self.assertEqual((stats["reference_single"], stats["reference_multiple"],
                              stats["reference_unresolved"]), (1, 1, 1))

    def test_quality_and_scope(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source, output, wordnet = (root / name for name in ("input.jsonl.gz", "kaikki.sqlite", "wordnet.sqlite"))
            records = [
                {"word": "bank", "lang_code": "en", "pos": "noun",
                 "senses": [{"glosses": ["銀行"]}, {"glosses": ["川岸"], "synonyms": [
                     {"word": "riverside", "lang_code": "en"}]}],
                 "translations": [{"word": "銀行", "lang_code": "ja", "sense": "銀行"},
                                  {"word": "banque", "lang_code": "fr"},
                                  {"word": "unknown"}],
                 "related": [{"word": "ambiguous"}]},
                {"word": "bank", "lang_code": "en", "pos": "noun", "senses": [{"glosses": ["別項目"]}]},
                {"word": "銀行", "lang_code": "ja", "forms": [{"form": "ぎんこう"}]},
                {"word": "banque", "lang_code": "fr"},
            ]
            data = ("\n".join(json.dumps(row, ensure_ascii=False) for row in records) + "\n{\n").encode("utf-8")
            source.write_bytes(gzip.compress(data))
            plain = root / "input.jsonl"
            plain.write_bytes(data)
            digest = import_kaikki.source_hash(source)
            self.assertEqual(import_kaikki.source_hash(plain), digest)
            with closing(sqlite3.connect(wordnet)) as db, db:
                db.execute("CREATE TABLE lemmas(language,word)")
                db.execute("INSERT INTO lemmas VALUES ('en','bank')")

            output.write_bytes(b"existing")
            with patch.object(import_kaikki, "EXPECTED_SHA256", "bad"):
                with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                    import_kaikki.build(source, output, wordnet)
                evaluation = import_kaikki.build(source, output, wordnet, evaluate=True,
                                                 candidate_url="fixture", dump_date="2026-11-01")
            self.assertEqual(output.read_bytes(), b"existing")
            self.assertEqual(evaluation["skipped"]["en.related.unknown_language"], 1)
            self.assertEqual(evaluation["skipped"]["en.translation.other_language"], 1)
            self.assertEqual(evaluation["skipped"]["en.translation.unknown_language"], 1)
            self.assertEqual(evaluation["skipped"]["invalid_json"], 1)
            english = evaluation["languages"]["en"]
            self.assertEqual((english["entries"], english["headwords"], english["senses"]), (2, 1, 3))
            self.assertEqual(english["headwords_absent_from_wordnet"], 0)
            self.assertEqual(english["relations"]["synonym"]["sense_linked"], 1)
            with patch.object(import_kaikki, "EXPECTED_SHA256", digest):
                import_kaikki.build(source, output, wordnet)
            with closing(sqlite3.connect(output)) as db:
                self.assertEqual(db.execute("SELECT target_word,sense_id,source_label FROM relations ORDER BY id")
                                 .fetchall(), [("銀行", None, "銀行"), ("riverside", 2, None)])
                self.assertEqual(db.execute("SELECT DISTINCT gloss_language FROM senses").fetchall(), [("ja",)])
                self.assertEqual(db.execute("SELECT value FROM metadata WHERE key='sha256'").fetchone(), (digest,))
