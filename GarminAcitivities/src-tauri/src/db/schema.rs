use rusqlite::{Connection, Result};

pub fn init_tables(conn: &Connection) -> Result<()> {
    conn.execute_batch(
        r#"
        CREATE TABLE IF NOT EXISTS activities (
            activity_id INTEGER PRIMARY KEY,
            user_id TEXT NOT NULL,
            region TEXT NOT NULL,
            activity_name TEXT NOT NULL,
            activity_type TEXT NOT NULL,
            start_time_local TEXT NOT NULL,
            distance_meters REAL DEFAULT 0.0,
            duration_seconds INTEGER DEFAULT 0,
            avg_speed_mps REAL DEFAULT 0.0,
            avg_hr_bpm INTEGER,
            max_hr_bpm INTEGER,
            elevation_gain_m REAL DEFAULT 0.0,
            raw_json TEXT,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
        );
        CREATE INDEX IF NOT EXISTS idx_activities_time ON activities(start_time_local DESC);
        CREATE INDEX IF NOT EXISTS idx_activities_type ON activities(activity_type);

        CREATE TABLE IF NOT EXISTS export_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            activity_id INTEGER NOT NULL,
            format TEXT NOT NULL,
            file_path TEXT NOT NULL,
            file_size_bytes INTEGER NOT NULL,
            exported_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            checksum_md5 TEXT,
            FOREIGN KEY(activity_id) REFERENCES activities(activity_id) ON DELETE CASCADE
        );
        CREATE UNIQUE INDEX IF NOT EXISTS idx_export_unique ON export_history(activity_id, format);

        CREATE TABLE IF NOT EXISTS app_settings (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );
        "#,
    )?;
    Ok(())
}
