#!/bin/sh
# Write an MPC Sample autorun package: a directory the card can boot, and a zip of it.
#
#   tools/pack-autorun.sh --name my-app --bin path/to/my-app --out build
#
# That creates build/autorun/ and build/my-app.zip. The zip's entries are
# autorun/..., which is what the stock runner and mpc-boot-thingy both accept.
# Run this again after a rebuild. Do not edit the generated files: autorun.check
# is a hash of autorun.json and autorun.sh, and a mismatch will not boot.
set -eu

SALT='f7bed1c9-7e90-4731-9194-c996666b4331'

usage() {
  cat <<'EOF'
Usage:
  pack-autorun.sh --name NAME --bin FILE --out DIR [--add FILE]...

--name NAME
  Package name. Letters, digits, dot, underscore, and hyphen.
  This is the file inside autorun/, the zip name, and the systemd unit.

--bin FILE
  Program to copy into the package under NAME.

--out DIR
  Writes DIR/autorun/ and DIR/NAME.zip.

--add FILE
  Copy one more file into autorun/ under its own name. Repeat for each file.
EOF
}

NAME=
BIN_PATH=
OUT=
adds=$(mktemp)
trap 'rm -f "$adds"' EXIT

while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --name)
      shift
      [ $# -ge 1 ] || { usage >&2; exit 1; }
      NAME=$1
      ;;
    --bin)
      shift
      [ $# -ge 1 ] || { usage >&2; exit 1; }
      BIN_PATH=$1
      ;;
    --out)
      shift
      [ $# -ge 1 ] || { usage >&2; exit 1; }
      OUT=$1
      ;;
    --add)
      shift
      [ $# -ge 1 ] || { usage >&2; exit 1; }
      printf '%s\n' "$1" >> "$adds"
      ;;
    *)
      echo "ERROR: unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
  shift
done

if [ -z "$NAME" ] || [ -z "$BIN_PATH" ] || [ -z "$OUT" ]; then
  usage >&2
  exit 1
fi

case "$NAME" in
  ''|*[!A-Za-z0-9._-]*|.*|-*|autorun.json|autorun.sh|autorun.check)
    echo "ERROR: --name must be letters, digits, dot, underscore, or hyphen: $NAME" >&2
    exit 1
    ;;
esac

case "$OUT" in
  ''|/|.)
    echo "ERROR: refusing --out $OUT" >&2
    exit 1
    ;;
esac

if [ ! -f "$BIN_PATH" ]; then
  echo "ERROR: --bin is not a file: $BIN_PATH" >&2
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "ERROR: python3 is required to write the zip" >&2
  exit 1
fi

pkg=$OUT/autorun
rm -rf "$pkg"
mkdir -p "$pkg"

cat > "$pkg/autorun.json" <<EOF
{
  "value0": {
    "scripts": [
      {
        "path": "autorun.sh",
        "askQuestion": false,
        "message": "Start $NAME?",
        "yesButtonText": "Start",
        "noButtonText": "Cancel"
      }
    ],
    "files": []
  }
}
EOF

# __NAME__ is the only substitution. The rest of this script is copied as written
# and runs on the MPC.
sed "s/__NAME__/$NAME/g" > "$pkg/autorun.sh" <<'PACK'
#!/bin/sh
# __NAME__. The card is mounted noexec. Copy the program into RAM and
# start it without holding the stock script runner.
set -eu
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
NAME=__NAME__

