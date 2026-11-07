#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
修复 Cloudflare D1 远程数据库中被存为英文释义的历史词汇数据
"""

import json
import re
import urllib.parse
import urllib.request

CF_ACCOUNT_ID = "bdfbfec8245e011c293f2a76e30af237"
CF_API_TOKEN = ""
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


def fetch_chinese_enrichment(text):
    norm = text.strip().lower()
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
                    if raw_str and has_chinese(raw_str):
                        m = re.match(r"^([a-zA-Z]+\.?)\s*(.+)$", raw_str)
                        if m:
                            pos = (
                                m.group(1)
                                if m.group(1).endswith(".")
                                else f"{m.group(1)}."
                            )
                            meanings.append(
                                {"pos": pos, "zh": m.group(2).strip()}
                            )
                        else:
                            meanings.append({
                                "pos": "phr." if " " in norm else "n./v.",
                                "zh": raw_str.strip(),
                            })

            if ec.get("exam_type"):
                tags = [t for t in ec["exam_type"] if isinstance(t, str)]

        blng = data.get("blng_sents_part", {}).get("sentence-pair", [])
        for pair in blng[:2]:
            en = pair.get("sentence")
            zh = pair.get("sentence-translation")
            if en and zh and has_chinese(zh):
                examples.append({
                    "en": en.strip(),
                    "zh": zh.strip(),
                    "targetInSentence": text,
                })
    except Exception as e:
        print(f"Fetch error for {text}: {e}")

    # Fallback to suggest if needed
    if not meanings or not any(has_chinese(m.get("zh")) for m in meanings):
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
                        part = part.strip()
                        if not part:
                            continue
                        m = re.match(r"^([a-zA-Z]+\.?)\s*(.+)$", part)
                        if m:
                            meanings.append(
                                {"pos": m.group(1), "zh": m.group(2).strip()}
                            )
                        else:
                            meanings.append({
                                "pos": "phr." if " " in norm else "n./v.",
                                "zh": part,
                            })
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

        # 检查是否全部是英文无中文
        needs_fix = not any(has_chinese(m.get("zh", "")) for m in meanings)
        if not needs_fix:
            # 同时也检查例句是否为 请阅读理解该例句
            examples = json.loads(row.get("examples_json") or "[]")
            if any("请阅读理解" in ex.get("zh", "") for ex in examples):
                needs_fix = True

        if needs_fix:
            print(f"🛠️ 修复词条: {text}...")
            enriched = fetch_chinese_enrichment(text)
            new_meanings = enriched["meanings"]
            new_examples = enriched["examples"]
            new_phone = enriched["phonetic_us"]

            if not new_meanings:
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
                f"✅ 词条 {text} 已更新为真实中文: {[m['zh'] for m in new_meanings]}"
            )
            fixed_count += 1

    print(f"\n🎉 修复完成！共修复了 {fixed_count} 个词条的中文释义与例句！")


if __name__ == "__main__":
    main()
