"""Build an offline SQLite dictionary from the official JMdict English XML."""
import argparse
from contextlib import closing
from datetime import datetime, timezone
import gzip
import hashlib
from pathlib import Path
import sqlite3
import tempfile
import time
import unicodedata
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
VERSION = 3


def replace_with_retry(source, target):
    # Match the Kaikki importer's retry window for Windows sharing violations.
    for attempt in range(20):
        try:
            source.replace(target)
            return
        except PermissionError:
            if attempt == 19:
                raise
            time.sleep(0.5)


def verify(db):
    if db.execute("PRAGMA user_version").fetchone() != (VERSION,):
        raise ValueError("Unsupported JMdict database version. Regenerate jmdict.sqlite.")
    if db.execute("PRAGMA integrity_check").fetchall() != [("ok",)]:
        raise ValueError("JMdict integrity check failed")
    if db.execute("PRAGMA foreign_key_check").fetchone():
        raise ValueError("JMdict database contains invalid references")
    db.execute("SELECT key,value FROM metadata LIMIT 0")
    db.execute("SELECT sense,position,japanese,english,source FROM sense_examples LIMIT 0")
    db.execute("""SELECT s.entry,s.position,s.pos,f.word,f.reading,g.text,g.exact,g.folded,g.bare_verb
                  FROM senses s JOIN forms f ON f.sense=s.id JOIN glosses g ON g.sense=s.id LIMIT 0""")
    counts = {table: db.execute(f"SELECT count(*) FROM {table}").fetchone()[0]
              for table in ("senses", "forms", "glosses")}
    if not all(counts.values()):
        raise ValueError("JMdict database is empty")
    return counts


def build(source, output):
    with source.open("rb") as raw:
        digest = hashlib.sha256()
        for block in iter(lambda: raw.read(1024 * 1024), b""):
            digest.update(block)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=output.parent, suffix=".tmp", delete=False) as file:
        temp = Path(file.name)
    try:
        with closing(sqlite3.connect(temp)) as db, db:
            db.executescript(f"""
                PRAGMA user_version={VERSION};
                PRAGMA foreign_keys=ON;
                CREATE TABLE metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL);
                CREATE TABLE senses(id INTEGER PRIMARY KEY,entry INTEGER NOT NULL,
                    position INTEGER NOT NULL,pos TEXT NOT NULL);
                CREATE TABLE sense_examples(sense INTEGER NOT NULL REFERENCES senses(id),
                    position INTEGER NOT NULL,japanese TEXT NOT NULL,english TEXT NOT NULL,
                    source TEXT NOT NULL,PRIMARY KEY(sense,position));
                CREATE TABLE forms(sense INTEGER NOT NULL REFERENCES senses(id),
                    word TEXT NOT NULL, reading TEXT NOT NULL, PRIMARY KEY(sense,word,reading));
                CREATE TABLE glosses(sense INTEGER NOT NULL REFERENCES senses(id),
                    position INTEGER NOT NULL, text TEXT NOT NULL, exact TEXT NOT NULL,
                    folded TEXT NOT NULL, bare_verb TEXT, PRIMARY KEY(sense,position));
            """)
            db.executemany("INSERT INTO metadata VALUES (?,?)", [
                ("source", "https://www.edrdg.org/jmdict/j_jmdict.html"),
                ("license", "CC BY-SA 4.0"), ("sha256", digest.hexdigest()),
                ("source_file_mtime_utc", datetime.fromtimestamp(source.stat().st_mtime, timezone.utc).isoformat()),
                ("built_utc", datetime.now(timezone.utc).isoformat()),
            ])
            with (gzip.open(source, "rb") if source.suffix == ".gz" else source.open("rb")) as stream:
                events = ET.iterparse(stream, events=("start", "end"))
                _, root = next(events)
                if root.tag != "JMdict":
                    raise ValueError("Expected JMdict XML")
                for event, entry in events:
                    if event == "end" and entry.tag == "entry":
                        insert_entry(db, entry)
                        root.remove(entry)
            db.executescript("""
                CREATE INDEX forms_word ON forms(word);
                CREATE INDEX forms_reading ON forms(reading);
                CREATE INDEX glosses_exact ON glosses(exact);
                CREATE INDEX glosses_folded ON glosses(folded);
                CREATE INDEX glosses_bare_verb ON glosses(bare_verb) WHERE bare_verb IS NOT NULL;
            """)
            count = verify(db)["senses"]
        replace_with_retry(temp, output)
        print(f"JMdict: {count:,} senses, {output.stat().st_size:,} bytes")
    finally:
        try:
            temp.unlink(missing_ok=True)
        except OSError:
            pass  # Preserve the original build or replacement error.


