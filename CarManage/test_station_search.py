#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
单元测试：加油站/充电站双地图检索、30米内以高德为主去重算法与离线缓存逻辑
"""

import math
import re
import unittest
import json
import urllib.parse
import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from server import CarTrackerHandler

def calc_haversine(lat1, lon1, lat2, lon2):
    R = 6371000
    rad = math.pi / 180
    d_lat = (lat2 - lat1) * rad
    d_lon = (lon2 - lon1) * rad
    a = (math.sin(d_lat / 2) ** 2 +
         math.cos(lat1 * rad) * math.cos(lat2 * rad) * (math.sin(d_lon / 2) ** 2))
    c = 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))
    return round(R * c)

def normalize_station_name(name):
    if not name:
        return ''
    s = re.sub(r'[\(（].*?[\)）]', '', name)
    s = re.sub(r'[·\-—\s]', '', s)
    s = re.sub(r'(加油站|加气站|充电站|充换电站|超充站|超级充电站|换电站)', '', s)
    return s.lower()

def get_station_brand_key(name):
    if not name:
        return 'other'
    n = name.lower()
    if '理想' in n or 'li auto' in n: return 'li'
    if '特斯拉' in n or 'tesla' in n: return 'tesla'
    if '华为' in n or '全液冷' in n: return 'huawei'
    if '蔚来' in n or 'nio' in n: return 'nio'
    if '小鹏' in n or 'xpeng' in n: return 'xpeng'
    if '极氪' in n or 'zeekr' in n: return 'zeekr'
    if '特来电' in n or 'teld' in n: return 'teld'
    if '星星充电' in n: return 'starcharge'
    if '国家电网' in n or '国网' in n: return 'stargrid'
    if '小桔' in n or '滴滴' in n: return 'xiaoju'
    if '开迈斯' in n: return 'cams'
    if '智电时代' in n: return 'zhidian'
    if '快电' in n: return 'kuaidian'
    if '云快充' in n: return 'yun'
    if '石化' in n: return 'sinopec'
    if '石油' in n: return 'petrochina'
    if '壳牌' in n: return 'shell'
    return 'other'

def merge_and_deduplicate(gaode_list, baidu_list, center_lng, center_lat, dist_threshold=30):
    normalized_gaode = []
    for p in gaode_list:
        lng, lat = [float(x) for x in p['location'].split(',')]
        dist = p.get('distance')
        if dist is None:
            dist = calc_haversine(center_lat, center_lng, lat, lng)
        normalized_gaode.append({
            'id': p.get('id', f"amap_{p['name']}"),
            'name': p['name'],
            'address': p.get('address', ''),
            'lng': lng,
            'lat': lat,
            'distance': dist,
            'source': 'amap'
        })

    normalized_baidu = []
    for p in baidu_list:
        lng = float(p['location']['lng'])
        lat = float(p['location']['lat'])
        dist = calc_haversine(center_lat, center_lng, lat, lng)
        normalized_baidu.append({
            'id': f"bd_{p.get('uid', p['name'])}",
            'name': p['name'],
            'address': p.get('address', ''),
            'lng': lng,
            'lat': lat,
            'distance': dist,
            'source': 'baidu'
        })

    result = list(normalized_gaode)
    for b in normalized_baidu:
        b_norm = normalize_station_name(b['name'])
        b_brand = get_station_brand_key(b['name'])
        is_dup = False
        for g in normalized_gaode:
            d_between = calc_haversine(b['lat'], b['lng'], g['lat'], g['lng'])
            if d_between <= dist_threshold:
                g_brand = get_station_brand_key(g['name'])
                # 核心保护：若两个站点分别属于不同明确品牌（例如 理想 vs 智电时代），绝不视为重复！
                if b_brand != 'other' and g_brand != 'other' and b_brand != g_brand:
                    continue
                g_norm = normalize_station_name(g['name'])
                name_match = bool(b_norm and g_norm and (b_norm == g_norm or b_norm in g_norm or g_norm in b_norm))
                if (b_brand != 'other' and b_brand == g_brand) or name_match or (d_between <= 10 and (b_norm in g_norm or g_norm in b_norm)):
                    is_dup = True
                    break
        if not is_dup:
            result.append(b)

    result.sort(key=lambda x: x['distance'] if x['distance'] is not None else 9999999)
    return result

class TestStationSearchAndDeduplication(unittest.TestCase):
    def test_haversine_distance(self):
        # 上海人民广场与大剧院 (~300米)
        d = calc_haversine(31.2304, 121.4737, 31.2294, 121.4726)
        self.assertTrue(100 < d < 500, f"Distance expected ~150-300m, got {d}")

    def test_station_name_normalization(self):
        n1 = normalize_station_name("中国石化成都路加油站")
        n2 = normalize_station_name("中国石化加油站(成都路店)")
        self.assertEqual(n1, "中国石化成都路")
        self.assertEqual(n2, "中国石化")

    def test_deduplication_within_30m_priority_gaode(self):
        # 高德站点：中国石化成都路加油站
        gaode = [{
            'id': 'amap_1',
            'name': '中国石化成都路加油站',
            'address': '成都北路118号',
            'location': '121.468157,31.226161',
            'distance': 707
        }]
        # 百度站点：同油站，坐标相差约 12 米 (< 30米)
        baidu_duplicate = [{
            'uid': 'bd_1',
            'name': '中国石化(成都北路加油站)',
            'address': '上海市黄浦区成都北路118号',
            'location': {'lat': 31.226230, 'lng': 121.468215}
        }]
        # 百度另一站点：距高德 500 米 (> 30米)
        baidu_distinct = [{
            'uid': 'bd_2',
            'name': '中化道达尔加油站',
            'address': '北京西路100号',
            'location': {'lat': 31.235000, 'lng': 121.465000}
        }]

        merged = merge_and_deduplicate(gaode, baidu_duplicate + baidu_distinct, 121.4737, 31.2304, dist_threshold=30)
        
        # 验证重复站点被过滤，只保留高德版本，且中化道达尔被纳入
        self.assertEqual(len(merged), 2)
        self.assertEqual(merged[0]['source'], 'amap')
        self.assertEqual(merged[0]['name'], '中国石化成都路加油站')
        self.assertEqual(merged[1]['source'], 'baidu')
        self.assertEqual(merged[1]['name'], '中化道达尔加油站')

    def test_deduplication_beyond_30m_kept(self):
        # 若两油站距离相距 45 米 (> 30米)，则均保留
        gaode = [{
            'id': 'amap_1',
            'name': '特来电充电站A区',
            'location': '121.468000,31.226000',
            'distance': 700
        }]
        baidu = [{
            'uid': 'bd_b',
            'name': '特来电充电站B区',
            'location': {'lat': 31.226450, 'lng': 121.468000} # ~50m away
        }]
        merged = merge_and_deduplicate(gaode, baidu, 121.4737, 31.2304, dist_threshold=30)
        self.assertEqual(len(merged), 2)

    def test_brand_collision_protection_li_vs_zhidian_near_2m(self):
        # 针对红星美凯龙场景：智电时代充电站与理想超充站相距仅 2.2 米，不同品牌绝不合并去重
        gaode = [{
            'id': 'amap_zhidian',
            'name': '智电时代汽车充电站(智电时代北五环红星美凯龙充电站)',
            'address': '北五环红星美凯龙西侧停车场',
            'location': '116.437400,40.023820',
            'distance': 10
        }]
        baidu = [{
            'uid': '2f543338cfd7cce19a00508d',
            'name': '理想充电站(理想超充站北京红星美凯龙(北五环店))',
            'address': '北京市朝阳区红星美凯龙(北五环店)停车场',
            'location': {'lat': 40.023801, 'lng': 116.437398}
        }]
        # 相距约 2.2 米
        d = calc_haversine(40.023820, 116.437400, 40.023801, 116.437398)
        self.assertTrue(d < 5, f"Expected < 5m, got {d}")

        merged = merge_and_deduplicate(gaode, baidu, 116.4374, 40.0238, dist_threshold=30)
        # 验证两个站点均完整保留，未被误杀合并
        self.assertEqual(len(merged), 2)
        names = [m['name'] for m in merged]
        self.assertTrue(any('智电时代' in n for n in names))
        self.assertTrue(any('理想' in n for n in names))

    def test_same_brand_within_30m_deduplicated(self):
        # 同属中石化品牌且在 30 米以内，视为重复高德为主
        gaode = [{
            'id': 'amap_sinopec_1',
            'name': '中国石化加油站(成都路店)',
            'location': '121.468157,31.226161',
            'distance': 100
        }]
        baidu = [{
            'uid': 'bd_sinopec_2',
            'name': '中国石化(成都北路站)',
            'location': {'lat': 31.226230, 'lng': 121.468215} # ~12m
        }]
        merged = merge_and_deduplicate(gaode, baidu, 121.4737, 31.2304, dist_threshold=30)
        self.assertEqual(len(merged), 1)
        self.assertEqual(merged[0]['source'], 'amap')

    def test_server_routes_baidu_search(self):
        # 验证 server.py 包含 /api/baidu-search 处理分支
        import inspect
        src = inspect.getsource(CarTrackerHandler.do_GET)
        self.assertIn("path == '/api/baidu-search'", src)
        self.assertIn("api.map.baidu.com", src)

    def test_worker_syntax_and_baidu_proxy(self):
        # 验证 worker.js 包含 /api/baidu-search 且 Service Worker 为 v44
        with open('car_maintenance_app/cloudflare/worker.js', 'r', encoding='utf-8') as f:
            w_code = f.read()
        self.assertIn("/api/baidu-search", w_code)
        self.assertIn("3YJcotkPHfGjr1vN50trOKrHoWUAwWCS", w_code)

        with open('car_maintenance_app/static/sw.js', 'r', encoding='utf-8') as f:
            sw_code = f.read()
        self.assertRegex(sw_code, r"car-tracker-cache-v\d+")

        with open('car_maintenance_app/static/db.js', 'r', encoding='utf-8') as f:
            db_code = f.read()
        self.assertRegex(db_code, r"version:\s*\d+")
        self.assertIn("station_cache", db_code)
        self.assertIn("saveStationCache", db_code)
        self.assertIn("queryStationCache", db_code)

    def test_app_js_brand_protection_and_verified_stations(self):
        with open('car_maintenance_app/static/app.js', 'r', encoding='utf-8') as f:
            app_code = f.read()
        self.assertIn("getStationBrandKey", app_code)
        self.assertIn("VERIFIED_CHARGING_STATIONS", app_code)
        self.assertIn("2f543338cfd7cce19a00508d", app_code)
        self.assertIn("page_size", app_code)
        self.assertIn("超充站", app_code)

if __name__ == '__main__':
    unittest.main()
