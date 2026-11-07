// Package importlog writes the daily import log <import>/logs/YYYYMMDD.log
// (PRD §3.3, ARCHITECTURE §5.10).
package importlog

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

// DirName is the log directory inside the import directory.
const DirName = "logs"

// RetentionDays is how long log files are kept.
const RetentionDays = 90

// Conflict / result types.
const (
	TypeBatchDup    = "本次重复"
	TypeSameNameDup = "同名重复"
	TypeContentDup  = "内容重复"
	TypeRenamed     = "同名改名"
	TypeLivePartial = "Live Photo 部分重复"
	TypeFailed      = "失败"
	LivePrefix      = "Live Photo "
)

// Skip reasons.
const (
	ReasonWriting = "检测到写入，跳过本次扫描"
	ReasonBusy    = "上次扫描未完成，跳过"
)

const timeLayout = "2006-01-02 15:04:05"

// Entry is one conflict / failure line.
type Entry struct {
	Type   string
	Source string // absolute path in the import directory
	Ref    string // conflicting album path, new path, or failure reason
}

// Logger writes log files in a directory using the given time zone.
type Logger struct {
	Dir string
	Loc *time.Location
}

// New returns a Logger for an import directory.
func New(importDir string, loc *time.Location) *Logger {
	return &Logger{Dir: filepath.Join(importDir, DirName), Loc: loc}
}

func (l *Logger) fileFor(t time.Time) string {
	return filepath.Join(l.Dir, t.In(l.Loc).Format("20060102")+".log")
}

func (l *Logger) ts(t time.Time) string { return t.In(l.Loc).Format(timeLayout) }

func (l *Logger) appendLines(t time.Time, lines []string) error {
	if err := os.MkdirAll(l.Dir, 0o755); err != nil {
		return err
	}
	f, err := os.OpenFile(l.fileFor(t), os.O_WRONLY|os.O_CREATE|os.O_APPEND, 0o644)
	if err != nil {
		return err
	}
	_, werr := f.WriteString(strings.Join(lines, "\n") + "\n")
	cerr := f.Close()
	if werr != nil {
		return werr
	}
	return cerr
}

// WriteScan appends the result of one scan to the file of the start date.
// abort is empty for a normal completion, otherwise the abort reason.
func (l *Logger) WriteScan(start, end time.Time, abort string, entries []Entry) error {
	head := fmt.Sprintf("[%s] 扫描开始 %s | 完成 %s", l.ts(start), l.ts(start), l.ts(end))
	if abort != "" {
		head = fmt.Sprintf("[%s] 扫描开始 %s | 中止 %s | %s", l.ts(start), l.ts(start), l.ts(end), abort)
	}
	lines := []string{head}
	for _, e := range entries {
		ref := e.Ref
		if ref == "" {
			ref = "-"
		}
		lines = append(lines, fmt.Sprintf("%s | %s | %s", e.Type, e.Source, oneLine(ref)))
	}
	return l.appendLines(start, lines)
}

// WriteSkip appends a skipped-scan line.
func (l *Logger) WriteSkip(t time.Time, reason string) error {
	return l.appendLines(t, []string{fmt.Sprintf("[%s] 跳过 | %s", l.ts(t), reason)})
}

// Prune deletes YYYYMMDD.log files older than RetentionDays before now.
func (l *Logger) Prune(now time.Time) error {
	ents, err := os.ReadDir(l.Dir)
	if err != nil {
		if os.IsNotExist(err) {
			return nil
		}
		return err
	}
	n := now.In(l.Loc)
	cutoff := time.Date(n.Year(), n.Month(), n.Day(), 0, 0, 0, 0, l.Loc).AddDate(0, 0, -RetentionDays)
	var names []string
	for _, e := range ents {
		names = append(names, e.Name())
	}
	sort.Strings(names)
	for _, name := range names {
		if !strings.HasSuffix(name, ".log") || len(name) != len("20060102.log") {
			continue
		}
		d, err := time.ParseInLocation("20060102", strings.TrimSuffix(name, ".log"), l.Loc)
		if err != nil {
			continue
		}
		if d.Before(cutoff) {
			if err := os.Remove(filepath.Join(l.Dir, name)); err != nil {
				return err
			}
		}
	}
	return nil
}

func oneLine(s string) string { return strings.NewReplacer("\n", " ", "\r", " ").Replace(s) }
