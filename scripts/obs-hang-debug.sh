#!/usr/bin/env bash
# On-demand diagnostic launcher. Never attaches to an unrelated OBS process.
set -euo pipefail
umask 077
state="${XDG_STATE_HOME:-$HOME/.local/state}/obs-hang-diagnostics"
mkdir -p "$state"
chmod 700 "$state"
mode=${1:-status}
socket=obs-hang-debug
session=obs
if [[ "$mode" == self-test ]]; then
  socket=obs-hang-self-test
  session=fixture
fi
tm=(tmux -L "$socket")
target="$session:0.0"
has_session(){ "${tm[@]}" has-session -t "$session" 2>/dev/null; }
start(){
  if has_session; then echo "Diagnostic session already exists"; return 1; fi
  local dir
  dir=$(mktemp -d "$state/run.XXXXXXXX")
  cat > "$dir/gdb.commands" <<GDB
set pagination off
set confirm off
set print frame-arguments none
set debuginfod enabled off
set logging file $dir/threads.txt
set logging overwrite on
set logging redirect on
define capture-hang
  set logging enabled on
  echo === OBS thread snapshot ===\\n
  info threads
  thread apply all bt 32
  set logging enabled off
  echo HANG_CAPTURE_COMPLETE\\n
end
run
GDB
  printf '%s\n' "$dir" > "$state/$socket.latest"
  "${tm[@]}" new-session -d -s "$session" -e "DISPLAY=${DISPLAY:-:0}" -e "XAUTHORITY=${XAUTHORITY:-$HOME/.Xauthority}" gdb -q -nx -x "$dir/gdb.commands" --args "$@"
  echo "Diagnostic session: $session"
  echo "Capture directory: $dir"
}
capture(){
  has_session || { echo "No diagnostic session. Start OBS with the launcher first."; return 1; }
  local dir
  dir=$(cat "$state/$socket.latest")
  # Leave the process stopped for inspection after collecting stacks.
  "${tm[@]}" send-keys -t "$target" C-c
  for ((i=0;i<40;i++)); do
    if "${tm[@]}" capture-pane -p -t "$target" -S -4 | grep -qE '^\(gdb\)[[:space:]]*$'; then break; fi
    sleep 0.25
  done
  "${tm[@]}" capture-pane -p -t "$target" -S -4 | grep -qE '^\(gdb\)[[:space:]]*$' || { echo "Debugger did not stop; no snapshot claimed."; return 1; }
  "${tm[@]}" send-keys -t "$target" 'capture-hang' Enter
  for ((i=0;i<80;i++)); do
    if "${tm[@]}" capture-pane -p -t "$target" -S -6 | grep -qx 'HANG_CAPTURE_COMPLETE' && test -s "$dir/threads.txt"; then
      echo "Captured: $dir/threads.txt"
      echo "Inferior is paused; resume explicitly if desired."
      return 0
    fi
    sleep 0.25
  done
  echo "Capture did not complete; inspect debugger session."
  return 1
}
case "$mode" in
start)
  if pgrep -x obs >/dev/null; then echo "OBS is already running; refusing to start a second copy."; exit 1; fi
  start /usr/bin/obs --disable-updater
  ;;
capture) capture ;;
resume)
  has_session || exit 1
  "${tm[@]}" send-keys -t "$target" continue Enter
  ;;
attach) exec "${tm[@]}" attach-session -t "$session" ;;
status)
  if has_session; then echo "Diagnostic session exists."; cat "$state/$socket.latest"; else echo "No active diagnostic session."; fi
  ;;
self-test)
  trap '"${tm[@]}" kill-session -t "$session" 2>/dev/null || true' EXIT
  start /bin/sleep 60
  sleep 2
  capture
  dir=$(cat "$state/$socket.latest")
  grep -q 'OBS thread snapshot' "$dir/threads.txt"
  grep -Eq 'nanosleep|clock_nanosleep|sleep' "$dir/threads.txt"
  echo "Self-test passed."
  ;;
*) echo "Usage: $0 {start|capture|resume|attach|status|self-test}" >&2; exit 2 ;;
esac
