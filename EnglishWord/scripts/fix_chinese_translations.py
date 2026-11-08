#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
修复 Cloudflare D1 远程数据库中被存为英文释义或占位符的历史词汇与词组数据
"""

import json
import os
import re
import urllib.parse
import urllib.request

CF_ACCOUNT_ID = os.environ.get("CF_ACCOUNT_ID", "bdfbfec8245e011c293f2a76e30af237")
CF_API_TOKEN = os.environ.get("CF_API_TOKEN", "")
D1_DATABASE_ID = "75fd4e87-82eb-416f-81e1-7dcd4777682a"
CF_API_BASE = "https://api.cloudflare.com/client/v4"

HEADERS = {
    "Authorization": f"Bearer {CF_API_TOKEN}",
    "Content-Type": "application/json",
    "User-Agent": "Mozilla/5.0",
}


def query_d1(sql, params=None):
    payload = {"sql": sql}
    if params:
        payload["params"] = params
    url = f"{CF_API_BASE}/accounts/{CF_ACCOUNT_ID}/d1/database/{D1_DATABASE_ID}/query"
    req = urllib.request.Request(
        url, headers=HEADERS, data=json.dumps(payload).encode("utf-8")
    )
    with urllib.request.urlopen(req) as resp:
        res = json.loads(resp.read().decode("utf-8"))
        if not res.get("success"):
            raise RuntimeError(f"D1 Query failed: {res}")
        return res["result"][0]


def has_chinese(text):
    if not text:
        return False
    return bool(re.search(r"[\u4e00-\u9fa5]", text))


def is_placeholder_meaning(zh):
    if not zh or not has_chinese(zh):
        return True
    if "请编辑" in zh or "暂无中文" in zh:
        return True
    return False


def is_placeholder_example(zh):
    if not zh or not has_chinese(zh):
        return True
    if "请阅读理解" in zh or bool(re.match(r"^这是关于\s*.+\s*的例句[。.]?$", zh.strip())):
        return True
    return False


def parse_meaning_string(raw_str, is_phrase):
    raw = raw_str.strip()
    if not raw or not has_chinese(raw):
        return None

    pos_match = re.match(r"^([a-zA-Z]+\.?)\s*(.+)$", raw)
    if pos_match:
        pos = pos_match.group(1) if pos_match.group(1).endswith(".") else f"{pos_match.group(1)}."
        zh = pos_match.group(2).strip()
        if "：" in zh or ":" in zh:
            parts = re.split(r"[:：]", zh, maxsplit=1)
            if has_chinese(parts[0]):
                zh = parts[0].strip()
        zh = re.sub(r"[…\.\s]+$", "", zh)
        if not zh or is_placeholder_meaning(zh):
            return None
        return {"pos": pos, "zh": zh}

    if "：" in raw or ":" in raw:
        parts = re.split(r"[:：]", raw, maxsplit=1)
        if has_chinese(parts[0]):
            zh = re.sub(r"[…\.\s]+$", "", parts[0].strip())
            if zh and not is_placeholder_meaning(zh):
                return {"pos": "phr." if is_phrase else "n./v.", "zh": zh}

    clean_zh = re.sub(r"[…\.\s]+$", "", raw)
    if not clean_zh or is_placeholder_meaning(clean_zh):
        return None
    return {"pos": "phr." if is_phrase else "n./v.", "zh": clean_zh}


def fetch_chinese_enrichment(text):
    norm = text.strip().lower()
    is_phrase = " " in norm
    url = f"https://dict.youdao.com/jsonapi?q={urllib.parse.quote(norm)}"
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})

    meanings = []
    examples = []
    phonetic_us = ""
    tags = []

    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            data = json.loads(resp.read().decode("utf-8"))

        ec = data.get("ec")
        if ec and ec.get("word"):
            w = ec["word"][0]
            if w.get("usphone"):
                phonetic_us = f"/{w['usphone']}/"
            for tr_group in w.get("trs", []):
                for tr in tr_group.get("tr", []):
                    raw_str = tr.get("l", {}).get("i", [""])[0]
                    p = parse_meaning_string(raw_str, is_phrase)
                    if p and not any(m["zh"] == p["zh"] for m in meanings):
                        meanings.append(p)

            if ec.get("exam_type"):
                tags = [t for t in ec["exam_type"] if isinstance(t, str)]

        # web_trans fallback for phrases or idioms
        if not meanings and data.get("web_trans"):
            wt_list = data["web_trans"].get("web-translation", [])
            for wt in wt_list[:2]:
                for t in wt.get("trans", [])[:2]:
                    v = re.sub(r"<[^>]+>", "", t.get("value", "")).strip()
                    if v and has_chinese(v) and len(v) <= 20:
                        clean_v = re.sub(r"[…\.\s]+$", "", v)
                        if clean_v and not is_placeholder_meaning(clean_v) and not any(m["zh"] == clean_v for m in meanings):
                            meanings.append({"pos": "phr." if is_phrase else "n./v.", "zh": clean_v})
                if len(meanings) >= 2:
                    break

        blng = data.get("blng_sents_part", {}).get("sentence-pair", [])
        for pair in blng[:3]:
            en = pair.get("sentence") or re.sub(r"<[^>]+>", "", pair.get("sentence-eng", ""))
            zh = pair.get("sentence-translation")
            if en and zh and has_chinese(zh) and not is_placeholder_example(zh):
                examples.append({
                    "en": en.strip(),
                    "zh": zh.strip(),
                    "targetInSentence": text,
                })
    except Exception as e:
        print(f"Fetch error for {text}: {e}")

    # Fallback to suggest if needed
    if not meanings or any(is_placeholder_meaning(m.get("zh")) for m in meanings):
        try:
            sug_url = f"https://dict.youdao.com/suggest?num=1&doctype=json&q={urllib.parse.quote(norm)}"
            sug_req = urllib.request.Request(
                sug_url, headers={"User-Agent": "Mozilla/5.0"}
            )
            with urllib.request.urlopen(sug_req, timeout=5) as resp:
                sug_data = json.loads(resp.read().decode("utf-8"))
            entries = sug_data.get("data", {}).get("entries", [])
            if entries and entries[0].get("explain"):
                exp = entries[0]["explain"]
                if has_chinese(exp):
                    for part in re.split(r"[;；]", exp):
                        p = parse_meaning_string(part, is_phrase)
                        if p and not any(m["zh"] == p["zh"] for m in meanings):
                            meanings.append(p)
        except Exception as e:
            print(f"Suggest error for {text}: {e}")

    return {
        "meanings": meanings,
        "examples": examples,
        "phonetic_us": phonetic_us,
        "tags": tags,
    }


def main():
    print("🔍 查询当前数据库中的词条...")
    res = query_d1("SELECT id, text, meanings_json, examples_json FROM teacher_words")
    rows = res.get("results", [])
    print(f"共发现 {len(rows)} 个词条")

    fixed_count = 0
    for row in rows:
        wid = row["id"]
        text = row["text"]
        meanings = json.loads(row.get("meanings_json") or "[]")
        examples = json.loads(row.get("examples_json") or "[]")

        # 检查是否全部是英文无中文，或者包含占位符
        needs_fix_meaning = (
            len(meanings) == 0 or
            any(is_placeholder_meaning(m.get("zh", "")) for m in meanings)
        )
        needs_fix_example = (
            len(examples) == 0 or
            any(is_placeholder_example(ex.get("zh", "")) for ex in examples)
        )

        needs_fix = needs_fix_meaning or needs_fix_example

        if needs_fix:
            print(f"🛠️ 修复词条: {text} (meaning={needs_fix_meaning}, example={needs_fix_example})...")
            enriched = fetch_chinese_enrichment(text)
            new_meanings = enriched["meanings"] if needs_fix_meaning else meanings
            new_examples = enriched["examples"] if needs_fix_example else examples
            new_phone = enriched["phonetic_us"]

            if not new_meanings and needs_fix_meaning:
                print(f"⚠️ 无法自动获取 {text} 的中文释义，跳过")
                continue

            sql = """
            UPDATE teacher_words
            SET meanings_json = ?, examples_json = ?, phonetic_us = CASE WHEN phonetic_us = '' THEN ? ELSE phonetic_us END, updated_at = ?
            WHERE id = ?
            """
            params = [
                json.dumps(new_meanings, ensure_ascii=False),
                json.dumps(new_examples, ensure_ascii=False),
                new_phone,
                1794030000000,
                wid,
            ]
            query_d1(sql, params)
            print(
                f"✅ 词条 {text} 已更新:\n   释义: {[m['zh'] for m in new_meanings]}\n   例句: {[ex['zh'] for ex in new_examples[:1]]}"
            )
            fixed_count += 1

    print(f"\n🎉 修复完成！共修复了 {fixed_count} 个词条的中文释义与例句！")


if __name__ == "__main__":
    main()