card=$(awk -v p="$D" '
  function unesc(s) {
    gsub(/\\040/, " ", s)
    gsub(/\\011/, "\t", s)
    gsub(/\\012/, "\n", s)
    gsub(/\\134/, "\\", s)
    return s
  }
  {
    mp = unesc($2)
    if (index(p "/", mp "/") == 1 && length(mp) > bestlen) {
      best = mp
      bestlen = length(mp)
    }
  }
  END { if (bestlen) print best }
' /proc/mounts)

if [ -n "${card:-}" ]; then
  if mount -o remount,exec "$card"; then
    echo "remount exec $card"
  else
    echo "WARN: could not remount exec $card"
  fi
else
  echo "WARN: could not find the card mount"
fi

if command -v systemctl >/dev/null 2>&1 && systemctl is-active --quiet "$NAME.service"; then
  exit 0
fi

cp "$D/$NAME" "/run/$NAME"
chmod 755 "/run/$NAME"
mkdir -p "$D/user"

cat > "/run/$NAME-run.sh" <<'EOF'
#!/bin/sh
export MPC_APP_DIR="$1"
name=$2
# The boot menu has already stopped the stock app. On a plain card, wait
# until that app is up, then take the screen. The script runner is not blocked.
if [ ! -f /run/mpc-boot-thingy.pid ]; then
  i=0
  while [ "$i" -lt 45 ]; do
    if command -v systemctl >/dev/null 2>&1 && systemctl is-active --quiet ac50.service; then
      systemctl stop ac50.service || true
      break
    fi
    i=$((i + 1))
    sleep 1
  done
fi
"/run/$name"
status=$?
if [ ! -f /run/mpc-boot-thingy.pid ]; then
  command -v systemctl >/dev/null 2>&1 && systemctl start ac50.service || true
fi
exit "$status"
EOF
chmod 755 "/run/$NAME-run.sh"

# The boot menu waits on this script and takes the screen back when it exits.
if [ -f /run/mpc-boot-thingy.pid ]; then
  exec /bin/sh "/run/$NAME-run.sh" "$D" "$NAME"
fi

# Stock runner: queue the program and return so the card finishes booting.
systemd-run --unit="$NAME" --collect --no-block \
  /bin/sh "/run/$NAME-run.sh" "$D" "$NAME"
PACK
chmod 755 "$pkg/autorun.sh"
sh -n "$pkg/autorun.sh"

cp "$BIN_PATH" "$pkg/$NAME"
chmod 755 "$pkg/$NAME"

extra_names=$(mktemp)
trap 'rm -f "$adds" "$extra_names"' EXIT
while IFS= read -r extra; do
  [ -n "$extra" ] || continue
  if [ ! -f "$extra" ]; then
    echo "ERROR: --add is not a file: $extra" >&2
    exit 1
  fi
  base=$(basename "$extra")
  case "$base" in
    ''|.*|*/*|autorun.json|autorun.sh|autorun.check|"$NAME")
      echo "ERROR: refusing to add $base" >&2
      exit 1
      ;;
  esac
  if [ -e "$pkg/$base" ]; then
    echo "ERROR: $base is already in the package" >&2
    exit 1
  fi
  cp "$extra" "$pkg/$base"
  printf '%s\n' "$base" >> "$extra_names"
done < "$adds"

if command -v shasum >/dev/null 2>&1; then
  digest=$({ cat "$pkg/autorun.json" "$pkg/autorun.sh"; printf '%s' "$SALT"; } | shasum -a 256 | awk '{print $1}')
elif command -v sha256sum >/dev/null 2>&1; then
  digest=$({ cat "$pkg/autorun.json" "$pkg/autorun.sh"; printf '%s' "$SALT"; } | sha256sum | awk '{print $1}')
else
  echo "ERROR: shasum or sha256sum is required" >&2
  exit 1
fi
printf '%s' "$digest" > "$pkg/autorun.check"

zip_path=$OUT/$NAME.zip
rm -f "$zip_path"
python3 - "$pkg" "$NAME" "$zip_path" "$extra_names" <<'PY'
import hashlib, json, os, sys, zipfile

root, app, dest, extra_file = sys.argv[1:5]
salt = b"f7bed1c9-7e90-4731-9194-c996666b4331"
js = os.path.join(root, "autorun.json")
sh = os.path.join(root, "autorun.sh")
check_path = os.path.join(root, "autorun.check")

def die(msg):
    sys.stderr.write("ERROR: %s\n" % msg)
    sys.exit(1)

blob = open(js, "rb").read() + open(sh, "rb").read() + salt
digest = hashlib.sha256(blob).hexdigest().encode("ascii")
got = open(check_path, "rb").read()
if got != digest or b"\n" in got:
    die("autorun.check does not match")

doc = json.loads(open(js, "rb").read().decode("utf-8"))
script = doc["value0"]["scripts"][0]
if script.get("askQuestion") is not False:
    die("askQuestion is not false")
if script.get("path") != "autorun.sh":
    die("script path is not autorun.sh")

extras = []
with open(extra_file, "r") as fh:
    for line in fh:
        line = line.rstrip("\n")
        if line:
            extras.append(line)

with zipfile.ZipFile(dest, "w", compression=zipfile.ZIP_DEFLATED) as zipped:
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if not d.startswith("."))
        for fname in sorted(filenames):
            if fname.startswith(".") or fname == ".DS_Store":
                continue
            path = os.path.join(dirpath, fname)
            rel = "autorun/" + os.path.relpath(path, root).replace(os.sep, "/")
            with open(path, "rb") as fh:
                data = fh.read()
            info = zipfile.ZipInfo(rel)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (os.stat(path).st_mode & 0xFFFF) << 16
            zipped.writestr(info, data)

with zipfile.ZipFile(dest) as zipped:
    names = sorted(i.filename for i in zipped.infolist())
want = sorted(
    [
        "autorun/autorun.json",
        "autorun/autorun.sh",
        "autorun/autorun.check",
        "autorun/" + app,
    ]
    + ["autorun/" + extra for extra in extras]
)
if names != want:
    die("zip entries are %s" % ", ".join(names))
PY
