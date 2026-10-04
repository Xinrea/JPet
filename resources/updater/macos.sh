#!/bin/bash
# The app copies this helper outside its bundle before launching it.
set -euo pipefail
export PATH=/usr/bin:/bin:/usr/sbin:/sbin

parent_pid="$1"
staged="$2"
target="$3"
failure_log="$4"
work=""
installed=false

recover() {
  result=$?
  trap - EXIT
  if [ "$result" -ne 0 ]; then
    printf '%s\n' "更新安装失败，已尝试恢复旧版本。详情见 updater.log。" > "$failure_log"
    if [ -n "$work" ] && [ -d "$work/previous.app" ]; then
      if [ -e "$target" ]; then mv "$target" "$work/failed.app" || true; fi
      mv "$work/previous.app" "$target" || true
      if [ ! -d "$work/previous.app" ]; then
        /usr/bin/open -n "$target" || true
      else
        printf '旧版本保存在：%s\n' "$work/previous.app" >> "$failure_log"
      fi
    elif [ "$installed" = false ] && [ -d "$target" ]; then
      /usr/bin/open -n "$target" || true
    fi
  fi
  # Preserve the backup if restoration itself failed.
  if [ -n "$work" ] && [ ! -d "$work/previous.app" ]; then rm -rf "$work"; fi
  exit "$result"
}
trap recover EXIT

# Never replace a bundle while the old application is still running.
for ((attempt=0; attempt<120; attempt++)); do
  if ! kill -0 "$parent_pid" 2>/dev/null; then break; fi
  sleep 1
done
if kill -0 "$parent_pid" 2>/dev/null; then
  echo "JPet did not exit within 120 seconds." >&2
  exit 1
fi
test -x "$staged/Contents/MacOS/JPet"
test -d "$target"
work=$(mktemp -d "$(dirname "$target")/.jpet-update.XXXXXX")
/usr/bin/ditto "$staged" "$work/new.app"
# Verify the downloaded application's signature after copying it.
/usr/bin/codesign --verify --deep --strict "$work/new.app"
mv "$target" "$work/previous.app"
mv "$work/new.app" "$target"
installed=true
/usr/bin/open -n "$target"
rm -rf "$work/previous.app"
rm -f "$failure_log"
