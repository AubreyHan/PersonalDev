package metadata

import (
	"encoding/binary"
	"errors"
	"io"
	"strings"
)

var errBadTIFF = errors.New("metadata: invalid TIFF structure")

// maxRead bounds any single read from a file to keep corrupt input harmless.
const maxRead = 1 << 20

// readAt reads exactly n bytes at off from r.
func readAt(r io.ReaderAt, off int64, n int) ([]byte, error) {
	if n < 0 || n > maxRead || off < 0 {
		return nil, errBadTIFF
	}
	b := make([]byte, n)
	if _, err := r.ReadAt(b, off); err != nil {
		return nil, err
	}
	return b, nil
}

// tiff is a lazily-read TIFF structure. All offsets are relative to base.
type tiff struct {
	r    io.ReaderAt
	base int64
	bo   binary.ByteOrder
}

type ifdEntry struct {
	tag   uint16
	typ   uint16
	count uint32
	raw   [4]byte // value or offset field
}

var typeSize = map[uint16]int{1: 1, 2: 1, 3: 2, 4: 4, 5: 8, 6: 1, 7: 1, 8: 2, 9: 4, 10: 8, 11: 4, 12: 8, 13: 4}

// newTIFF parses a TIFF header at base. Besides the standard magic 42 it
// accepts the RAW variants used by Panasonic RW2 (0x55) and Olympus ORF
// ("RO"/"SR").
func newTIFF(r io.ReaderAt, base int64) (*tiff, uint32, error) {
	h, err := readAt(r, base, 8)
	if err != nil {
		return nil, 0, err
	}
	var bo binary.ByteOrder
	switch string(h[:2]) {
	case "II":
		bo = binary.LittleEndian
	case "MM":
		bo = binary.BigEndian
	default:
		return nil, 0, errBadTIFF
	}
	switch bo.Uint16(h[2:4]) {
	case 42, 0x55, 0x4F52, 0x5352:
	default:
		return nil, 0, errBadTIFF
	}
	return &tiff{r: r, base: base, bo: bo}, bo.Uint32(h[4:8]), nil
}

// readIFD reads the IFD at off and returns its entries keyed by tag.
func (t *tiff) readIFD(off uint32) (map[uint16]ifdEntry, error) {
	cb, err := readAt(t.r, t.base+int64(off), 2)
	if err != nil {
		return nil, err
	}
	n := int(t.bo.Uint16(cb))
	if n == 0 || n > 1000 {
		return nil, errBadTIFF
	}
	b, err := readAt(t.r, t.base+int64(off)+2, n*12)
	if err != nil {
		return nil, err
	}
	m := make(map[uint16]ifdEntry, n)
	for i := 0; i < n; i++ {
		e := b[i*12 : i*12+12]
		var ent ifdEntry
		ent.tag = t.bo.Uint16(e[0:2])
		ent.typ = t.bo.Uint16(e[2:4])
		ent.count = t.bo.Uint32(e[4:8])
		copy(ent.raw[:], e[8:12])
		if _, dup := m[ent.tag]; !dup {
			m[ent.tag] = ent
		}
	}
	return m, nil
}

// dataSize returns the byte size of an entry's value.
func dataSize(e ifdEntry) (int64, bool) {
	sz, ok := typeSize[e.typ]
	if !ok {
		return 0, false
	}
	return int64(sz) * int64(e.count), true
}

// valueOffset returns the offset (relative to base) of a non-inline value.
func (t *tiff) valueOffset(e ifdEntry) uint32 { return t.bo.Uint32(e.raw[:]) }

// bytes returns an entry's value bytes.
func (t *tiff) bytes(e ifdEntry) ([]byte, error) {
	n, ok := dataSize(e)
	if !ok || n > maxRead {
		return nil, errBadTIFF
	}
	if n <= 4 {
		return e.raw[:n], nil
	}
	return readAt(t.r, t.base+int64(t.valueOffset(e)), int(n))
}

// ascii returns an ASCII entry as a trimmed string.
func (t *tiff) ascii(e ifdEntry) string {
	if e.typ != 2 && e.typ != 7 && e.typ != 1 {
		return ""
	}
	b, err := t.bytes(e)
	if err != nil {
		return ""
	}
	if i := strings.IndexByte(string(b), 0); i >= 0 {
		b = b[:i]
	}
	return strings.TrimSpace(string(b))
}

// long returns a SHORT/LONG/IFD entry's first value.
func (t *tiff) long(e ifdEntry) (uint32, bool) {
	switch e.typ {
	case 3:
		return uint32(t.bo.Uint16(e.raw[0:2])), true
	case 4, 9, 13:
		return t.bo.Uint32(e.raw[:]), true
	}
	return 0, false
}
