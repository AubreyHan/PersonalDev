package metadata

import (
	"bytes"
	"encoding/binary"
	"encoding/hex"
	"os"
	"path/filepath"
	"testing"
	"time"
)

// ---- synthetic file builders ----

type tg struct {
	id, typ uint16
	count   uint32
	data    []byte
}

func asciiTag(id uint16, s string) tg {
	return tg{id: id, typ: 2, count: uint32(len(s) + 1), data: append([]byte(s), 0)}
}

func longTag(id uint16, v uint32) tg {
	b := make([]byte, 4)
	binary.BigEndian.PutUint32(b, v)
	return tg{id: id, typ: 4, count: 1, data: b}
}

func undefTag(id uint16, b []byte) tg { return tg{id: id, typ: 7, count: uint32(len(b)), data: b} }

// ifdBytes lays out an IFD at offset start followed by its data area.
func ifdBytes(start uint32, tags []tg) []byte {
	be := binary.BigEndian
	hdr := uint32(2 + 12*len(tags) + 4)
	var ents, data bytes.Buffer
	binary.Write(&ents, be, uint16(len(tags)))
	for _, t := range tags {
		binary.Write(&ents, be, t.id)
		binary.Write(&ents, be, t.typ)
		binary.Write(&ents, be, t.count)
		if len(t.data) <= 4 {
			v := make([]byte, 4)
			copy(v, t.data)
			ents.Write(v)
		} else {
			binary.Write(&ents, be, start+hdr+uint32(data.Len()))
			data.Write(t.data)
			if data.Len()%2 == 1 {
				data.WriteByte(0)
			}
		}
	}
	binary.Write(&ents, be, uint32(0))
	return append(ents.Bytes(), data.Bytes()...)
}

// buildTIFF returns a big-endian TIFF with IFD0 and an optional Exif IFD.
func buildTIFF(ifd0, exif []tg) []byte {
	head := []byte{'M', 'M', 0, 42, 0, 0, 0, 8}
	if len(exif) == 0 {
		return append(head, ifdBytes(8, ifd0)...)
	}
	with := append(append([]tg{}, ifd0...), longTag(tagExifIFD, 0))
	first := ifdBytes(8, with)
	exifStart := 8 + uint32(len(first))
	with[len(with)-1] = longTag(tagExifIFD, exifStart)
	first = ifdBytes(8, with)
	out := append(head, first...)
	return append(out, ifdBytes(exifStart, exif)...)
}

func appleMakerNote(cid string) []byte {
	h := []byte("Apple iOS\x00\x00\x01MM")
	return append(h, ifdBytes(14, []tg{longTag(0x0001, 14), asciiTag(appleTagContentIdent, cid)})...)
}

func buildJPEG(tiff []byte) []byte {
	var b bytes.Buffer
	b.Write([]byte{0xFF, 0xD8, 0xFF, 0xE0, 0, 16})
	b.Write([]byte("JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00"))
	b.Write([]byte{0xFF, 0xE1})
	binary.Write(&b, binary.BigEndian, uint16(2+6+len(tiff)))
	b.WriteString("Exif\x00\x00")
	b.Write(tiff)
	b.Write([]byte{0xFF, 0xDA, 0, 2, 0xFF, 0xD9})
	return b.Bytes()
}

func bx(typ string, parts ...[]byte) []byte {
	payload := bytes.Join(parts, nil)
	out := make([]byte, 8, 8+len(payload))
	binary.BigEndian.PutUint32(out, uint32(8+len(payload)))
	copy(out[4:], typ)
	return append(out, payload...)
}

func u16(v uint16) []byte { b := make([]byte, 2); binary.BigEndian.PutUint16(b, v); return b }
func u32(v uint32) []byte { b := make([]byte, 4); binary.BigEndian.PutUint32(b, v); return b }

