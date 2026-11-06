"""Convert WN-LMF via wn to the read-only Ruigo database. See README.md."""
import argparse
from contextlib import closing
import hashlib
import json
import gzip
import urllib.request
import unicodedata
import sqlite3
from pathlib import Path

import wn
from wn import lmf

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = """
PRAGMA foreign_keys=ON;
PRAGMA user_version=3;
CREATE TABLE metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL);
CREATE TABLE sources(id TEXT PRIMARY KEY,version TEXT NOT NULL,language TEXT NOT NULL,url TEXT,license TEXT,reading_mode TEXT NOT NULL);
CREATE TABLE synsets(id TEXT PRIMARY KEY,source TEXT NOT NULL REFERENCES sources(id),original_id TEXT NOT NULL,pos TEXT,ili TEXT);
CREATE TABLE lemmas(id INTEGER PRIMARY KEY,source TEXT NOT NULL REFERENCES sources(id),original_id TEXT NOT NULL,word TEXT NOT NULL,language TEXT NOT NULL,pos TEXT,forms_json TEXT NOT NULL,search_key TEXT NOT NULL,UNIQUE(source,original_id));
CREATE INDEX idx_lemmas_language_word ON lemmas(language,word);
CREATE INDEX idx_lemmas_language_key ON lemmas(language,search_key);
CREATE TABLE lemma_aliases(lemma_id INTEGER NOT NULL REFERENCES lemmas(id),language TEXT NOT NULL,word TEXT NOT NULL,PRIMARY KEY(lemma_id,word));
CREATE INDEX idx_aliases_language_word ON lemma_aliases(language,word);
CREATE TABLE synset_definitions(synset_id TEXT NOT NULL REFERENCES synsets(id),language TEXT NOT NULL,definition TEXT NOT NULL,PRIMARY KEY(synset_id,language));
CREATE TABLE senses(source TEXT NOT NULL REFERENCES sources(id),original_id TEXT NOT NULL,synset_id TEXT NOT NULL REFERENCES synsets(id),lemma_id INTEGER NOT NULL REFERENCES lemmas(id),PRIMARY KEY(source,original_id));
CREATE INDEX idx_senses_lemma ON senses(lemma_id);
CREATE INDEX idx_senses_synset ON senses(synset_id);
"""


EXPECTED = {
    "wnja:2.1": {"lemmas": 90706, "synsets": 67509, "senses": 148703, "synset_definitions": 67064},
    "oewn:2025": {"lemmas": 135969, "synsets": 107519, "senses": 185129, "synset_definitions": 107519},
}
ENGLISH_URL = "https://en-word.net/static/english-wordnet-2025.xml.gz"


# Pinned to the supplied Japanese XML and the English XML downloaded in this
# environment, not an independently verified publisher checksum.
SOURCE_SHA256 = {
    "wnja:2.1": "0e08760cd3fd82ac583b846d210a7db25282378af28e8d6c1612aaac51139919",
    "oewn:2025": "6f49adeec174ab3092169fb25cf4a925226b63975a5d29a691a5dff88f0673b2",
}


def verify_source(path, source_id):
    actual = hashlib.sha256(path.read_bytes()).hexdigest()
    if actual != SOURCE_SHA256[source_id]:
        raise ValueError(f"SHA-256 mismatch for {source_id}: {path} ({actual})")


def search_key(word, language):
    if language != "en":
        return word
    # Fold full-width Latin letters only; do not change punctuation or inner spaces.
    word = "".join(chr(ord(c) - 0xfee0) if "Ａ" <= c <= "Ｚ" or "ａ" <= c <= "ｚ" else c for c in word)
    return unicodedata.normalize("NFC", word).lower()


def validate(path):
    with closing(sqlite3.connect(path.resolve().as_uri() + "?mode=ro", uri=True)) as db:
        if db.execute("PRAGMA user_version").fetchone()[0] != 3:
            raise ValueError("Dictionary schema is old; regenerate the bilingual DB")
        if db.execute("PRAGMA integrity_check").fetchone() != ("ok",):
            raise ValueError("SQLite integrity check failed")
        if db.execute("PRAGMA foreign_key_check").fetchall():
            raise ValueError("Broken dictionary references")
        if set(r[0] for r in db.execute("SELECT id FROM sources")) != set(EXPECTED):
            raise ValueError("Expected both wnja:2.1 and oewn:2025")
        result = {}
        for source, expected in EXPECTED.items():
            counts = {table: db.execute(f"SELECT count(*) FROM {table} WHERE source=?", (source,)).fetchone()[0]
                      for table in ("lemmas", "synsets", "senses")}
            counts["synset_definitions"] = db.execute(
                "SELECT count(*) FROM synset_definitions d JOIN synsets s ON s.id=d.synset_id WHERE s.source=?", (source,)).fetchone()[0]
            if counts != expected:
                raise ValueError(f"Unexpected counts for {source}: {counts}")
            result[source] = counts
        if db.execute("SELECT count(*) FROM lemmas l JOIN sources s ON s.id=l.source WHERE l.language<>s.language").fetchone()[0]:
            raise ValueError("Lemma language mismatch")
        if db.execute("SELECT count(*) FROM synset_definitions d JOIN synsets s ON s.id=d.synset_id JOIN sources src ON src.id=s.source WHERE d.language<>src.language").fetchone()[0]:
            raise ValueError("Definition language mismatch")
        actual_aliases = set(db.execute("SELECT lemma_id,language,word FROM lemma_aliases"))
        expected_aliases = set()
        for number, language, word, raw in db.execute("SELECT id,language,word,forms_json FROM lemmas"):
            entry = {"lemma": {"writtenForm": word}, "forms": json.loads(raw)}
            expected_aliases.update((number, language, alias) for alias in spelling_aliases(entry, language))
        if actual_aliases != expected_aliases:
            raise ValueError("Spelling aliases do not match the dictionary forms")
        return result


