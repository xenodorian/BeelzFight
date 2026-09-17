#!/usr/bin/env python3
"""Bake shared content/*.json into Dreamcast C includes.

Source of truth is the JSON pack (web loads it directly). This writer
upper-cases dialogue for the DC bitmap font and emits the C symbol
names main.c already references.
"""
from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# Workspace layout: /workspace/content  +  optional DC clone at --out
CONTENT = ROOT / "content"


SPEAKER = {
    "none": 0,
    "max": 1,
    "anne": 2,
    "mason": 3,
    "wren": 4,
    "mae": 5,
    "ivo": 6,
    "nell": 7,
    "pike": 8,
    "calder": 9,
    "bram": 10,
    "cathleen": 11,
    "shinigami": 12,
    "oren": 13,
    "tessa": 14,
    "birch": 15,
    "sable": 16,
    "cross": 17,
    "commander": 18,
    "conscript": 19,
    "enforcer": 20,
    "sentry": 21,
    "father": 22,
    "heavenfall": 23,
}

# JSON camelCase key -> existing main.c TALK_* symbol
TALK_C = {
    "father": "TALK_FATHER",
    "fatherAfter": "TALK_FATHER_AFTER",
    "bed": "TALK_BED",
    "shelf": "TALK_SHELF",
    "shelfEmpty": "TALK_SHELF_EMPTY",
    "crate": "TALK_CRATE",
    "crateEmpty": "TALK_CRATE_EMPTY",
    "doorLocked": "TALK_DOOR_LOCKED",
    "masonFight": "TALK_MASON_FIGHT",
    "masonWin": "TALK_MASON_WIN",
    "masonFight2": "TALK_MASON_FIGHT2",
    "masonWin2": "TALK_MASON_WIN2",
    "wrenFirst": "TALK_WREN_FIRST",
    "wrenBeat": "TALK_WREN_BEAT",
    "wrenCart": "TALK_WREN_CART",
    "wrenHeal": "TALK_WREN_HEAL",
    "maeFirst": "TALK_MAE_FIRST",
    "maeAgain": "TALK_MAE_AGAIN",
    "ivoFirst": "TALK_IVO_FIRST",
    "ivoAgain": "TALK_IVO_AGAIN",
    "nellFirst": "TALK_NELL_FIRST",
    "nellBonus": "TALK_NELL_BONUS",
    "nellAgain": "TALK_NELL_AGAIN",
    "pikeFirst": "TALK_PIKE_FIRST",
    "pikeHelp": "TALK_PIKE_HELP",
    "pikeDone": "TALK_PIKE_DONE",
    "pikeHint": "TALK_PIKE_HINT",
    "herb": "TALK_HERB",
    "herbGone": "TALK_HERB_GONE",
    "gemPike": "TALK_GEM_PIKE",
    "gemWild": "TALK_GEM_WILD",
    "gemGone": "TALK_GEM_GONE",
    "stump": "TALK_STUMP",
    "stumpGone": "TALK_STUMP_GONE",
    "cart": "TALK_CART",
    "calderAfter": "TALK_CALDER_AFTER",
    "calderFight": "TALK_CALDER_FIGHT",
    "calderWin": "TALK_CALDER_WIN",
    "commander": "TALK_CAMP_COMMANDER",
    "cathleenSpot": "TALK_CATHLEEN_SPOT",
    "cathleenAfter": "TALK_CATHLEEN_AFTER",
    "cathleenGone": "TALK_CATHLEEN_GONE",
    "shinigamiSpot": "TALK_SHINIGAMI_SPOT",
    "shinigamiAfter": "TALK_SHINIGAMI_WIN",
    "shinigamiDone": "TALK_SHINIGAMI_DONE",
    "soldierSpot": "TALK_SOLDIER_SPOT",
    "soldierDone": "TALK_SOLDIER_DONE",
    "soldierAfter": "TALK_SOLDIER_AFTER",
    "bramOpen": "TALK_BRAM_OPEN",
    "sentrySpot": "TALK_WSOLDIER_CLIFFS_SPOT",
    "sentryWin": "TALK_WSOLDIER_CLIFFS_WIN",
    "conscriptSpot": "TALK_WSOLDIER_CAMP1_SPOT",
    "conscriptWin": "TALK_WSOLDIER_CAMP1_WIN",
    "enforcerSpot": "TALK_WSOLDIER_CAMP2_SPOT",
    "enforcerWin": "TALK_WSOLDIER_CAMP2_WIN",
    "crossSpot": "TALK_WSOLDIER_GROVE_SPOT",
    "crossWin": "TALK_WSOLDIER_GROVE_WIN",
    "orenOpen": "TALK_OREN_OPEN",
    "tessaFirst": "TALK_TESSA_FIRST",
    "tessaAgain": "TALK_TESSA_AGAIN",
    "birchFirst": "TALK_BIRCH_FIRST",
    "birchAgain": "TALK_BIRCH_AGAIN",
    "sableFirst": "TALK_SABLE_FIRST",
    "sableAgain": "TALK_SABLE_AGAIN",
    "chest": "TALK_CHEST",
    "chestEmpty": "TALK_CHEST_EMPTY",
    "anneGift": "TALK_ANNE_GIFT",
    "anneReturn": "TALK_ANNE_RETURN",
    "choiceFather": "TALK_CHOICE_FATHER",
    "choiceHeavenfall": "TALK_CHOICE_HEAVENFALL",
}

SPELL = {"firebolt": 0, "icebeam": 1, "lightning": 2, "manasurge": 3}

SPECIES_ORDER = [
    "quillpup", "glimmoth", "tortcask", "razorbat", "mossback",
    "briarfox", "fenwisp", "duskhorn", "needleroot", "cathleen",
    "crymare", "emberling", "frostail", "boulderam", "stormwing",
    "sableclaw", "thornhide", "glasswisp", "ashenmaw", "heavenfall",
]

