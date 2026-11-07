// Package store wraps the SQLite database kept in <album>/.photo-album/.
package store

import (
	"database/sql"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"time"

	_ "modernc.org/sqlite"
)

// DirName is the hidden data directory inside the album directory.
const DirName = ".photo-album"

// DBName is the database file name inside DirName.
const DBName = "photo-album.db"

// Setting keys.
const (
	KeyAlbumDir           = "album_dir"
	KeyImportDir          = "import_dir"
	KeyIntervalMinutes    = "interval_minutes"
	KeyInitialIndexDone   = "initial_index_done"
	KeyIndexTotal         = "index_total"
	KeyIndexDone          = "index_done"
	KeyLastScanStartedAt  = "last_scan_started_at"
	KeyLastScanFinishedAt = "last_scan_finished_at"
	KeyNextScanAt         = "next_scan_at"
)

const schema = `
CREATE TABLE IF NOT EXISTS settings (
  key   TEXT PRIMARY KEY,
  value TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS files (
  rel_path   TEXT PRIMARY KEY,
  size       INTEGER NOT NULL,
  mtime_ns   INTEGER NOT NULL,
  md5        TEXT NOT NULL,
  indexed_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_files_md5 ON files(md5);
`

// Store is the application database.
type Store struct {
	db *sql.DB
}

// FileRec is one row of the MD5 index.
type FileRec struct {
	RelPath string
	Size    int64
	MtimeNs int64
	MD5     string
}

// DBPath returns the database path for an album directory.
func DBPath(albumDir string) string {
	return filepath.Join(albumDir, DirName, DBName)
}

// Open opens (creating if needed) the database for albumDir. When create is
// false the database file must already exist.
func Open(albumDir string, create bool) (*Store, error) {
	p := DBPath(albumDir)
	if create {
		if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
			return nil, err
		}
	} else if _, err := os.Stat(p); err != nil {
		return nil, err
	}
	dsn := "file:" + p + "?_pragma=journal_mode(WAL)&_pragma=busy_timeout(10000)&_pragma=synchronous(NORMAL)"
	db, err := sql.Open("sqlite", dsn)
	if err != nil {
		return nil, err
	}
	db.SetMaxOpenConns(1)
	if _, err := db.Exec(schema); err != nil {
		db.Close()
		return nil, fmt.Errorf("init schema: %w", err)
	}
	return &Store{db: db}, nil
}

// Close closes the database.
func (s *Store) Close() error { return s.db.Close() }

// Get returns a setting value ("" when missing).
func (s *Store) Get(key string) (string, error) {
	var v string
	err := s.db.QueryRow(`SELECT value FROM settings WHERE key=?`, key).Scan(&v)
	if errors.Is(err, sql.ErrNoRows) {
		return "", nil
	}
	return v, err
}

// Set stores a setting value.
func (s *Store) Set(key, value string) error {
	_, err := s.db.Exec(`INSERT INTO settings(key,value) VALUES(?,?)
		ON CONFLICT(key) DO UPDATE SET value=excluded.value`, key, value)
	return err
}

// GetInt returns an integer setting (0 when missing or invalid).
func (s *Store) GetInt(key string) (int64, error) {
	v, err := s.Get(key)
	if err != nil || v == "" {
		return 0, err
	}
	n, _ := strconv.ParseInt(v, 10, 64)
	return n, nil
}

// SetInt stores an integer setting.
func (s *Store) SetInt(key string, n int64) error { return s.Set(key, strconv.FormatInt(n, 10)) }

// GetTime returns a Unix-seconds setting as time (zero when missing).
func (s *Store) GetTime(key string) (time.Time, error) {
	n, err := s.GetInt(key)
	if err != nil || n == 0 {
		return time.Time{}, err
	}
	return time.Unix(n, 0), nil
}

// SetTime stores a time as Unix seconds.
func (s *Store) SetTime(key string, t time.Time) error { return s.SetInt(key, t.Unix()) }

// GetBool returns a boolean setting.
func (s *Store) GetBool(key string) (bool, error) {
	v, err := s.Get(key)
	return v == "true", err
}

// SetBool stores a boolean setting.
func (s *Store) SetBool(key string, b bool) error {
	if b {
		return s.Set(key, "true")
	}
	return s.Set(key, "false")
}

// UpsertFile inserts or replaces an index row.
func (s *Store) UpsertFile(r FileRec) error {
	_, err := s.db.Exec(`INSERT INTO files(rel_path,size,mtime_ns,md5,indexed_at) VALUES(?,?,?,?,?)
		ON CONFLICT(rel_path) DO UPDATE SET size=excluded.size, mtime_ns=excluded.mtime_ns,
		md5=excluded.md5, indexed_at=excluded.indexed_at`,
		r.RelPath, r.Size, r.MtimeNs, r.MD5, time.Now().Unix())
	return err
}

// DeleteFile removes an index row.
func (s *Store) DeleteFile(rel string) error {
	_, err := s.db.Exec(`DELETE FROM files WHERE rel_path=?`, rel)
	return err
}

// GetFile returns the index row for rel.
func (s *Store) GetFile(rel string) (FileRec, bool, error) {
	r := FileRec{RelPath: rel}
	err := s.db.QueryRow(`SELECT size,mtime_ns,md5 FROM files WHERE rel_path=?`, rel).Scan(&r.Size, &r.MtimeNs, &r.MD5)
	if errors.Is(err, sql.ErrNoRows) {
		return r, false, nil
	}
	return r, err == nil, err
}

// FindByMD5 returns any indexed path with the given MD5.
func (s *Store) FindByMD5(md5 string) (string, bool, error) {
	var rel string
	err := s.db.QueryRow(`SELECT rel_path FROM files WHERE md5=? ORDER BY rel_path LIMIT 1`, md5).Scan(&rel)
	if errors.Is(err, sql.ErrNoRows) {
		return "", false, nil
	}
	return rel, err == nil, err
}

// AllFiles returns every index row keyed by rel_path.
func (s *Store) AllFiles() (map[string]FileRec, error) {
	rows, err := s.db.Query(`SELECT rel_path,size,mtime_ns,md5 FROM files`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	m := map[string]FileRec{}
	for rows.Next() {
		var r FileRec
		if err := rows.Scan(&r.RelPath, &r.Size, &r.MtimeNs, &r.MD5); err != nil {
			return nil, err
		}
		m[r.RelPath] = r
	}
	return m, rows.Err()
}

// CountFiles returns the number of index rows.
func (s *Store) CountFiles() (int64, error) {
	var n int64
	err := s.db.QueryRow(`SELECT COUNT(*) FROM files`).Scan(&n)
	return n, err
}
