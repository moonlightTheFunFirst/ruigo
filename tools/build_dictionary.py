"""Convert WN-LMF via wn to the read-only Ruigo database. See README.md."""
import argparse
from contextlib import closing
import hashlib
import json
import sqlite3
from pathlib import Path

import wn
from wn import lmf

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = """
PRAGMA foreign_keys=ON;
PRAGMA user_version=1;
CREATE TABLE metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL);
CREATE TABLE sources(id TEXT PRIMARY KEY,version TEXT NOT NULL,language TEXT NOT NULL,url TEXT,license TEXT);
CREATE TABLE synsets(id TEXT PRIMARY KEY,source TEXT NOT NULL REFERENCES sources(id),original_id TEXT NOT NULL,pos TEXT,ili TEXT);
CREATE TABLE lemmas(id INTEGER PRIMARY KEY,source TEXT NOT NULL REFERENCES sources(id),original_id TEXT NOT NULL,word TEXT NOT NULL,language TEXT NOT NULL,pos TEXT,forms_json TEXT NOT NULL,UNIQUE(source,original_id));
CREATE INDEX idx_lemmas_language_word ON lemmas(language,word);
CREATE TABLE synset_definitions(synset_id TEXT NOT NULL REFERENCES synsets(id),language TEXT NOT NULL,definition TEXT NOT NULL,PRIMARY KEY(synset_id,language));
CREATE TABLE senses(source TEXT NOT NULL REFERENCES sources(id),original_id TEXT NOT NULL,synset_id TEXT NOT NULL REFERENCES synsets(id),lemma_id INTEGER NOT NULL REFERENCES lemmas(id),PRIMARY KEY(source,original_id));
CREATE INDEX idx_senses_lemma ON senses(lemma_id);
CREATE INDEX idx_senses_synset ON senses(synset_id);
"""


def validate(path):
    with closing(sqlite3.connect(path.resolve().as_uri() + "?mode=ro", uri=True)) as db:
        if db.execute("PRAGMA integrity_check").fetchone() != ("ok",):
            raise ValueError("SQLite integrity check failed")
        if db.execute("PRAGMA foreign_key_check").fetchall():
            raise ValueError("Broken dictionary references")
        counts = {table: db.execute(f"SELECT count(*) FROM {table}").fetchone()[0]
                  for table in ("lemmas", "synsets", "senses", "synset_definitions")}
        if not all(counts.values()):
            raise ValueError("Empty dictionary")
        return counts


def build(source, output):
    resource = lmf.load(source, progress_handler=None)
    lexicons = resource["lexicons"]
    if len(lexicons) != 1 or (lexicons[0]["id"], lexicons[0]["version"], lexicons[0]["language"]) != ("wnja", "2.1", "ja"):
        raise ValueError("Expected the Japanese WordNet 2.1 package")
    lexicon = lexicons[0]
    # This release contains its own synsets. Refuse unresolved extension data;
    # never silently drop senses or substitute English definitions.
    if lexicon.get("extends"):
        raise ValueError("An extended lexicon needs explicit synset resolution")
    source_id = lexicon["id"] + ":" + lexicon["version"]
    def synset_id(original):
        return source_id + ":" + original

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".sqlite.tmp")
    if temporary.exists():
        temporary.unlink()
    try:
        with closing(sqlite3.connect(temporary)) as db, db:
            db.executescript(SCHEMA)
            db.execute("INSERT INTO sources VALUES (?,?,?,?,?)", (source_id, lexicon["version"], "ja", lexicon["url"], lexicon["license"]))
            db.executemany("INSERT INTO metadata VALUES (?,?)", [
                ("source_sha256", hashlib.sha256(source.read_bytes()).hexdigest()),
                ("wn_version", wn.__version__), ("build_dependency", "omw-en:2.0"),
                ("lmf_version", resource["lmf_version"])])
            for synset in lexicon["synsets"]:
                sid = synset_id(synset["id"])
                db.execute("INSERT INTO synsets VALUES (?,?,?,?,?)", (sid, source_id, synset["id"], synset.get("partOfSpeech"), synset.get("ili")))
                definitions = {}
                for definition in synset.get("definitions", []):
                    language = definition.get("language", "ja")
                    if language != "ja":
                        continue
                    text = definition["text"].strip()
                    if text:
                        definitions.setdefault(language, []).append(text)
                for language, texts in definitions.items():
                    db.execute("INSERT INTO synset_definitions VALUES (?,?,?)", (sid, language, "\n".join(dict.fromkeys(texts))))
            for number, entry in enumerate(lexicon["entries"], 1):
                lemma = entry["lemma"]
                db.execute("INSERT INTO lemmas VALUES (?,?,?,?,?,?,?)", (number, source_id, entry["id"], lemma["writtenForm"], "ja", lemma["partOfSpeech"], json.dumps(entry.get("forms", []), ensure_ascii=False)))
                for sense in entry.get("senses", []):
                    db.execute("INSERT INTO senses VALUES (?,?,?,?)", (source_id, sense["id"], synset_id(sense["synset"]), number))
        counts = validate(temporary)
        expected = {"lemmas": 90706, "synsets": 67509, "senses": 148703, "synset_definitions": 67064}
        if counts != expected:
            raise ValueError(f"Package counts differ from wnja 2.1 README: {counts}")
        temporary.replace(output)
        print(json.dumps(counts))
    finally:
        if temporary.exists():
            temporary.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "wnja-2.1/wnja/wnja-2.1.xml")
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
        wn.download("omw-en:2.0", progress_handler=None)
    if not wn.lexicons(lexicon="omw-en:2.0"):
        parser.error("Prepare omw-en:2.0 with --download-dependencies (network required once)")
    build(args.source, args.output)


if __name__ == "__main__":
    main()
