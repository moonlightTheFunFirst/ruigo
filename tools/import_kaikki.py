"""Build or evaluate a standalone SQLite dictionary from Japanese Kaikki JSONL."""

import argparse
from collections import Counter
from contextlib import closing
import gzip
import hashlib
import json
from pathlib import Path
import sqlite3
import sys
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[1]
SOURCE = {
    "url": "https://kaikki.org/jawiktionary/raw-wiktextract-data.jsonl.gz",
    "dump_date": "2026-11-01",
    "extraction_date": "2026-11-02",
    "extraction_commits": "19f9a30,6f96ddf",
    "edition": "jawiktionary",
}
EXPECTED_SHA256 = "491dbaf5fbf7c34cf4d033cdac59776627287eb5072799e7581ecef495717edd"
LANGUAGES = ("ja", "en")
KINDS = {"translations": "translation", "synonyms": "synonym",
         "antonyms": "antonym", "related": "related"}
SCHEMA = """
PRAGMA user_version=1;
PRAGMA foreign_keys=ON;
CREATE TABLE metadata(key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE entries(
    id INTEGER PRIMARY KEY, source_line INTEGER NOT NULL UNIQUE,
    word TEXT NOT NULL, language TEXT NOT NULL CHECK(language IN ('ja','en')),
    pos TEXT, forms_json TEXT NOT NULL
);
CREATE TABLE senses(
    id INTEGER PRIMARY KEY, entry_id INTEGER NOT NULL REFERENCES entries(id),
    source_position INTEGER NOT NULL, glosses_json TEXT NOT NULL,
    gloss_language TEXT NOT NULL,
    UNIQUE(entry_id, source_position), UNIQUE(entry_id, id)
);
CREATE TABLE relations(
    id INTEGER PRIMARY KEY, entry_id INTEGER NOT NULL REFERENCES entries(id),
    sense_id INTEGER, kind TEXT NOT NULL CHECK(kind IN ('translation','synonym','antonym','related')),
    target_word TEXT NOT NULL, target_language TEXT NOT NULL CHECK(target_language IN ('ja','en')),
    source_label TEXT,
    FOREIGN KEY(entry_id, sense_id) REFERENCES senses(entry_id, id)
);
CREATE INDEX entries_lookup ON entries(language, word);
CREATE INDEX relations_entry ON relations(entry_id, kind);
CREATE INDEX relations_sense ON relations(sense_id, kind);
CREATE INDEX relations_target ON relations(target_language, target_word);
"""


def open_source(path):
    return gzip.open(path, "rb") if path.suffix.lower() == ".gz" else path.open("rb")


def source_hash(path):
    with open_source(path) as stream:
        digest = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
        return digest.hexdigest()


def nonempty(value):
    return value if isinstance(value, str) and value.strip() else None


def array(record, key, stats):
    value = record.get(key, [])
    if isinstance(value, list):
        return value
    stats["invalid_" + key] += 1
    return []


def insert_relations(db, record, entry_id, sense_id, language, stats):
    for key, kind in KINDS.items():
        for relation in array(record, key, stats):
            if not isinstance(relation, dict) or not nonempty(relation.get("word")):
                stats[f"{language}.{kind}.invalid"] += 1
                continue
            # The ja extractor's Linkage drops language information, even for
            # templates that may link to another language. The containing entry
            # alone cannot establish the target language.
            target_language = nonempty(relation.get("lang_code"))
            if target_language is None:
                stats[f"{language}.{kind}.unknown_language"] += 1
                continue
            if target_language not in LANGUAGES:
                stats[f"{language}.{kind}.other_language"] += 1
                continue
            db.execute(
                """INSERT INTO relations(entry_id,sense_id,kind,target_word,target_language,source_label)
                   VALUES (?,?,?,?,?,?)""",
                (entry_id, sense_id, kind, relation["word"], target_language, nonempty(relation.get("sense"))),
            )


def ingest(db, source, expected_hash):
    stats = Counter()
    digest = hashlib.sha256()
    with open_source(source) as stream:
        for line_number, line in enumerate(stream, 1):
            digest.update(line)
            try:
                record = json.loads(line.decode("utf-8"))
            except (ValueError, RecursionError):
                stats["invalid_json"] += 1
                if stats["invalid_json"] <= 10:
                    print(f"Invalid JSON at line {line_number}", file=sys.stderr)
                continue
            if isinstance(record, dict) and record.get("pos") == "hard-redirect":
                stats["redirects"] += 1
                continue
            if not isinstance(record, dict) or not nonempty(record.get("lang_code")):
                stats["invalid_entry"] += 1
                continue
            language = record["lang_code"]
            if language not in LANGUAGES:
                stats["other_language_entries"] += 1
                continue
            word = nonempty(record.get("word"))
            if word is None:
                stats["missing_word"] += 1
                continue
            forms = array(record, "forms", stats)
            entry_id = db.execute(
                "INSERT INTO entries(source_line,word,language,pos,forms_json) VALUES (?,?,?,?,?)",
                (line_number, word, language, nonempty(record.get("pos")), json.dumps(forms, ensure_ascii=False)),
            ).lastrowid
            insert_relations(db, record, entry_id, None, language, stats)
            for position, sense in enumerate(array(record, "senses", stats), 1):
                if not isinstance(sense, dict):
                    stats["invalid_sense"] += 1
                    continue
                glosses = [gloss for gloss in array(sense, "glosses", stats) if nonempty(gloss)]
                sense_id = db.execute(
                    "INSERT INTO senses(entry_id,source_position,glosses_json,gloss_language) VALUES (?,?,?,?)",
                    (entry_id, position, json.dumps(glosses, ensure_ascii=False), "ja"),
                ).lastrowid
                insert_relations(db, sense, entry_id, sense_id, language, stats)
    if digest.hexdigest() != expected_hash:
        raise ValueError("Input changed during conversion")
    return stats


