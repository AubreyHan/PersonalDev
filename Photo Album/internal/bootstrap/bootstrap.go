// Package bootstrap reads and writes bootstrap.json in the QPKG install
// directory. The file only records the album directory; its presence marks
// the first-run setup as complete (ARCHITECTURE §4.1).
package bootstrap

import (
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
)

// FileName is the bootstrap file name inside the data directory.
const FileName = "bootstrap.json"

// Config is the bootstrap content.
type Config struct {
	AlbumDir string `json:"album_dir"`
}

// ErrNotConfigured is returned by Load when bootstrap.json does not exist.
var ErrNotConfigured = errors.New("not configured")

// Load reads <dir>/bootstrap.json.
func Load(dir string) (*Config, error) {
	b, err := os.ReadFile(filepath.Join(dir, FileName))
	if errors.Is(err, os.ErrNotExist) {
		return nil, ErrNotConfigured
	}
	if err != nil {
		return nil, err
	}
	var c Config
	if err := json.Unmarshal(b, &c); err != nil {
		return nil, err
	}
	if c.AlbumDir == "" {
		return nil, errors.New("bootstrap.json: empty album_dir")
	}
	return &c, nil
}

// Save atomically writes <dir>/bootstrap.json.
func Save(dir string, c Config) error {
	b, err := json.MarshalIndent(c, "", "  ")
	if err != nil {
		return err
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	tmp := filepath.Join(dir, FileName+".tmp")
	if err := os.WriteFile(tmp, append(b, '\n'), 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, filepath.Join(dir, FileName))
}
