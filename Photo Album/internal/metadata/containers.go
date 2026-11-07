package metadata

import (
	"bytes"
	"encoding/binary"
	"errors"
	"io"
)

var errNoExif = errors.New("metadata: no EXIF found")

// readJPEGExif finds the APP1 "Exif" segment of a JPEG stream.
func readJPEGExif(r io.ReaderAt) (exifFields, error) {
	h, err := readAt(r, 0, 2)
	if err != nil || h[0] != 0xFF || h[1] != 0xD8 {
		return exifFields{}, errNoExif
	}
	off := int64(2)
	for i := 0; i < 256; i++ {
		seg, err := readAt(r, off, 4)
		if err != nil {
			return exifFields{}, errNoExif
		}
		if seg[0] != 0xFF {
			return exifFields{}, errNoExif
		}
		marker := seg[1]
		if marker == 0xFF { // fill byte
			off++
			continue
		}
		if marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7) || marker == 0x01 {
			off += 2
			continue
		}
		if marker == 0xDA || marker == 0xD9 { // start of scan / end
			return exifFields{}, errNoExif
		}
		length := int64(binary.BigEndian.Uint16(seg[2:4]))
		if length < 2 {
			return exifFields{}, errNoExif
		}
		if marker == 0xE1 && length >= 8 {
			id, err := readAt(r, off+4, 6)
			if err == nil && bytes.Equal(id, []byte("Exif\x00\x00")) {
				return readTIFFExif(r, off+10)
			}
		}
		off += 2 + length
	}
	return exifFields{}, errNoExif
}

// readPNGExif reads the eXIf chunk of a PNG.
func readPNGExif(r io.ReaderAt) (exifFields, error) {
	off := int64(8)
	for i := 0; i < 10000; i++ {
		h, err := readAt(r, off, 8)
		if err != nil {
			return exifFields{}, errNoExif
		}
		n := int64(binary.BigEndian.Uint32(h[0:4]))
		typ := string(h[4:8])
		if typ == "eXIf" {
			return readTIFFExif(r, off+8)
		}
		if typ == "IEND" {
			break
		}
		off += 12 + n
	}
	return exifFields{}, errNoExif
}

// readWebPExif reads the EXIF chunk of a WebP (RIFF) file.
func readWebPExif(r io.ReaderAt) (exifFields, error) {
	off := int64(12)
	for i := 0; i < 10000; i++ {
		h, err := readAt(r, off, 8)
		if err != nil {
			return exifFields{}, errNoExif
		}
		n := int64(binary.LittleEndian.Uint32(h[4:8]))
		if string(h[0:4]) == "EXIF" {
			start := off + 8
			if p, err := readAt(r, start, 6); err == nil && bytes.Equal(p, []byte("Exif\x00\x00")) {
				start += 6
			}
			return readTIFFExif(r, start)
		}
		off += 8 + n + n%2
	}
	return exifFields{}, errNoExif
}

// readRAFExif reads the JPEG preview embedded in a Fujifilm RAF file.
func readRAFExif(r io.ReaderAt) (exifFields, error) {
	h, err := readAt(r, 84, 8)
	if err != nil {
		return exifFields{}, err
	}
	off := int64(binary.BigEndian.Uint32(h[0:4]))
	n := int64(binary.BigEndian.Uint32(h[4:8]))
	if off == 0 || n == 0 {
		return exifFields{}, errNoExif
	}
	return readJPEGExif(io.NewSectionReader(r, off, n))
}