func buildHEIC(tiff []byte) []byte {
	ftyp := bx("ftyp", []byte("heic"), u32(0), []byte("mif1heic"))
	hdlr := bx("hdlr", u32(0), u32(0), []byte("pict"), make([]byte, 13))
	infe := bx("infe", []byte{2, 0, 0, 0}, u16(1), u16(0), []byte("Exif"), []byte{0})
	iinf := bx("iinf", u32(0), u16(1), infe)
	payload := append(append(u32(6), []byte("Exif\x00\x00")...), tiff...)
	mkMeta := func(off uint32) []byte {
		iloc := bx("iloc", u32(0), []byte{0x44, 0x00}, u16(1), u16(1), u16(0), u16(1), u32(off), u32(uint32(len(payload))))
		return bx("meta", u32(0), hdlr, iinf, iloc)
	}
	meta := mkMeta(0)
	off := uint32(len(ftyp) + len(meta) + 8)
	meta = mkMeta(off)
	return bytes.Join([][]byte{ftyp, meta, bx("mdat", payload)}, nil)
}

func buildMOV(mvhdSecs uint32, kv map[string]string) []byte {
	mvhd := bx("mvhd", u32(0), u32(mvhdSecs), u32(mvhdSecs), make([]byte, 88))
	parts := [][]byte{mvhd}
	if len(kv) > 0 {
		var keys, ilst [][]byte
		i := uint32(0)
		for k, v := range kv {
			i++
			keys = append(keys, append(append(u32(uint32(8+len(k))), []byte("mdta")...), []byte(k)...))
			data := bx("data", u32(1), u32(0), []byte(v))
			ilst = append(ilst, bx(string(u32(i)), data))
		}
		keysBox := bx("keys", u32(0), u32(i), bytes.Join(keys, nil))
		hdlr := bx("hdlr", u32(0), u32(0), []byte("mdta"), make([]byte, 13))
		parts = append(parts, bx("meta", hdlr, keysBox, bx("ilst", ilst...)))
	}
	return append(bx("ftyp", []byte("qt  "), u32(0), []byte("qt  ")), bx("moov", parts...)...)
}

func buildCR3(cmt2 []byte) []byte {
	uuid, _ := hex.DecodeString("85c0b687820f11e08111f4ce462b6a48")
	cmt1 := buildTIFF([]tg{asciiTag(0x010F, "Canon")}, nil)
	canon := bx("uuid", uuid, bx("CMT1", cmt1), bx("CMT2", cmt2))
	return append(bx("ftyp", []byte("crx "), u32(1), []byte("crx isom")), bx("moov", canon)...)
}

func write(t *testing.T, name string, b []byte) string {
	t.Helper()
	p := filepath.Join(t.TempDir(), name)
	if err := os.WriteFile(p, b, 0o644); err != nil {
		t.Fatal(err)
	}
	return p
}

var shanghai = time.FixedZone("CST", 8*3600)

// ---- tests ----

func TestJPEGWithOffsetAndAppleCID(t *testing.T) {
	cid := "A1B2C3D4-0000-1111-2222-333344445555"
	tiff := buildTIFF([]tg{asciiTag(0x010F, "Apple")}, []tg{
		asciiTag(tagDateTimeOriginal, "2026:11:07 00:30:00"),
		asciiTag(tagOffsetTimeOrig, "+09:00"),
		undefTag(tagMakerNote, appleMakerNote(cid)),
	})
	info, err := Read(write(t, "IMG_0001.JPG", buildJPEG(tiff)), shanghai)
	if err != nil {
		t.Fatal(err)
	}
	want := time.Date(2026, 11, 6, 15, 30, 0, 0, time.UTC)
	if !info.Taken.Equal(want) {
		t.Fatalf("taken=%v want %v", info.Taken, want)
	}
	if got := info.Taken.In(shanghai).Format("2006-01-02"); got != "2026-11-06" {
		t.Fatalf("local date %s", got)
	}
	if info.ContentID != cid {
		t.Fatalf("cid=%q", info.ContentID)
	}
}

func TestJPEGWithoutOffsetUsesNASZone(t *testing.T) {
	tiff := buildTIFF(nil, []tg{asciiTag(tagDateTimeOriginal, "2026:01:02 23:59:59")})
	info, _ := Read(write(t, "a.jpeg", buildJPEG(tiff)), shanghai)
	if got := info.Taken.In(shanghai).Format("2006-01-02 15:04:05"); got != "2026-01-02 23:59:59" {
		t.Fatalf("got %s", got)
	}
}

func TestCreateDateFallbackAndFutureRejected(t *testing.T) {
	tiff := buildTIFF(nil, []tg{
		asciiTag(tagDateTimeOriginal, "2999:01:01 00:00:00"),
		asciiTag(tagCreateDate, "2020:05:06 07:08:09"),
	})
	info, _ := Read(write(t, "a.dng", tiff), shanghai)
	if got := info.Taken.In(shanghai).Format("2006-01-02"); got != "2020-05-06" {
		t.Fatalf("got %s", got)
	}
}

