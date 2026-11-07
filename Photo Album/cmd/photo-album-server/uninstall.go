package main

import (
	"errors"
	"fmt"
	"log"
	"os"
	"path/filepath"

	"photo-album/internal/bootstrap"
	"photo-album/internal/store"
)

// uninstallCleanup deletes the configuration and MD5 index kept in
// <album>/.photo-album/ (PRD §1 卸载). Nothing else is touched.
func uninstallCleanup(dataDir string) error {
	cfg, err := bootstrap.Load(dataDir)
	if errors.Is(err, bootstrap.ErrNotConfigured) {
		log.Printf("uninstall-cleanup: not configured, nothing to do")
		return nil
	}
	if err != nil {
		return err
	}
	if !filepath.IsAbs(cfg.AlbumDir) {
		return fmt.Errorf("album_dir is not absolute: %q", cfg.AlbumDir)
	}
	target := filepath.Join(filepath.Clean(cfg.AlbumDir), store.DirName)
	st, err := os.Lstat(target)
	if errors.Is(err, os.ErrNotExist) {
		log.Printf("uninstall-cleanup: %s does not exist", target)
	} else if err != nil {
		return err
	} else if !st.IsDir() {
		return fmt.Errorf("%s is not a directory", target)
	} else {
		if err := os.RemoveAll(target); err != nil {
			return err
		}
		log.Printf("uninstall-cleanup: removed %s", target)
	}
	return os.Remove(filepath.Join(dataDir, bootstrap.FileName))
}
