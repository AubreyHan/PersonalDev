#!/bin/sh
# Photo Album — TS-873A 环境探查脚本（只读，不修改任何文件）
# 用法：ssh admin@<NAS> 后执行  sh nas_probe.sh > probe.txt 2>&1
# 然后把 probe.txt 的内容发回。

section() { echo; echo "===== $1 ====="; }

section "系统"
uname -a
cat /etc/version 2>/dev/null
getcfg System Version 2>/dev/null
getcfg System "Build Number" 2>/dev/null
id

section "时区"
date
date -u
ls -la /etc/localtime 2>&1
cat /etc/TZ 2>&1
echo "TZ env=$TZ"
grep -i -E 'zone|time' /etc/config/uLinux.conf 2>/dev/null | head -20
ls /usr/share/zoneinfo 2>&1 | head -5

section "nice / ionice"
which nice ionice 2>&1
ionice -h 2>&1 | head -5
busybox 2>&1 | head -3
busybox --list 2>/dev/null | grep -E '^(ionice|nice|chrt)$'

section "/share 结构"
ls -la /share/ 2>&1
mount | grep -E 'CACHEDEV|/share' 2>&1 | head -20
df -h 2>&1 | grep -E 'CACHEDEV|Filesystem' | head -20

section "QPKG 安装位置"
getcfg -f /etc/config/qpkg.conf 2>/dev/null >/dev/null && echo "qpkg.conf exists"
grep -E '^\[|Install_Path|Web_Port|Enable' /etc/config/qpkg.conf 2>/dev/null | head -40
ls -d /share/*/.qpkg 2>&1
ls /share/CACHEDEV1_DATA/.qpkg 2>&1 | head -20

section "QPKG 工具"
which qbuild getcfg setcfg 2>&1
ls /etc/init.d/ 2>&1 | head -40

section "ACL / 文件系统"
which getfacl setfacl 2>&1
getcfg System "ACL Enable" 2>/dev/null
getcfg -f /etc/config/smb.conf global "nt acl support" 2>/dev/null
for d in /share/CACHEDEV*_DATA; do
  [ -d "$d" ] || continue
  echo "-- $d"
  stat -f -c 'fs type: %T' "$d" 2>/dev/null
  ls -la "$d" | head -15
done

section "资源"
nproc 2>/dev/null
grep -m1 'model name' /proc/cpuinfo
free -m 2>/dev/null

section "完成"
