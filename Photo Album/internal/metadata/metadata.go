// Package metadata extracts capture time and the Apple Live Photo
// ContentIdentifier from images, RAW files and videos (ARCHITECTURE §5.5–5.6),
// using only the standard library.
package metadata

import (
	"bytes"
	"os"
	"strings"
	"time"

	"photo-album/internal/walk"
)

// Info is the extracted metadata. Taken is zero when no usable capture time
// was found (callers then fall back to the file modification time).
type Info struct {
	Taken     time.Time
	ContentID string
}

// now is replaceable in tests.
var now = time.Now

// Read extracts metadata from path. loc is the NAS time zone, used for EXIF
// times without an offset. Errors only indicate unreadable files; unknown or
// metadata-less formats yield an empty Info and nil error.
func Read(path string, loc *time.Location) (Info, error) {
	f, err := os.Open(path)
	if err != nil {
		return Info{}, err
	}
	defer f.Close()
	st, err := f.Stat()
	if err != nil {
		return Info{}, err
	}
	size := st.Size()
	head := make([]byte, 16)
	n, _ := f.ReadAt(head, 0)
	head = head[:n]

	if walk.KindOf(path) == walk.KindVideo || isQuickTime(head) && ftypBrand(f) != "crx " && !isHEIF(ftypBrand(f)) {
		q, err := readQuickTime(f, size)
		if err != nil {
			return Info{}, nil
		}
		return Info{Taken: videoTime(q), ContentID: q.ContentID}, nil
	}

	var fields exifFields
	switch {
	case bytes.HasPrefix(head, []byte{0xFF, 0xD8}):
		fields, err = readJPEGExif(f)
	case bytes.HasPrefix(head, []byte("\x89PNG\r\n\x1a\n")):
		fields, err = readPNGExif(f)
	case len(head) >= 12 && string(head[0:4]) == "RIFF" && string(head[8:12]) == "WEBP":
		fields, err = readWebPExif(f)
	case bytes.HasPrefix(head, []byte("FUJIFILMCCD-RAW")):
		fields, err = readRAFExif(f)
	case bytes.HasPrefix(head, []byte("II")) || bytes.HasPrefix(head, []byte("MM")):
		fields, err = readTIFFExif(f, 0)
	case len(head) >= 8 && string(head[4:8]) == "ftyp":
		brand := ftypBrand(f)
		if brand == "crx " {
			fields, err = readCR3Exif(f, size)
		} else {
			fields, err = readHEIFExif(f, size)
		}
	}
	if err != nil {
		return Info{}, nil
	}
	return Info{Taken: imageTime(fields, loc), ContentID: fields.ContentID}, nil
}

func isQuickTime(head []byte) bool {
	if len(head) < 8 {
		return false
	}
	switch string(head[4:8]) {
	case "ftyp", "moov", "mdat", "wide", "free", "skip":
		return true
	}
	return false
}

func isHEIF(brand string) bool {
	switch brand {
	case "heic", "heix", "heim", "heis", "hevc", "hevx", "mif1", "msf1", "avif":
		return true
	}
	return false
}

// imageTime applies the EXIF priority: DateTimeOriginal (+OffsetTimeOriginal),
// then CreateDate (+OffsetTimeDigitized).
func imageTime(f exifFields, loc *time.Location) time.Time {
	if t, ok := parseExifTime(f.DateTimeOriginal, f.OffsetOriginal, loc); ok {
		return t
	}
	if t, ok := parseExifTime(f.CreateDate, f.OffsetDigitized, loc); ok {
		return t
	}
	return time.Time{}
}

// videoTime applies the video priority: Apple creationdate (with offset),
// then mvhd creation time (UTC).
func videoTime(q qtFields) time.Time {
	if t, ok := parseISOTime(q.CreationDate); ok {
		return t
	}
	if valid(q.MvhdCreation) {
		return q.MvhdCreation
	}
	return time.Time{}
}

func parseExifTime(s, offset string, loc *time.Location) (time.Time, bool) {
	s = strings.TrimSpace(s)
	if len(s) < 19 {
		return time.Time{}, false
	}
	s = s[:19]
	zone := loc
	if o, ok := parseOffset(offset); ok {
		zone = o
	}
	t, err := time.ParseInLocation("2006:01:02 15:04:05", s, zone)
	if err != nil {
		// Some writers use dashes in the date part.
		t, err = time.ParseInLocation("2006-01-02 15:04:05", s, zone)
		if err != nil {
			return time.Time{}, false
		}
	}
	return t, valid(t)
}

func parseOffset(s string) (*time.Location, bool) {
	s = strings.TrimSpace(s)
	if s == "" {
		return nil, false
	}
	t, err := time.Parse("-07:00", s)
	if err != nil {
		return nil, false
	}
	_, off := t.Zone()
	return time.FixedZone(s, off), true
}

func parseISOTime(s string) (time.Time, bool) {
	s = strings.TrimSpace(s)
	for _, layout := range []string{"2006-01-02T15:04:05-0700", "2006-01-02T15:04:05Z07:00", "2006-01-02T15:04:05.000-0700"} {
		if t, err := time.Parse(layout, s); err == nil && valid(t) {
			return t, true
		}
	}
	return time.Time{}, false
}

// valid rejects zero, pre-1970 and future (> now+1 day) times.
func valid(t time.Time) bool {
	if t.IsZero() || t.Before(time.Unix(0, 0)) {
		return false
	}
	return !t.After(now().Add(24 * time.Hour))
}
