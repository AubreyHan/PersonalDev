package metadata

import (
	"encoding/binary"
	"io"
	"strings"
	"time"
)

const (
	keyContentID    = "com.apple.quicktime.content.identifier"
	keyCreationDate = "com.apple.quicktime.creationdate"
)

// qtFields are the values extracted from a QuickTime / MP4 file.
type qtFields struct {
	CreationDate string    // com.apple.quicktime.creationdate (with offset)
	MvhdCreation time.Time // mvhd creation time (UTC), zero if unset
	ContentID    string
}

var epoch1904 = time.Date(1904, 1, 1, 0, 0, 0, 0, time.UTC)

// readQuickTime parses moov/mvhd and the moov/meta keys+ilst metadata.
func readQuickTime(r io.ReaderAt, size int64) (qtFields, error) {
	var out qtFields
	top, _ := readBoxes(r, 0, size)
	moov, ok := find(top, "moov")
	if !ok {
		return out, errBadBox
	}
	mc, _ := children(r, moov, 0)
	if mvhd, ok := find(mc, "mvhd"); ok {
		out.MvhdCreation = parseMvhd(r, mvhd)
	}
	metas := []box{}
	if m, ok := find(mc, "meta"); ok {
		metas = append(metas, m)
	}
	if udta, ok := find(mc, "udta"); ok {
		uc, _ := children(r, udta, 0)
		if m, ok := find(uc, "meta"); ok {
			metas = append(metas, m)
		}
	}
	for _, m := range metas {
		vals := parseMdta(r, m)
		if out.ContentID == "" {
			out.ContentID = vals[keyContentID]
		}
		if out.CreationDate == "" {
			out.CreationDate = vals[keyCreationDate]
		}
	}
	return out, nil
}

func parseMvhd(r io.ReaderAt, b box) time.Time {
	d, err := readAt(r, b.dataOff, int(min64(b.dataSize, 32)))
	if err != nil || len(d) < 8 {
		return time.Time{}
	}
	var secs uint64
	if d[0] == 1 {
		if len(d) < 12 {
			return time.Time{}
		}
		secs = binary.BigEndian.Uint64(d[4:12])
	} else {
		secs = uint64(binary.BigEndian.Uint32(d[4:8]))
	}
	if secs == 0 {
		return time.Time{}
	}
	return epoch1904.Add(time.Duration(secs) * time.Second)
}

// parseMdta returns the UTF-8 string values of a QuickTime "mdta" meta box.
func parseMdta(r io.ReaderAt, meta box) map[string]string {
	out := map[string]string{}
	// QuickTime meta is a plain box; ISO meta is a FullBox (4 extra bytes).
	skip := int64(4)
	if h, err := readAt(r, meta.dataOff, 8); err == nil && string(h[4:8]) == "hdlr" {
		skip = 0
	}
	mc, _ := children(r, meta, skip)
	keysBox, ok1 := find(mc, "keys")
	ilst, ok2 := find(mc, "ilst")
	if !ok1 || !ok2 {
		return out
	}
	kd, err := readAt(r, keysBox.dataOff, int(min64(keysBox.dataSize, maxRead)))
	if err != nil || len(kd) < 8 {
		return out
	}
	count := binary.BigEndian.Uint32(kd[4:8])
	keys := []string{""} // 1-based
	pos := 8
	for i := uint32(0); i < count && pos+8 <= len(kd); i++ {
		sz := int(binary.BigEndian.Uint32(kd[pos : pos+4]))
		if sz < 8 || pos+sz > len(kd) {
			break
		}
		keys = append(keys, string(kd[pos+8:pos+sz]))
		pos += sz
	}
	items, _ := children(r, ilst, 0)
	for _, it := range items {
		idx := binary.BigEndian.Uint32([]byte(it.typ))
		if idx == 0 || int(idx) >= len(keys) {
			continue
		}
		dc, _ := children(r, it, 0)
		data, ok := find(dc, "data")
		if !ok || data.dataSize < 8 || data.dataSize > 4096 {
			continue
		}
		d, err := readAt(r, data.dataOff, int(data.dataSize))
		if err != nil {
			continue
		}
		if binary.BigEndian.Uint32(d[0:4])&0xFFFFFF != 1 { // UTF-8
			continue
		}
		out[keys[idx]] = strings.TrimRight(string(d[8:]), "\x00")
	}
	return out
}