def insert_entry(db, entry):
    number = int(entry.findtext("ent_seq"))
    written = [element.text for element in entry.findall("k_ele/keb")]
    pairs = []
    for reading in entry.findall("r_ele"):
        kana = reading.findtext("reb")
        restrictions = [element.text for element in reading.findall("re_restr")]
        spellings = [kana] if not written or reading.find("re_nokanji") is not None else restrictions or written
        pairs.extend((spelling, kana) for spelling in spellings)
    pos = []
    for position, sense in enumerate(entry.findall("sense"), 1):
        pos = [element.text for element in sense.findall("pos")] or pos
        glosses = list(dict.fromkeys(g.text.strip() for g in sense.findall("gloss")
                      if g.text and g.text.strip() and
                      g.get("{http://www.w3.org/XML/1998/namespace}lang", "eng") == "eng"))
        kanji_limit = {element.text for element in sense.findall("stagk")}
        kana_limit = {element.text for element in sense.findall("stagr")}
        forms = list(dict.fromkeys((word, reading) for word, reading in pairs
                                   if (not kanji_limit or word in kanji_limit)
                                   and (not kana_limit or reading in kana_limit)))
        if not glosses or not forms:
            continue
        sid = db.execute("INSERT INTO senses(entry,position,pos) VALUES (?,?,?)",
                         (number, position, " / ".join(pos))).lastrowid
        db.executemany("INSERT INTO forms VALUES (?,?,?)", ((sid, *form) for form in forms))
        for i, example in enumerate(sense.findall("example")):
            sentences = {"jpn": [], "eng": []}
            for sentence in example.findall("ex_sent"):
                lang = sentence.get("{http://www.w3.org/XML/1998/namespace}lang", "eng")
                text = "".join(sentence.itertext()).strip()
                if lang in sentences and text:
                    sentences[lang].append(text)
            source = example.find("ex_srce")
            source_id = (source.text or "").strip() if source is not None else ""
            if source is not None and source.get("exsrc_type") == "tat" and source_id.isascii() and source_id.isdecimal():
                source_id = "https://tatoeba.org/sentences/show/" + source_id
            if any(sentences.values()):
                db.execute("INSERT INTO sense_examples VALUES (?,?,?,?,?)",
                           (sid, i, "\n".join(sentences["jpn"]), "\n".join(sentences["eng"]), source_id))
        for i, gloss in enumerate(glosses):
            exact = unicodedata.normalize("NFC", gloss)
            folded = exact.lower()
            bare_verb = folded[3:] if folded.startswith("to ") else None
            db.execute("INSERT INTO glosses VALUES (?,?,?,?,?,?)", (sid, i, gloss, exact, folded, bare_verb))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, nargs="?")
    parser.add_argument("--output", type=Path, default=ROOT / "data" / "jmdict.sqlite")
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    if args.verify_only:
        with closing(sqlite3.connect(args.output.resolve().as_uri() + "?mode=ro", uri=True)) as db:
            print("JMdict verified:", verify(db))
    elif args.source is None:
        parser.error("source is required unless --verify-only is specified")
    else:
        build(args.source, args.output)
