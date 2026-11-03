use rusqlite::{params, Connection, Result};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ActivityRecord {
    pub activity_id: i64,
    pub user_id: String,
    pub region: String,
    pub activity_name: String,
    pub activity_type: String,
    pub start_time_local: String,
    pub distance_meters: f64,
    pub duration_seconds: i64,
    pub avg_speed_mps: f64,
    pub avg_hr_bpm: Option<i32>,
    pub max_hr_bpm: Option<i32>,
    pub elevation_gain_m: f64,
    pub raw_json: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ActivityListItem {
    pub activity_id: i64,
    pub activity_name: String,
    pub activity_type: String,
    pub start_time_local: String,
    pub distance_meters: f64,
    pub duration_seconds: i64,
    pub avg_speed_mps: f64,
    pub avg_hr_bpm: Option<i32>,
    pub max_hr_bpm: Option<i32>,
    pub elevation_gain_meters: f64,
    pub is_exported: bool,
    pub local_file_path: Option<String>,
}

#[derive(Debug, Clone, Default, Deserialize)]
pub struct ActivityQuery {
    pub start: i64,
    pub limit: i64,
    pub activity_type: Option<String>,
    pub start_date: Option<String>,
    pub end_date: Option<String>,
}

pub struct HistoryRepo;

impl HistoryRepo {
    /// 批量 Upsert 活动数据
    pub fn upsert_activities(conn: &mut Connection, records: &[ActivityRecord]) -> Result<usize> {
        let tx = conn.transaction()?;
        let mut count = 0;
        {
            let mut stmt = tx.prepare(
                r#"
                INSERT INTO activities (
                    activity_id, user_id, region, activity_name, activity_type,
                    start_time_local, distance_meters, duration_seconds, avg_speed_mps,
                    avg_hr_bpm, max_hr_bpm, elevation_gain_m, raw_json, updated_at
                ) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, CURRENT_TIMESTAMP)
                ON CONFLICT(activity_id) DO UPDATE SET
                    activity_name = excluded.activity_name,
                    activity_type = excluded.activity_type,
                    start_time_local = excluded.start_time_local,
                    distance_meters = excluded.distance_meters,
                    duration_seconds = excluded.duration_seconds,
                    avg_speed_mps = excluded.avg_speed_mps,
                    avg_hr_bpm = excluded.avg_hr_bpm,
                    max_hr_bpm = excluded.max_hr_bpm,
                    elevation_gain_m = excluded.elevation_gain_m,
                    raw_json = excluded.raw_json,
                    updated_at = CURRENT_TIMESTAMP
                "#,
            )?;

            for item in records {
                stmt.execute(params![
                    item.activity_id,
                    item.user_id,
                    item.region,
                    item.activity_name,
                    item.activity_type,
                    item.start_time_local,
                    item.distance_meters,
                    item.duration_seconds,
                    item.avg_speed_mps,
                    item.avg_hr_bpm,
                    item.max_hr_bpm,
                    item.elevation_gain_m,
                    item.raw_json,
                ])?;
                count += 1;
            }
        }
        tx.commit()?;
        Ok(count)
    }

    /// 标记活动为已导出，记录文件落盘路径与摘要
    pub fn mark_as_exported(
        conn: &Connection,
        activity_id: i64,
        format: &str,
        file_path: &str,
        file_size_bytes: i64,
        checksum_md5: Option<&str>,
    ) -> Result<()> {
        conn.execute(
            r#"
            INSERT INTO export_history (
                activity_id, format, file_path, file_size_bytes, exported_at, checksum_md5
            ) VALUES (?1, ?2, ?3, ?4, CURRENT_TIMESTAMP, ?5)
            ON CONFLICT(activity_id, format) DO UPDATE SET
                file_path = excluded.file_path,
                file_size_bytes = excluded.file_size_bytes,
                exported_at = CURRENT_TIMESTAMP,
                checksum_md5 = excluded.checksum_md5
            "#,
            params![activity_id, format, file_path, file_size_bytes, checksum_md5],
        )?;
        Ok(())
    }

    /// 查询尚未以指定格式导出的活动 ID 列表（用于一键增量同步）
    pub fn query_unexported_ids(conn: &Connection, format: &str) -> Result<Vec<i64>> {
        let mut stmt = conn.prepare(
            r#"
            SELECT a.activity_id
            FROM activities a
            LEFT JOIN export_history e ON a.activity_id = e.activity_id AND e.format = ?1
            WHERE e.id IS NULL
            ORDER BY a.start_time_local DESC
            "#,
        )?;

        let rows = stmt.query_map(params![format], |row| row.get(0))?;
        let mut ids = Vec::new();
        for id in rows {
            ids.push(id?);
        }
        Ok(ids)
    }

    /// 分页多维查询活动列表，并左连接导出历史状态
    pub fn query_activities_page(
        conn: &Connection,
        query: &ActivityQuery,
    ) -> Result<(i64, Vec<ActivityListItem>)> {
        let mut where_clauses = Vec::new();
        let mut params: Vec<Box<dyn rusqlite::ToSql>> = Vec::new();

        if let Some(ref t) = query.activity_type {
            if t != "all" && !t.is_empty() {
                where_clauses.push("a.activity_type = ?".to_string());
                params.push(Box::new(t.clone()));
            }
        }

        if let Some(ref start) = query.start_date {
            if !start.is_empty() {
                // start_date 包含当天 (00:00:00)
                let start_dt = format!("{} 00:00:00", start.trim());
                where_clauses.push("a.start_time_local >= ?".to_string());
                params.push(Box::new(start_dt));
            }
        }

        if let Some(ref end) = query.end_date {
            if !end.is_empty() {
                // end_date 包含当天 (23:59:59)
                let end_dt = format!("{} 23:59:59", end.trim());
                where_clauses.push("a.start_time_local <= ?".to_string());
                params.push(Box::new(end_dt));
            }
        }

        let where_sql = if where_clauses.is_empty() {
            "".to_string()
        } else {
            format!("WHERE {}", where_clauses.join(" AND "))
        };

        // 1. 获取过滤后的总行数
        let count_sql = format!("SELECT COUNT(*) FROM activities a {}", where_sql);
        let count_params_slice: Vec<&dyn rusqlite::ToSql> = params.iter().map(|p| p.as_ref()).collect();
        let total_count: i64 = conn.query_row(&count_sql, rusqlite::params_from_iter(count_params_slice), |r| r.get(0))?;

        // 2. 查询分页记录
        let query_sql = format!(
            r#"
            SELECT
                a.activity_id,
                a.activity_name,
                a.activity_type,
                a.start_time_local,
                a.distance_meters,
                a.duration_seconds,
                a.avg_speed_mps,
                a.avg_hr_bpm,
                a.max_hr_bpm,
                a.elevation_gain_m,
                e.file_path
            FROM activities a
            LEFT JOIN export_history e ON a.activity_id = e.activity_id
            {}
            ORDER BY a.start_time_local DESC
            LIMIT ? OFFSET ?
            "#,
            where_sql
        );

        let mut query_params = params;
        let limit = if query.limit <= 0 { 50 } else { query.limit };
        let offset = if query.start < 0 { 0 } else { query.start };
        query_params.push(Box::new(limit));
        query_params.push(Box::new(offset));

        let q_params_slice: Vec<&dyn rusqlite::ToSql> = query_params.iter().map(|p| p.as_ref()).collect();
        let mut stmt = conn.prepare(&query_sql)?;
        let rows = stmt.query_map(rusqlite::params_from_iter(q_params_slice), |row| {
            let local_file_path: Option<String> = row.get(10)?;
            Ok(ActivityListItem {
                activity_id: row.get(0)?,
                activity_name: row.get(1)?,
                activity_type: row.get(2)?,
                start_time_local: row.get(3)?,
                distance_meters: row.get(4)?,
                duration_seconds: row.get(5)?,
                avg_speed_mps: row.get(6)?,
                avg_hr_bpm: row.get(7)?,
                max_hr_bpm: row.get(8)?,
                elevation_gain_meters: row.get(9)?,
                is_exported: local_file_path.is_some(),
                local_file_path,
            })
        })?;

        let mut items = Vec::new();
        for item in rows {
            items.push(item?);
        }

        Ok((total_count, items))
    }

    /// 动态查询指定日期区间内实际存在的运动类型列表（去重升序）
    pub fn query_activity_types_in_range(
        conn: &Connection,
        start_date: Option<&str>,
        end_date: Option<&str>,
    ) -> Result<Vec<String>> {
        let mut where_clauses = Vec::new();
        let mut params: Vec<Box<dyn rusqlite::ToSql>> = Vec::new();

        if let Some(start) = start_date {
            let trimmed = start.trim();
            if !trimmed.is_empty() {
                let start_dt = format!("{} 00:00:00", trimmed);
                where_clauses.push("start_time_local >= ?");
                params.push(Box::new(start_dt));
            }
        }

        if let Some(end) = end_date {
            let trimmed = end.trim();
            if !trimmed.is_empty() {
                let end_dt = format!("{} 23:59:59", trimmed);
                where_clauses.push("start_time_local <= ?");
                params.push(Box::new(end_dt));
            }
        }

        let where_sql = if where_clauses.is_empty() {
            "".to_string()
        } else {
            format!("WHERE {}", where_clauses.join(" AND "))
        };

        let sql = format!(
            "SELECT DISTINCT activity_type FROM activities {} ORDER BY activity_type ASC",
            where_sql
        );

        let params_slice: Vec<&dyn rusqlite::ToSql> = params.iter().map(|p| p.as_ref()).collect();
        let mut stmt = conn.prepare(&sql)?;
        let rows = stmt.query_map(rusqlite::params_from_iter(params_slice), |r| r.get(0))?;

        let mut types = Vec::new();
        for t in rows {
            types.push(t?);
        }
        Ok(types)
    }

    /// 读取系统设置
    pub fn get_setting(conn: &Connection, key: &str) -> Result<Option<String>> {
        let mut stmt = conn.prepare("SELECT value FROM app_settings WHERE key = ?1")?;
        let mut rows = stmt.query(params![key])?;
        if let Some(row) = rows.next()? {
            let val: String = row.get(0)?;
            Ok(Some(val))
        } else {
            Ok(None)
        }
    }

    /// 保存系统设置
    pub fn set_setting(conn: &Connection, key: &str, value: &str) -> Result<()> {
        conn.execute(
            "INSERT INTO app_settings (key, value) VALUES (?1, ?2) ON CONFLICT(key) DO UPDATE SET value = excluded.value",
            params![key, value],
        )?;
        Ok(())
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;
    use crate::db::schema::init_tables;

    fn setup_memory_db() -> Connection {
        let conn = Connection::open_in_memory().unwrap();
        init_tables(&conn).unwrap();
        conn
    }

    #[test]
    fn test_upsert_and_query_activities() {
        let mut conn = setup_memory_db();

        let records = vec![
            ActivityRecord {
                activity_id: 1001,
                user_id: "user_1".to_string(),
                region: "cn".to_string(),
                activity_name: "Morning Run 5K".to_string(),
                activity_type: "running".to_string(),
                start_time_local: "2026-11-01 07:00:00".to_string(),
                distance_meters: 5020.0,
                duration_seconds: 1500,
                avg_speed_mps: 3.34,
                avg_hr_bpm: Some(150),
                max_hr_bpm: Some(165),
                elevation_gain_m: 20.0,
                raw_json: None,
            },
            ActivityRecord {
                activity_id: 1002,
                user_id: "user_1".to_string(),
                region: "cn".to_string(),
                activity_name: "Weekend Ride 20K".to_string(),
                activity_type: "cycling".to_string(),
                start_time_local: "2026-11-02 09:30:00".to_string(),
                distance_meters: 20400.0,
                duration_seconds: 3600,
                avg_speed_mps: 5.66,
                avg_hr_bpm: Some(140),
                max_hr_bpm: Some(158),
                elevation_gain_m: 120.0,
                raw_json: None,
            },
        ];

        let count = HistoryRepo::upsert_activities(&mut conn, &records).unwrap();
        assert_eq!(count, 2);

        // 验证全部查询
        let (total, items) = HistoryRepo::query_activities_page(&conn, &ActivityQuery {
            start: 0,
            limit: 10,
            activity_type: None,
            start_date: None,
            end_date: None,
        }).unwrap();
        assert_eq!(total, 2);
        assert_eq!(items.len(), 2);
        assert_eq!(items[0].activity_id, 1002); // 按时间倒序
        assert!(!items[0].is_exported);

        // 验证分类过滤
        let (total_run, run_items) = HistoryRepo::query_activities_page(&conn, &ActivityQuery {
            start: 0,
            limit: 10,
            activity_type: Some("running".to_string()),
            start_date: None,
            end_date: None,
        }).unwrap();
        assert_eq!(total_run, 1);
        assert_eq!(run_items[0].activity_id, 1001);

        // 验证日期过滤 (闭区间包含当天)
        let (total_date, date_items) = HistoryRepo::query_activities_page(&conn, &ActivityQuery {
            start: 0,
            limit: 10,
            activity_type: None,
            start_date: Some("2026-11-02".to_string()),
            end_date: Some("2026-11-02".to_string()),
        }).unwrap();
        assert_eq!(total_date, 1);
        assert_eq!(date_items[0].activity_id, 1002);

        // 验证标记导出
        HistoryRepo::mark_as_exported(&conn, 1001, "gpx", "/path/to/1001.gpx", 4096, Some("md5checksum")).unwrap();
        let unexported = HistoryRepo::query_unexported_ids(&conn, "gpx").unwrap();
        assert_eq!(unexported, vec![1002]);

        // 再次查询已导出的项
        let (_, checked_items) = HistoryRepo::query_activities_page(&conn, &ActivityQuery {
            start: 0,
            limit: 10,
            activity_type: Some("running".to_string()),
            start_date: None,
            end_date: None,
        }).unwrap();
        assert!(checked_items[0].is_exported);
        assert_eq!(checked_items[0].local_file_path, Some("/path/to/1001.gpx".to_string()));
    }

    #[test]
    fn test_app_settings() {
        let conn = setup_memory_db();
        let val = HistoryRepo::get_setting(&conn, "exportDirectory").unwrap();
        assert_eq!(val, None);

        HistoryRepo::set_setting(&conn, "exportDirectory", "/Users/aubrey/Garmin").unwrap();
        let saved = HistoryRepo::get_setting(&conn, "exportDirectory").unwrap();
        assert_eq!(saved, Some("/Users/aubrey/Garmin".to_string()));
    }
}
