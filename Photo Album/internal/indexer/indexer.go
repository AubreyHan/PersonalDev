// Package indexer maintains the MD5 index of the album directory
// (PRD §3.4, ARCHITECTURE §5.3).
package indexer

import (
	"context"
	"log"
	"time"

	"photo-album/internal/hashing"
	"photo-album/internal/mover"
	"photo-album/internal/store"
	"photo-album/internal/walk"
)

// Indexer syncs the files table with the album directory.
type Indexer struct {
	Store    *store.Store
	AlbumDir string
	// Hash is replaceable in tests.
	Hash func(path string) (string, error)
	// ProgressEvery controls how often index_done is persisted.
	ProgressEvery time.Duration
}

// New returns an Indexer.
func New(s *store.Store, albumDir string) *Indexer {
	return &Indexer{Store: s, AlbumDir: albumDir, Hash: hashing.MD5File, ProgressEvery: 2 * time.Second}
}

// Initial performs (or resumes) the first full index and marks it done.
func (ix *Indexer) Initial(ctx context.Context) error {
	if err := ix.sync(ctx, true); err != nil {
		return err
	}
	return ix.Store.SetBool(store.KeyInitialIndexDone, true)
}

// Incremental brings the index up to date (run before every scan).
func (ix *Indexer) Incremental(ctx context.Context) error { return ix.sync(ctx, false) }

func (ix *Indexer) sync(ctx context.Context, progress bool) error {
	entries, errs, err := walk.Walk(ix.AlbumDir, walk.Options{OnSkipped: func(abs string) { mover.CleanTemp(abs) }})
	if err != nil {
		return err
	}
	for _, e := range errs {
		log.Printf("indexer: %v", e)
	}
	existing, err := ix.Store.AllFiles()
	if err != nil {
		return err
	}
	total := int64(len(entries))
	if progress {
		if err := ix.Store.SetInt(store.KeyIndexTotal, total); err != nil {
			return err
		}
	}
	var done int64
	last := time.Now()
	seen := make(map[string]bool, len(entries))
	for _, e := range entries {
		if err := ctx.Err(); err != nil {
			return err
		}
		seen[e.Rel] = true
		rec, ok := existing[e.Rel]
		if !ok || rec.Size != e.Size || rec.MtimeNs != e.ModTime.UnixNano() {
			sum, err := ix.Hash(e.Abs)
			if err != nil {
				log.Printf("indexer: hash %s: %v", e.Abs, err)
			} else if err := ix.Store.UpsertFile(store.FileRec{RelPath: e.Rel, Size: e.Size, MtimeNs: e.ModTime.UnixNano(), MD5: sum}); err != nil {
				return err
			}
		}
		done++
		if progress && time.Since(last) >= ix.ProgressEvery {
			last = time.Now()
			if err := ix.Store.SetInt(store.KeyIndexDone, done); err != nil {
				return err
			}
		}
	}
	for rel := range existing {
		if !seen[rel] {
			if err := ix.Store.DeleteFile(rel); err != nil {
				return err
			}
		}
	}
	if progress {
		return ix.Store.SetInt(store.KeyIndexDone, done)
	}
	return nil
}
