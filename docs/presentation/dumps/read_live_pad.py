#!/usr/bin/env python3
# Reads the live bytes of mainGtU's TC entry (trampoline + head pad + first counter pad) from the running
# bzip2 process, once during the profiling window and once after TC2 was committed, and disassembles them
# with objdump. Addresses come from bzip2_tc_only.txt (mainGtU TC entry at 0x40689fbb; a -dump_orig_code run lays TC out slightly differently).
import subprocess, time, os, glob, tempfile
TMP = tempfile.gettempdir()
PIN = os.path.expanduser('~/Desktop/pin/pin-external-4.0-99633-g5ca9893f2-gcc-linux/pin')
START, LEN = 0x40689fbb, 0x1d          # mainGtU in TC: trampoline (5) + head pad (8) + counter pad (8) + add counter (8); from bzip2_tc_only.txt
def app_pid():
    for p in glob.glob('/proc/[0-9]*'):
        try:
            if open(p + '/cmdline', 'rb').read().split(b'\0')[0].endswith(b'bzip2'):
                if '40600000' in open(p + '/maps').read(): return int(p.split('/')[-1])
        except Exception: pass
def snap(pid, label):
    with open(f'/proc/{pid}/mem', 'rb') as m:
        m.seek(START); b = m.read(LEN)
    open(os.path.join(TMP, 'livepad.bin'), 'wb').write(b)
    d = subprocess.run(['objdump', '-D', '-b', 'binary', '-mi386:x86-64', '-M', 'intel',
                        f'--adjust-vma={START:#x}', os.path.join(TMP, 'livepad.bin')], capture_output=True, text=True).stdout
    print(f'=== {label}: bytes at {START:#x}: {b.hex(" ")}')
    print('\n'.join(l for l in d.splitlines() if l.strip().startswith('406')))
out = open(os.path.join(TMP, 'livepad_tool_out.bz2'), 'wb')
t0 = time.time()
p = subprocess.Popen([PIN, '-t', 'obj-intel64/project.so', '-prof_time', '2', '--', './bzip2', '-c', 'input-long.txt'],
                     stdout=out, stderr=subprocess.PIPE)
pid = None
while pid is None and time.time() - t0 < 5: pid = app_pid(); time.sleep(0.05)
time.sleep(max(0, 1.0 - (time.time() - t0)))
snap(pid, 'during the profiling window (t~1.0 s)')
time.sleep(max(0, 3.2 - (time.time() - t0)))
snap(pid, 'after the TC2 commit (t~3.2 s)')
err = p.communicate()[1].decode()
print('=== tool stderr:'); print(err.strip())
