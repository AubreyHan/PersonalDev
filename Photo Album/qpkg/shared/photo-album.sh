#!/bin/sh
# Photo Album service script (QPKG_SERVICE_PROGRAM): start | stop | restart | status
CONF=/etc/config/qpkg.conf
QPKG_NAME="photo-album"
QPKG_ROOT=$(/sbin/getcfg "$QPKG_NAME" Install_Path -f "$CONF")
PORT=$(/sbin/getcfg "$QPKG_NAME" Web_Port -d 8090 -f "$CONF")
BIN="$QPKG_ROOT/bin/photo-album-server"
PIDFILE="/var/run/${QPKG_NAME}.pid"
LOG="$QPKG_ROOT/photo-album.log"
LOG_MAX_BYTES=5242880

is_running() {
  [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null
}

start() {
  ENABLED=$(/sbin/getcfg "$QPKG_NAME" Enable -u -d FALSE -f "$CONF")
  if [ "$ENABLED" != "TRUE" ]; then
    echo "$QPKG_NAME is disabled."
    exit 1
  fi
  if is_running; then
    echo "$QPKG_NAME is already running."
    return 0
  fi
  if [ -f "$LOG" ] && [ "$(wc -c < "$LOG")" -gt "$LOG_MAX_BYTES" ]; then
    mv -f "$LOG" "$LOG.1"
  fi
  # Low priority (ARCHITECTURE §1): nice 19 + idle I/O class when available.
  # The binary also applies both itself via Linux syscalls as a fallback.
  PREFIX=""
  ionice -c 3 true >/dev/null 2>&1 && PREFIX="ionice -c 3"
  nice -n 19 true >/dev/null 2>&1 && PREFIX="nice -n 19 $PREFIX"
  cd "$QPKG_ROOT" || exit 1
  trap '' HUP
  # shellcheck disable=SC2086
  $PREFIX "$BIN" -data-dir "$QPKG_ROOT" -listen ":$PORT" -share-root /share </dev/null >> "$LOG" 2>&1 &
  echo $! > "$PIDFILE"
  echo "$QPKG_NAME started."
}

stop() {
  if ! is_running; then
    rm -f "$PIDFILE"
    return 0
  fi
  PID=$(cat "$PIDFILE")
  kill "$PID"
  # Allow an in-progress file move to finish.
  i=0
  while kill -0 "$PID" 2>/dev/null && [ $i -lt 75 ]; do
    sleep 1
    i=$((i + 1))
  done
  kill -0 "$PID" 2>/dev/null && kill -9 "$PID"
  rm -f "$PIDFILE"
  echo "$QPKG_NAME stopped."
}

case "$1" in
  start) start ;;
  stop) stop ;;
  restart) stop; start ;;
  status)
    if is_running; then echo "$QPKG_NAME is running."; else echo "$QPKG_NAME is stopped."; exit 1; fi
    ;;
  *)
    echo "Usage: $0 {start|stop|restart|status}"
    exit 1
    ;;
esac
exit 0
