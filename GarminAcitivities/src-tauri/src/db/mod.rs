pub mod history_repo;
pub mod schema;

use std::path::PathBuf;
use std::sync::{Arc, Mutex};
use rusqlite::Connection;

#[derive(Clone)]
pub struct DbManager {
    pub conn: Arc<Mutex<Connection>>,
    pub db_path: PathBuf,
}

impl DbManager {
    pub fn new_in_memory() -> rusqlite::Result<Self> {
        let conn = Connection::open_in_memory()?;
        schema::init_tables(&conn)?;
        Ok(Self {
            conn: Arc::new(Mutex::new(conn)),
            db_path: PathBuf::from(":memory:"),
        })
    }

    pub fn new_file(path: PathBuf) -> rusqlite::Result<Self> {
        if let Some(parent) = path.parent() {
            let _ = std::fs::create_dir_all(parent);
        }
        let conn = Connection::open(&path)?;
        schema::init_tables(&conn)?;
        Ok(Self {
            conn: Arc::new(Mutex::new(conn)),
            db_path: path,
        })
    }
}
