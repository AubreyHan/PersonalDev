package importer

import (
	"path"
	"strings"

	"photo-album/internal/metadata"
	"photo-album/internal/walk"
)

// file is a supported file found in the import directory.
type file struct {
	walk.Entry
	Info metadata.Info
}

func (f *file) name() string { return path.Base(f.Rel) }
func (f *file) dir() string  { return path.Dir(f.Rel) }
func (f *file) ext() string  { return strings.ToLower(path.Ext(f.Rel)) }
func (f *file) stem() string { n := f.name(); return n[:len(n)-len(path.Ext(n))] }

func (f *file) isLiveImage() bool {
	switch f.ext() {
	case ".heic", ".heif", ".jpg", ".jpeg":
		return true
	}
	return false
}

func (f *file) isLiveVideo() bool { return f.ext() == ".mov" }

// unit is a processing unit: a single file, or a Live Photo (image, video).
type unit struct {
	files []*file
	live  bool
}

// buildUnits pairs Live Photos (ARCHITECTURE §5.6) and returns units ordered
// by the smallest relative path of their members. files must be sorted by Rel.
func buildUnits(files []*file) []unit {
	used := make([]bool, len(files))
	pairOf := map[int]int{} // image index -> video index

	// 1. ContentIdentifier: exactly one image and one video share the ID.
	type idx struct{ img, vid []int }
	byCID := map[string]*idx{}
	for i, f := range files {
		if f.Info.ContentID == "" {
			continue
		}
		g := byCID[f.Info.ContentID]
		if g == nil {
			g = &idx{}
			byCID[f.Info.ContentID] = g
		}
		if f.isLiveImage() {
			g.img = append(g.img, i)
		} else if f.isLiveVideo() {
			g.vid = append(g.vid, i)
		}
	}
	for _, g := range byCID {
		if len(g.img) == 1 && len(g.vid) == 1 {
			pairOf[g.img[0]] = g.vid[0]
			used[g.img[0]], used[g.vid[0]] = true, true
		}
	}

	// 2. Fallback: same directory, same stem (case-insensitive), exactly one
	// image and one video, and no conflicting ContentIdentifiers.
	byStem := map[string]*idx{}
	for i, f := range files {
		if used[i] || !(f.isLiveImage() || f.isLiveVideo()) {
			continue
		}
		key := f.dir() + "/" + strings.ToLower(f.stem())
		g := byStem[key]
		if g == nil {
			g = &idx{}
			byStem[key] = g
		}
		if f.isLiveImage() {
			g.img = append(g.img, i)
		} else {
			g.vid = append(g.vid, i)
		}
	}
	for _, g := range byStem {
		if len(g.img) != 1 || len(g.vid) != 1 {
			continue
		}
		a, b := files[g.img[0]].Info.ContentID, files[g.vid[0]].Info.ContentID
		if a != "" && b != "" && a != b {
			continue
		}
		pairOf[g.img[0]] = g.vid[0]
		used[g.img[0]], used[g.vid[0]] = true, true
	}

	isVideoOfPair := map[int]bool{}
	for _, v := range pairOf {
		isVideoOfPair[v] = true
	}
	// Emit in path order; a pair is emitted at its first member.
	var out []unit
	emitted := map[int]bool{}
	videoToImage := map[int]int{}
	for img, vid := range pairOf {
		videoToImage[vid] = img
	}
	for i := range files {
		if emitted[i] {
			continue
		}
		if v, ok := pairOf[i]; ok {
			out = append(out, unit{files: []*file{files[i], files[v]}, live: true})
			emitted[i], emitted[v] = true, true
			continue
		}
		if isVideoOfPair[i] {
			img := videoToImage[i]
			out = append(out, unit{files: []*file{files[img], files[i]}, live: true})
			emitted[i], emitted[img] = true, true
			continue
		}
		out = append(out, unit{files: []*file{files[i]}})
		emitted[i] = true
	}
	return out
}
