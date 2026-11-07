// Package mover moves files into the album, preserving modification time and
// applying inherited permissions; cross-volume moves go through a hidden
// temporary file so a crash never leaves a partial file under the final name
// (ARCHITECTURE §5.8).
package mover

import (
	"crypto/rand"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"strings"
	"syscall"

	"photo-album/internal/perm"
)

// TmpPrefix marks in-progress copies; they start with "." so they are skipped
// by walks, and are removed on startup.
const TmpPrefix = ".photo-album-tmp-"

// ErrExists is returned when the destination already exists.
var ErrExists = errors.New("mover: destination exists")

// IsDiskFull reports whether err is an out-of-space error.
func IsDiskFull(err error) bool {
	return errors.Is(err, syscall.ENOSPC) || errors.Is(err, syscall.EDQUOT)
}

// rename is replaceable in tests to simulate cross-device moves.
var rename = os.Rename

// MkdirAll creates dir and any missing parents below stop, applying spec to
// every directory it creates.
func MkdirAll(dir, stop string, spec *perm.Spec) error {
	if st, err := os.Stat(dir); err == nil {
		if !st.IsDir() {
			return fmt.Errorf("mover: %s is not a directory", dir)
		}
		return nil
	}
	parent := filepath.Dir(dir)
	if parent != dir && filepath.Clean(dir) != filepath.Clean(stop) {
		if err := MkdirAll(parent, stop, spec); err != nil {
			return err
		}
	}
	if err := os.Mkdir(dir, 0o755); err != nil && !errors.Is(err, fs.ErrExist) {
		return err
	}
	return spec.ApplyDir(dir)
}

// Move moves src to dst (dst's directory must exist). spec may be nil.
func Move(src, dst string, spec *perm.Spec) error {
	if _, err := os.Lstat(dst); err == nil {
		return ErrExists
	} else if !errors.Is(err, fs.ErrNotExist) {
		return err
	}
	err := rename(src, dst)
	if err == nil {
		return spec.ApplyFile(dst)
	}
	if !isCrossDevice(err) {
		return err
	}
	return copyMove(src, dst, spec)
}

func isCrossDevice(err error) bool { return errors.Is(err, syscall.EXDEV) }

func copyMove(src, dst string, spec *perm.Spec) (err error) {
	st, err := os.Stat(src)
	if err != nil {
		return err
	}
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()

	var rnd [6]byte
	_, _ = rand.Read(rnd[:])
	tmp := filepath.Join(filepath.Dir(dst), TmpPrefix+hex.EncodeToString(rnd[:]))
	out, err := os.OpenFile(tmp, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0o644)
	if err != nil {
		return err
	}
	defer func() {
		if err != nil {
			out.Close()
			os.Remove(tmp)
		}
	}()
	if _, err = io.CopyBuffer(out, in, make([]byte, 1<<20)); err != nil {
		return err
	}
	if err = out.Sync(); err != nil {
		return err
	}
	if err = out.Close(); err != nil {
		return err
	}
	if err = os.Chtimes(tmp, st.ModTime(), st.ModTime()); err != nil {
		return err
	}
	if err = spec.ApplyFile(tmp); err != nil {
		return err
	}
	if _, e := os.Lstat(dst); e == nil {
		err = ErrExists
		return err
	}
	if err = os.Rename(tmp, dst); err != nil {
		return err
	}
	in.Close()
	if e := os.Remove(src); e != nil {
		// The copy is complete; report but keep the imported file.
		return fmt.Errorf("mover: copied but could not remove source: %w", e)
	}
	return nil
}

// CleanTemp removes a leftover temporary file (called for skipped files
// found while walking the album).
func CleanTemp(abs string) bool {
	if strings.HasPrefix(filepath.Base(abs), TmpPrefix) {
		return os.Remove(abs) == nil
	}
	return false
}