def quality_report(db):
    report = {}
    for language in LANGUAGES:
        entries, headwords = db.execute(
            "SELECT count(*), count(DISTINCT word) FROM entries WHERE language=?", (language,)
        ).fetchone()
        senses = db.execute(
            "SELECT count(*) FROM senses s JOIN entries e ON e.id=s.entry_id WHERE e.language=?", (language,)
        ).fetchone()[0]
        missing = db.execute(
            """SELECT count(DISTINCT e.word) FROM entries e WHERE e.language=?
               AND NOT EXISTS (SELECT 1 FROM wordnet.lemmas l WHERE l.language=e.language AND l.word=e.word)""",
            (language,),
        ).fetchone()[0]
        relations = {}
        for kind in KINDS.values():
            total, scoped, covered = db.execute(
                """SELECT count(*),count(r.sense_id),count(DISTINCT e.word)
                   FROM relations r JOIN entries e ON e.id=r.entry_id WHERE e.language=? AND r.kind=?""",
                (language, kind),
            ).fetchone()
            relations[kind] = {
                "count": total, "sense_linked": scoped, "sense_linked_ratio": scoped / total if total else None,
                "headwords": covered, "headword_ratio": covered / headwords if headwords else None,
            }
        report[language] = {"entries": entries, "headwords": headwords, "senses": senses,
                            "headwords_absent_from_wordnet": missing, "relations": relations}
    return report


def build(source, output, wordnet, *, evaluate=False, candidate_url=None, dump_date=None):
    digest = source_hash(source)
    if not evaluate and digest != EXPECTED_SHA256:
        raise ValueError(f"Kaikki SHA-256 mismatch: {digest}; use the pinned snapshot or evaluate the candidate")
    if not evaluate and output.resolve() in (source.resolve(), wordnet.resolve()):
        raise ValueError("Kaikki output must be separate from both inputs")
    provenance = ({"url": candidate_url, "dump_date": dump_date, "edition": "jawiktionary"}
                  if evaluate else SOURCE)
    # Generate beside the output so publication is an atomic replace. Evaluation
    # uses the same code path and discards its database.
    with TemporaryDirectory(prefix="kaikki-", dir=output.parent) as folder:
        temporary = Path(folder) / "kaikki.sqlite"
        with closing(sqlite3.connect(temporary, uri=True)) as db:
            db.execute("ATTACH DATABASE ? AS wordnet", (wordnet.resolve().as_uri() + "?mode=ro",))
            db.executescript(SCHEMA)
            stats = ingest(db, source, digest)
            report = quality_report(db)
            if any(not report[language]["headwords"] for language in LANGUAGES):
                raise ValueError("Kaikki must contain both Japanese and English headwords")
            db.executemany("INSERT INTO metadata VALUES (?,?)",
                           {**provenance, "sha256": digest, "provider": "Kaikki / Wiktionary"}.items())
            db.commit()
            if (db.execute("PRAGMA main.integrity_check").fetchone() != ("ok",)
                    or db.execute("PRAGMA main.foreign_key_check").fetchone()):
                raise ValueError("Kaikki SQLite integrity check failed")
        result = {"source": provenance, "sha256": digest, "database_bytes": temporary.stat().st_size,
                  "languages": report, "skipped": dict(stats)}
        if not evaluate:
            temporary.replace(output)
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Japanese Kaikki JSONL or JSONL.gz")
    parser.add_argument("--output", type=Path, default=ROOT / "data" / "kaikki.sqlite")
    parser.add_argument("--wordnet", type=Path, default=ROOT / "data" / "dictionary.sqlite")
    parser.add_argument("--evaluate-candidate", action="store_true", help="Report quality using a disposable DB")
    parser.add_argument("--source-url", help="Download URL for a candidate")
    parser.add_argument("--dump-date", help="Wiktionary dump date for a candidate")
    args = parser.parse_args()
    if args.evaluate_candidate and not (args.source_url and args.dump_date):
        parser.error("--evaluate-candidate requires --source-url and --dump-date")
    try:
        report = build(args.source, args.output, args.wordnet, evaluate=args.evaluate_candidate,
                       candidate_url=args.source_url, dump_date=args.dump_date)
    except (OSError, EOFError, ValueError, sqlite3.Error) as error:
        parser.exit(1, f"{error}\n")
    print(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
