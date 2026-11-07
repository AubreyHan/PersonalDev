package metadata

import (
	"encoding/binary"
	"encoding/hex"
	"errors"
	"io"
)

var errBadBox = errors.New("metadata: invalid ISO-BMFF structure")

// box is an ISO-BMFF / QuickTime box.
type box struct {
	typ      string
	uuid     string // hex user type for "uuid" boxes
	start    int64  // offset of the box header
	dataOff  int64  // offset of the payload
	dataSize int64
}

// readBoxes lists the boxes in [start, end).
func readBoxes(r io.ReaderAt, start, end int64) ([]box, error) {
	var out []box
	off := start
	for off+8 <= end && len(out) < 4096 {
		h, err := readAt(r, off, 8)
		if err != nil {
			return out, err
		}
		size := int64(binary.BigEndian.Uint32(h[0:4]))
		b := box{typ: string(h[4:8]), start: off}
		hdr := int64(8)
		switch size {
		case 1:
			ls, err := readAt(r, off+8, 8)
			if err != nil {
				return out, err
			}
			size = int64(binary.BigEndian.Uint64(ls))
			hdr = 16
		case 0:
			size = end - off
		}
		if size < hdr || off+size > end {
			return out, errBadBox
		}
		if b.typ == "uuid" {
			u, err := readAt(r, off+hdr, 16)
			if err != nil {
				return out, err
			}
			b.uuid = hex.EncodeToString(u)
			hdr += 16
		}
		b.dataOff = off + hdr
		b.dataSize = size - hdr
		out = append(out, b)
		off += size
	}
	return out, nil
}

func children(r io.ReaderAt, b box, skip int64) ([]box, error) {
	return readBoxes(r, b.dataOff+skip, b.dataOff+b.dataSize)
}

func find(bs []box, typ string) (box, bool) {
	for _, b := range bs {
		if b.typ == typ {
			return b, true
		}
	}
	return box{}, false
}

// ftypBrand returns the major brand of an ISO-BMFF file.
func ftypBrand(r io.ReaderAt) string {
	h, err := readAt(r, 0, 12)
	if err != nil || string(h[4:8]) != "ftyp" {
		return ""
	}
	return string(h[8:12])
}

// readHEIFExif reads the "Exif" item of a HEIC/HEIF file.
func readHEIFExif(r io.ReaderAt, size int64) (exifFields, error) {
	top, _ := readBoxes(r, 0, size)
	meta, ok := find(top, "meta")
	if !ok {
		return exifFields{}, errNoExif
	}
	mc, err := children(r, meta, 4)
	if err != nil && len(mc) == 0 {
		return exifFields{}, err
	}
	iinf, ok1 := find(mc, "iinf")
	iloc, ok2 := find(mc, "iloc")
	if !ok1 || !ok2 {
		return exifFields{}, errNoExif
	}
	id, err := exifItemID(r, iinf)
	if err != nil {
		return exifFields{}, err
	}
	idatOff := int64(-1)
	if idat, ok := find(mc, "idat"); ok {
		idatOff = idat.dataOff
	}
	off, _, err := itemLocation(r, iloc, id, idatOff)
	if err != nil {
		return exifFields{}, err
	}
	p, err := readAt(r, off, 4)
	if err != nil {
		return exifFields{}, err
	}
	tiffOff := off + 4 + int64(binary.BigEndian.Uint32(p))
	return readTIFFExif(r, tiffOff)
}

func exifItemID(r io.ReaderAt, iinf box) (uint32, error) {
	h, err := readAt(r, iinf.dataOff, 4)
	if err != nil {
		return 0, err
	}
	skip := int64(4 + 2)
	if h[0] != 0 {
		skip = 4 + 4
	}
	infes, _ := children(r, iinf, skip)
	for _, b := range infes {
		if b.typ != "infe" {
			continue
		}
		d, err := readAt(r, b.dataOff, int(min64(b.dataSize, 64)))
		if err != nil || len(d) < 4 {
			continue
		}
		ver := d[0]
		var id uint32
		var typ string
		switch {
		case ver == 2 && len(d) >= 12:
			id = uint32(binary.BigEndian.Uint16(d[4:6]))
			typ = string(d[8:12])
		case ver == 3 && len(d) >= 14:
			id = binary.BigEndian.Uint32(d[4:8])
			typ = string(d[10:14])
		default:
			continue
		}
		if typ == "Exif" {
			return id, nil
		}
	}
	return 0, errNoExif
}

