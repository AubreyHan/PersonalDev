//go:build linux

package main

import (
	"log"
	"syscall"
)

const (
	ioprioClassIdle  = 3
	ioprioClassShift = 13
	ioprioWhoProcess = 1
	sysIoprioSet     = 251 // x86_64
)

// lowerPriority sets nice 19 and the idle I/O class for the whole process
// (threads created later inherit both). The start script also uses
// nice/ionice when available; this is the fallback (ARCHITECTURE §8).
func lowerPriority() {
	if err := syscall.Setpriority(syscall.PRIO_PROCESS, 0, 19); err != nil {
		log.Printf("priority: setpriority: %v", err)
	}
	prio := uintptr(ioprioClassIdle << ioprioClassShift)
	if _, _, e := syscall.Syscall(sysIoprioSet, ioprioWhoProcess, 0, prio); e != 0 {
		log.Printf("priority: ioprio_set: %v", e)
	}
}
