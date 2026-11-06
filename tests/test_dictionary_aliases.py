"""Run with .venv/Scripts/python.exe -m unittest discover -s tests -p test_dictionary_aliases.py."""
import importlib.util
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "dictionary_builder", Path(__file__).resolve().parents[1] / "tools/build_dictionary.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class SpellingAliases(unittest.TestCase):
    def test_readings_are_excluded(self):
        entry = {"lemma": {"writtenForm": "哀しい"}, "forms": [
            {"writtenForm": "悲しい"},
            {"writtenForm": "かなしい", "script": "hira"},
            {"writtenForm": "カナシイ", "script": "kana"},
        ]}
        self.assertEqual(builder.spelling_aliases(entry, "ja"), ["悲しい"])

    def test_deduplication_and_literal_kana_spelling(self):
        entry = {"lemma": {"writtenForm": "見出し"}, "forms": [
            {"writtenForm": "見出し"}, {"writtenForm": ""},
            {"writtenForm": "別表記"}, {"writtenForm": "別表記"},
            {"writtenForm": "かなの別表記"},
        ]}
        self.assertEqual(builder.spelling_aliases(entry, "ja"), ["かなの別表記", "別表記"])

    def test_english_inflections_remain_excluded(self):
        entry = {"lemma": {"writtenForm": "abacus"}, "forms": [{"writtenForm": "abaci"}]}
        self.assertEqual(builder.spelling_aliases(entry, "en"), [])


class LemmaAliases(unittest.TestCase):
    def test_japanese_readings_and_spellings_are_separate_kinds(self):
        entry = {"lemma": {"writtenForm": "哀しい"}, "forms": [
            {"writtenForm": "悲しい"},
            {"writtenForm": "かなしい", "script": "hira"},
            {"writtenForm": "カナシイ", "script": "kana"},
            {"writtenForm": "哀しい", "script": "hira"},
        ]}
        self.assertEqual(builder.lemma_aliases(entry, "ja"),
                         [("かなしい", "reading"), ("カナシイ", "reading"), ("悲しい", "spelling")])

    def test_english_forms_are_inflections(self):
        entry = {"lemma": {"writtenForm": "go"}, "forms": [{"writtenForm": "went"}, {"writtenForm": "gone"}]}
        self.assertEqual(builder.lemma_aliases(entry, "en"), [("gone", "inflection"), ("went", "inflection")])


if __name__ == "__main__":
    unittest.main()