// itemLocation returns the absolute offset and length of an item's first extent.
func itemLocation(r io.ReaderAt, iloc box, id uint32, idatOff int64) (int64, int64, error) {
	n := min64(iloc.dataSize, maxRead)
	d, err := readAt(r, iloc.dataOff, int(n))
	if err != nil {
		return 0, 0, err
	}
	p := &cursor{b: d}
	ver := p.u8()
	p.skip(3)
	s1, s2 := p.u8(), p.u8()
	offSize, lenSize, baseSize := int(s1>>4), int(s1&0xF), int(s2>>4)
	idxSize := 0
	if ver == 1 || ver == 2 {
		idxSize = int(s2 & 0xF)
	}
	var count uint32
	if ver < 2 {
		count = uint32(p.uN(2))
	} else {
		count = uint32(p.uN(4))
	}
	for i := uint32(0); i < count && !p.bad; i++ {
		var itemID uint32
		if ver < 2 {
			itemID = uint32(p.uN(2))
		} else {
			itemID = uint32(p.uN(4))
		}
		method := uint64(0)
		if ver == 1 || ver == 2 {
			method = p.uN(2) & 0xF
		}
		p.skip(2) // data_reference_index
		base := p.uN(baseSize)
		extents := int(p.uN(2))
		var firstOff, firstLen uint64
		for e := 0; e < extents && !p.bad; e++ {
			p.uN(idxSize)
			eo, el := p.uN(offSize), p.uN(lenSize)
			if e == 0 {
				firstOff, firstLen = eo, el
			}
		}
		if itemID == id && !p.bad {
			abs := int64(base + firstOff)
			if method == 1 {
				if idatOff < 0 {
					return 0, 0, errBadBox
				}
				abs += idatOff
			} else if method != 0 {
				return 0, 0, errBadBox
			}
			return abs, int64(firstLen), nil
		}
	}
	return 0, 0, errNoExif
}

// readCR3Exif reads the CMT2 (Exif IFD) block of a Canon CR3 file.
func readCR3Exif(r io.ReaderAt, size int64) (exifFields, error) {
	const canonUUID = "85c0b687820f11e08111f4ce462b6a48"
	top, _ := readBoxes(r, 0, size)
	moov, ok := find(top, "moov")
	if !ok {
		return exifFields{}, errNoExif
	}
	mc, _ := children(r, moov, 0)
	var out exifFields
	for _, b := range mc {
		if b.typ != "uuid" || b.uuid != canonUUID {
			continue
		}
		cc, _ := children(r, b, 0)
		for _, name := range []string{"CMT2", "CMT1"} {
			if c, ok := find(cc, name); ok {
				if f, err := readTIFFExif(r, c.dataOff); err == nil {
					out.merge(f)
				}
			}
		}
	}
	if !out.hasDate() {
		return out, errNoExif
	}
	return out, nil
}

// cursor is a bounds-checked big-endian reader over a byte slice.
type cursor struct {
	b   []byte
	pos int
	bad bool
}

func (c *cursor) skip(n int) {
	if c.pos+n > len(c.b) {
		c.bad = true
		return
	}
	c.pos += n
}

func (c *cursor) u8() byte { return byte(c.uN(1)) }

func (c *cursor) uN(n int) uint64 {
	if n == 0 {
		return 0
	}
	if n > 8 || c.pos+n > len(c.b) {
		c.bad = true
		return 0
	}
	var v uint64
	for _, x := range c.b[c.pos : c.pos+n] {
		v = v<<8 | uint64(x)
	}
	c.pos += n
	return v
}

func min64(a, b int64) int64 {
	if a < b {
		return a
	}
	return b
}