func TestInvalidDateYieldsZero(t *testing.T) {
	tiff := buildTIFF(nil, []tg{asciiTag(tagDateTimeOriginal, "0000:00:00 00:00:00")})
	info, _ := Read(write(t, "a.nef", tiff), shanghai)
	if !info.Taken.IsZero() {
		t.Fatalf("got %v", info.Taken)
	}
}

func TestHEIC(t *testing.T) {
	cid := "11112222-3333-4444-5555-666677778888"
	tiff := buildTIFF([]tg{asciiTag(0x010F, "Apple")}, []tg{
		asciiTag(tagDateTimeOriginal, "2025:12:31 23:00:00"),
		asciiTag(tagOffsetTimeOrig, "+08:00"),
		undefTag(tagMakerNote, appleMakerNote(cid)),
	})
	info, err := Read(write(t, "IMG_0002.HEIC", buildHEIC(tiff)), shanghai)
	if err != nil {
		t.Fatal(err)
	}
	if got := info.Taken.In(shanghai).Format("2006-01-02 15:04"); got != "2025-12-31 23:00" {
		t.Fatalf("taken %s", got)
	}
	if info.ContentID != cid {
		t.Fatalf("cid=%q", info.ContentID)
	}
}

func TestMOVAppleCreationDateAndCID(t *testing.T) {
	cid := "11112222-3333-4444-5555-666677778888"
	mov := buildMOV(100, map[string]string{
		keyContentID:    cid,
		keyCreationDate: "2025-12-31T23:00:05+0800",
	})
	info, err := Read(write(t, "IMG_0002.MOV", mov), shanghai)
	if err != nil {
		t.Fatal(err)
	}
	if got := info.Taken.In(shanghai).Format("2006-01-02 15:04:05"); got != "2025-12-31 23:00:05" {
		t.Fatalf("taken %s", got)
	}
	if info.ContentID != cid {
		t.Fatalf("cid=%q", info.ContentID)
	}
}

func TestMP4MvhdUTCConvertedAcrossMidnight(t *testing.T) {
	// 2026-11-06 20:00:00 UTC == 2026-11-07 04:00 in UTC+8.
	utc := time.Date(2026, 11, 6, 20, 0, 0, 0, time.UTC)
	secs := uint32(utc.Sub(epoch1904) / time.Second)
	info, _ := Read(write(t, "clip.mp4", buildMOV(secs, nil)), shanghai)
	if got := info.Taken.In(shanghai).Format("2006-01-02"); got != "2026-11-07" {
		t.Fatalf("got %s (%v)", got, info.Taken)
	}
	infoM4V, _ := Read(write(t, "clip.M4V", buildMOV(secs, nil)), shanghai)
	if got := infoM4V.Taken.In(shanghai).Format("2006-01-02"); got != "2026-11-07" {
		t.Fatalf("m4v got %s (%v)", got, infoM4V.Taken)
	}
}

func TestCR3(t *testing.T) {
	// CMT2 is an Exif IFD stored as IFD0 of its own TIFF.
	cmt2 := buildTIFF([]tg{asciiTag(tagDateTimeOriginal, "2024:03:04 05:06:07")}, nil)
	info, _ := Read(write(t, "IMG_1.CR3", buildCR3(cmt2)), shanghai)
	if got := info.Taken.In(shanghai).Format("2006-01-02"); got != "2024-03-04" {
		t.Fatalf("got %v", info.Taken)
	}
}

func TestPNGWithoutExifAndGarbage(t *testing.T) {
	png := []byte("\x89PNG\r\n\x1a\n\x00\x00\x00\x00IEND\xaeB`\x82")
	if info, err := Read(write(t, "a.png", png), shanghai); err != nil || !info.Taken.IsZero() {
		t.Fatal(info, err)
	}
	if info, err := Read(write(t, "b.jpg", []byte("not an image")), shanghai); err != nil || !info.Taken.IsZero() {
		t.Fatal(info, err)
	}
	if info, err := Read(write(t, "c.mov", []byte{0, 0, 0, 1}), shanghai); err != nil || !info.Taken.IsZero() {
		t.Fatal(info, err)
	}
}