def spelling_aliases(entry, language):
    # wnja forms without a script tag are orthographic variants. hira/kana
    # are readings. OEWN forms include inflections, which remain out of scope.
    if language != "ja":
        return []
    return sorted({f["writtenForm"] for f in entry.get("forms", [])
                   if not f.get("script") and f.get("writtenForm", "").strip()
                   and f["writtenForm"] != entry["lemma"]["writtenForm"]})


def import_lexicon(db, source, expected_id, language, reading_mode):
    resource = lmf.load(source, progress_handler=None)
    lexicons = resource["lexicons"]
    if len(lexicons) != 1:
        raise ValueError("Expected one lexicon per package")
    lexicon = lexicons[0]
    source_id = lexicon["id"] + ":" + lexicon["version"]
    if source_id != expected_id or lexicon["language"] != language or lexicon.get("extends"):
        raise ValueError(f"Unexpected or unresolved lexicon: {source_id}")
    def sid(original):
        return source_id + ":" + original
    db.execute("INSERT INTO sources VALUES (?,?,?,?,?,?)", (source_id, lexicon["version"], language, lexicon["url"], lexicon["license"], reading_mode))
    db.executemany("INSERT INTO metadata VALUES (?,?)", [
        (source_id + ":source_sha256", hashlib.sha256(source.read_bytes()).hexdigest()),
        (source_id + ":lmf_version", resource["lmf_version"])])
    for synset in lexicon["synsets"]:
        db.execute("INSERT INTO synsets VALUES (?,?,?,?,?)", (sid(synset["id"]), source_id, synset["id"], synset.get("partOfSpeech"), synset.get("ili")))
        texts = [d["text"].strip() for d in synset.get("definitions", [])
                 if d.get("language", language) == language and d["text"].strip()]
        if texts:
            db.execute("INSERT INTO synset_definitions VALUES (?,?,?)", (sid(synset["id"]), language, "\n".join(dict.fromkeys(texts))))
    for entry in lexicon["entries"]:
        lemma = entry["lemma"]
        word = lemma["writtenForm"]
        number = db.execute("INSERT INTO lemmas(source,original_id,word,language,pos,forms_json,search_key) VALUES (?,?,?,?,?,?,?)",
            (source_id, entry["id"], word, language, lemma["partOfSpeech"], json.dumps(entry.get("forms", []), ensure_ascii=False), search_key(word, language))).lastrowid
        db.executemany("INSERT INTO lemma_aliases VALUES (?,?,?)",
                       [(number, language, alias) for alias in spelling_aliases(entry, language)])
        for sense in entry.get("senses", []):
            db.execute("INSERT INTO senses VALUES (?,?,?,?)", (source_id, sense["id"], sid(sense["synset"]), number))


def build(source, english_source, output):
    verify_source(source, "wnja:2.1")
    verify_source(english_source, "oewn:2025")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".sqlite.tmp")
    if temporary.exists():
        temporary.unlink()
    try:
        with closing(sqlite3.connect(temporary)) as db, db:
            db.executescript(SCHEMA)
            db.executemany("INSERT INTO metadata VALUES (?,?)", [("wn_version", wn.__version__), ("build_dependency", "omw-en:2.0")])
            import_lexicon(db, source, "wnja:2.1", "ja", "kana")
            import_lexicon(db, english_source, "oewn:2025", "en", "none")
            db.execute("ANALYZE")
        counts = validate(temporary)
        temporary.replace(output)
        print(json.dumps(counts))
    finally:
        if temporary.exists():
            temporary.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "wnja-2.1/wnja/wnja-2.1.xml")
    parser.add_argument("--english-source", type=Path, default=ROOT / ".wn/english-wordnet-2025.xml")
    parser.add_argument("--output", type=Path, default=ROOT / "data/dictionary.sqlite")
    parser.add_argument("--wn-data", type=Path, default=ROOT / ".wn")
    parser.add_argument("--download-dependencies", action="store_true")
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    if args.verify_only:
        print(json.dumps(validate(args.output)))
        return
    wn.config.data_directory = args.wn_data.resolve()
    if args.download_dependencies:
        if not wn.lexicons(lexicon="omw-en:2.0"):
            wn.download("omw-en:2.0", progress_handler=None)
        if not args.english_source.exists():
            args.english_source.parent.mkdir(parents=True, exist_ok=True)
            with urllib.request.urlopen(ENGLISH_URL, timeout=60) as response:
                data = gzip.decompress(response.read())
            temporary = args.english_source.with_suffix(".download")
            temporary.write_bytes(data)
            temporary.replace(args.english_source)
    if not wn.lexicons(lexicon="omw-en:2.0"):
        parser.error("Prepare omw-en:2.0 with --download-dependencies (network required once)")
    if not args.english_source.is_file():
        parser.error("Prepare OEWN 2025 with --download-dependencies or --english-source PATH")
    build(args.source, args.english_source, args.output)


if __name__ == "__main__":
    main()
