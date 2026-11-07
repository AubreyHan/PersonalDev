package metadata

import (
	"bytes"
	"encoding/binary"
	"io"
	"strings"
	"unicode"
)

// EXIF / TIFF tags used.
const (
	tagExifIFD           = 0x8769
	tagDateTimeOriginal  = 0x9003
	tagCreateDate        = 0x9004
	tagOffsetTimeOrig    = 0x9011
	tagOffsetTimeDigit   = 0x9012
	tagMakerNote         = 0x927C
	tagRW2JpgFromRaw     = 0x002E
	appleTagContentIdent = 0x0011
)

// exifFields are the raw values we care about.
type exifFields struct {
	DateTimeOriginal string
	OffsetOriginal   string
	CreateDate       string
	OffsetDigitized  string
	ContentID        string
}

func (f *exifFields) merge(o exifFields) {
	if f.DateTimeOriginal == "" {
		f.DateTimeOriginal, f.OffsetOriginal = o.DateTimeOriginal, o.OffsetOriginal
	}
	if f.CreateDate == "" {
		f.CreateDate, f.OffsetDigitized = o.CreateDate, o.OffsetDigitized
	}
	if f.ContentID == "" {
		f.ContentID = o.ContentID
	}
}

func (f exifFields) hasDate() bool { return f.DateTimeOriginal != "" || f.CreateDate != "" }

// readTIFFExif extracts exif fields from a TIFF stream at base. It looks at
// both IFD0 and the Exif sub-IFD (CR3's CMT2 block is itself an Exif IFD),
// and follows RW2's embedded JpgFromRaw when no date is found.
func readTIFFExif(r io.ReaderAt, base int64) (exifFields, error) {
	var out exifFields
	t, first, err := newTIFF(r, base)
	if err != nil {
		return out, err
	}
	ifd0, err := t.readIFD(first)
	if err != nil {
		return out, err
	}
	collect := func(ifd map[uint16]ifdEntry) {
		var f exifFields
		if e, ok := ifd[tagDateTimeOriginal]; ok {
			f.DateTimeOriginal = t.ascii(e)
		}
		if e, ok := ifd[tagOffsetTimeOrig]; ok {
			f.OffsetOriginal = t.ascii(e)
		}
		if e, ok := ifd[tagCreateDate]; ok {
			f.CreateDate = t.ascii(e)
		}
		if e, ok := ifd[tagOffsetTimeDigit]; ok {
			f.OffsetDigitized = t.ascii(e)
		}
		if e, ok := ifd[tagMakerNote]; ok {
			f.ContentID = appleContentID(t, e)
		}
		out.merge(f)
	}
	collect(ifd0)
	if e, ok := ifd0[tagExifIFD]; ok {
		if off, ok := t.long(e); ok && off != 0 {
			if exif, err := t.readIFD(off); err == nil {
				collect(exif)
			}
		}
	}
	if !out.hasDate() {
		if e, ok := ifd0[tagRW2JpgFromRaw]; ok && (e.typ == 7 || e.typ == 1) && e.count > 4 {
			sec := io.NewSectionReader(t.r, t.base+int64(t.valueOffset(e)), int64(e.count))
			if f, err := readJPEGExif(sec); err == nil {
				out.merge(f)
			}
		}
	}
	return out, nil
}

// appleContentID parses an Apple iOS MakerNote and returns tag 0x0011
// (ContentIdentifier, shared by both halves of a Live Photo).
//
// Layout: "Apple iOS\0" + 2-byte version + "MM"/"II", IFD at offset 14,
// value offsets relative to the start of the MakerNote.
func appleContentID(t *tiff, mn ifdEntry) string {
	if mn.count < 16 {
		return ""
	}
	mnOff := t.base + int64(t.valueOffset(mn))
	hdr, err := readAt(t.r, mnOff, 14)
	if err != nil || !bytes.HasPrefix(hdr, []byte("Apple iOS")) {
		return ""
	}
	var bo binary.ByteOrder = binary.BigEndian
	if string(hdr[12:14]) == "II" {
		bo = binary.LittleEndian
	}
	sub := &tiff{r: t.r, base: mnOff, bo: bo}
	ifd, err := sub.readIFD(14)
	if err != nil {
		return ""
	}
	e, ok := ifd[appleTagContentIdent]
	if !ok {
		return ""
	}
	if s := sub.ascii(e); plausibleID(s) {
		return s
	}
	// Fallback: some writers use offsets relative to the TIFF header.
	alt := &tiff{r: t.r, base: t.base, bo: bo}
	if s := alt.ascii(e); plausibleID(s) {
		return s
	}
	return ""
}

func plausibleID(s string) bool {
	if len(s) < 8 || len(s) > 128 {
		return false
	}
	for _, c := range s {
		if c > unicode.MaxASCII || !(unicode.IsLetter(c) || unicode.IsDigit(c) || strings.ContainsRune("-_", c)) {
			return false
		}
	}
	return true
}