MAP_ORDER = ["house", "veld", "forest", "grove", "camp", "cliffs", "ruins"]


def c_escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"')


def dc_text(s: str) -> str:
    t = s.upper()
    t = t.replace("\u2014", ".").replace("\u2013", ".").replace("\u2018", "'").replace("\u2019", "'")
    t = t.replace('"', "").replace(":", ",").replace(";", ",")
    t = re.sub(r"\s+", " ", t).strip()
    return t


def load_pack(content: Path) -> dict:
    return {
        "species": json.loads((content / "species.json").read_text()),
        "items": json.loads((content / "items.json").read_text()),
        "maps": json.loads((content / "maps.json").read_text()),
        "dialogue": json.loads((content / "dialogue.json").read_text()),
        "world": json.loads((content / "world.json").read_text()),
    }


HEADER = "/* AUTO-GENERATED from content/*.json — do not edit. python3 tools/bake_content.py */\n"


def bake_maps(data: dict, out: Path) -> None:
    maps = data["maps"]["rows"]
    names = data["world"]["mapNames"]
    lines = [HEADER]
    for mid in MAP_ORDER:
        rows = maps[mid]
        lines.append(f"static const char *const map_{mid}_rows[] = {{")
        for r in rows:
            lines.append(f'    "{c_escape(r)}",')
        lines.append("};")
        lines.append("")
    lines.append("static const Map MAPS[7] = {")
    for mid in MAP_ORDER:
        rows = maps[mid]
        cols = max(len(r) for r in rows)
        lines.append(f"    {{ map_{mid}_rows, {cols}, {len(rows)} }},")
    lines.append("};")
    lines.append("")
    lines.append("static const char *const MAP_DISPLAY_NAME[7] = {")
    lines.append("    " + ", ".join(f'"{c_escape(dc_text(names[m]))}"' for m in MAP_ORDER) + ",")
    lines.append("};")
    lines.append("")
    out.write_text("\n".join(lines) + "\n")


def bake_talk(data: dict, out: Path) -> None:
    talk = data["dialogue"]["talk"]
    ending = data["dialogue"]["endingWin"]
    lines = [HEADER, "#if defined(__GNUC__)"]
    lines.append("#pragma GCC diagnostic ignored \"-Wunused-const-variable\"")
    lines.append("#endif")
    lines.append("")
    for key, symbol in TALK_C.items():
        beats = talk[key]
        lines.append(f"static const TalkBeat {symbol}[] = {{")
        for b in beats:
            sp = SPEAKER[b["speaker"]]
            text = dc_text(b["text"])
            lines.append(f'    {{ "{c_escape(text)}", {sp} }},')
        lines.append("};")
    lines.append("")
    lines.append("static const char *const DEMO_END[] = {")
    for s in ending:
        lines.append(f'    "{c_escape(dc_text(s))}",')
    lines.append("};")
    lines.append("")
    lines.append("#define TALK_LEN(arr) (int)(sizeof(arr) / sizeof((arr)[0]))")
    lines.append("")
    out.write_text("\n".join(lines) + "\n")


def bake_species(data: dict, out: Path) -> None:
    spec = data["species"]
    lines = [HEADER, "static const Species SPECIES[20] = {"]
    for sid in SPECIES_ORDER:
        s = spec[sid]
        spells = s.get("spells") or []
        ids = [SPELL[sp["id"]] for sp in spells]
        while len(ids) < 4:
            ids.append(0)
        n = len(spells)
        name = dc_text(s["name"])
        basic = dc_text(s["basic"])
        special = dc_text(s["special"])
        lines.append(
            f'    {{ "{c_escape(name)}", "{c_escape(basic)}", "{c_escape(special)}", '
            f'{s["maxHp"]}, {s["str"]}, {s["agl"]}, {s["spc"]}, {s["specialPp"]}, '
            f"{n}, {{{ids[0]},{ids[1]},{ids[2]},{ids[3]}}} }},"
        )
    lines.append("};")
    lines.append("")
    out.write_text("\n".join(lines) + "\n")


def bake_items(data: dict, out: Path) -> None:
    items = data["items"]
    order = items["order"]
    defs = items["defs"]
    lines = [HEADER, f"#define ITEM_COUNT {len(order)}", "static const ItemDef ITEMS[ITEM_COUNT] = {"]
    for iid in order:
        it = defs[iid]
        lines.append(f'    {{ "{c_escape(dc_text(it["name"]))}", {it["buy"]}, {it["sell"]} }},')
    lines.append("};")
    lines.append("")
    out.write_text("\n".join(lines) + "\n")


def main() -> None:
    import argparse

    ap = argparse.ArgumentParser()
    ap.add_argument("--content", type=Path, default=CONTENT)
    ap.add_argument("--out", type=Path, default=None, help="Directory for *.inc")
    args = ap.parse_args()
    content = args.content
    data = load_pack(content)
    if args.out:
        outdir = args.out
    else:
        cand = ROOT / "ports" / "dreamcast" / "src"
        if not cand.is_dir():
            cand = Path("/tmp/beelz/repo/crymon-dreamcast/src")
        outdir = cand if cand.is_dir() else content.parent / "src"
    outdir.mkdir(parents=True, exist_ok=True)
    bake_maps(data, outdir / "content_maps.inc")
    bake_talk(data, outdir / "content_talk.inc")
    bake_species(data, outdir / "content_species.inc")
    bake_items(data, outdir / "content_items.inc")
    print(f"baked maps/talk/species/items -> {outdir}")


if __name__ == "__main__":
    main()
