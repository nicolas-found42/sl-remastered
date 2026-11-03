#!/bin/bash
# Same-image fixture: Bash + GNU core/findutils only; no guest Python.
set -euo pipefail
export LC_ALL=C
seed=/seed target=/privateprefix out=/work
payload=${PREFIX_PAYLOAD:-/payload}
MAX_FILE=$((128*1024*1024)) MAX_ARCHIVE=$((32*1024*1024))
valid_text() {
 local value=$1
 [[ -n "$value" && ${#value} -le 4096 && "$value" != *\\* && "$value" != *[[:cntrl:]]* ]]
}
valid_path() {
 valid_text "$1" && [[ "$1" != /* && "$1" != . && "$1" != .. && "$1" != */ && "$1" != */../* && "$1" != ../* && "$1" != */.. && "$1" != */./* && "$1" != ./* && "$1" != */. && "$1" != *//* ]]
}
manifest() {
 local root=$1 destination=$2 path kind size digest link count=0 info
 local list="${destination}.list0" rows="${destination}.rows"
 # SIGPIPE/timeout/entry overflow refuse; never silently accept truncated output.
 find -P "$root" -mindepth 1 -printf '%P\0' | head -z -n 20001 > "$list"
 : > "$rows"
 while IFS= read -r -d '' path; do
  count=$((count+1)); [[ "$count" -le 20000 ]] || return 21
  valid_path "$path" || return 22
  size=0 digest=- link=-
  if [[ -L "$root/$path" ]]; then
   kind=l
   # readlink -z preserves trailing newlines; Bash read rejects embedded NUL by design.
   IFS= read -r -d '' link < <(readlink -z -- "$root/$path") || return 23
   valid_text "$link" || return 24
  elif [[ -d "$root/$path" ]]; then kind=d
  elif [[ -f "$root/$path" ]]; then
   kind=f; size=$(stat -c '%s' -- "$root/$path")
   [[ "$size" =~ ^[0-9]+$ && "$size" -le "$MAX_FILE" ]] || return 25
   info=$(sha256sum -- "$root/$path"); digest=${info%% *}
   [[ "$digest" =~ ^[0-9a-f]{64}$ ]] || return 26
  else return 27
  fi
  printf '%s\t%s\t%s\t%s\t%s\n' "$path" "$kind" "$size" "$digest" "$link" >> "$rows"
 done < "$list"
 sort -- "$rows" > "$destination"
}
run_manifest() {
 timeout --kill-after=3 45 /bin/bash "$payload/prefix_archive.sh" manifest "$1" "$2"
}
prepare() {
 printf 'complete\t0\n' > "$out/prefix-prepared.tsv"
 [[ $(stat -f -c '%T' -- "$target") == tmpfs ]]
 local readonly=0 executable=0 fields
 while IFS=' ' read -r -a fields; do
  if [[ ${fields[4]-} == /seed ]]; then
   case ",${fields[5]-}," in *,ro,*) readonly=$((readonly+1));; esac
  fi
  if [[ ${fields[4]-} == /privateprefix ]]; then
   case ",${fields[5]-}," in *,noexec,*) return 28;; *) executable=$((executable+1));; esac
  fi
 done < /proc/self/mountinfo
 [[ "$readonly" -eq 1 && "$executable" -eq 1 ]]
 [[ -z $(find -P "$target" -mindepth 1 -print -quit) ]]
 run_manifest "$seed" "$out/prefix-seed.tsv"
 cmp -- "$payload/expected-prefix.tsv" "$out/prefix-seed.tsv"
 cp -a --no-preserve=ownership -- "$seed/." "$target/"
 run_manifest "$target" "$out/prefix-before.tsv"
 cmp -- "$payload/expected-prefix.tsv" "$out/prefix-before.tsv"
 local path kind size digest link source_id target_id count=0
 : > "$out/prefix-inodes.tsv"
 while IFS=$'\t' read -r path kind size digest link; do
  if [[ "$kind" == f ]]; then
   source_id=$(stat -c '%d:%i' -- "$seed/$path");target_id=$(stat -c '%d:%i' -- "$target/$path")
   [[ "$source_id" != "$target_id" ]]
   printf '%s\t%s\t%s\n' "$path" "$source_id" "$target_id" >> "$out/prefix-inodes.tsv"
  fi
  count=$((count+1))
 done < "$out/prefix-before.tsv"
 printf 'complete\t1\nseed_mount_readonly\t1\nseed_bytes_match\t1\nprivate_inode_identities\t1\nentry_count\t%s\ntarget_fs_type\ttmpfs\ntarget_mount_executable\t1\n' "$count" > "$out/prefix-prepared.tsv"
}
finish() {
 printf 'complete\t0\nwriters_quiescent\t%s\n' "${PREFIX_QUIESCENCE:-0}" > "$out/prefix-finished.tsv"
 run_manifest "$target" "$out/prefix-after.tsv"
 # Stream merge preserves deletions and metadata-only changes without evaluating
 # untrusted paths as Bash associative subscripts or invoking guest interpreters.
 local before_row='' after_row='' before_path='' after_path='' before_ok=0 after_ok=0
 local path kind size digest link dest info total=0 changed=0
 exec 3< "$payload/expected-prefix.tsv" 4< "$out/prefix-after.tsv"
 read_before() { if IFS= read -r before_row <&3; then before_path=${before_row%%$'\t'*};before_ok=1;else before_ok=0;fi; }
 read_after() { if IFS= read -r after_row <&4; then after_path=${after_row%%$'\t'*};after_ok=1;else after_ok=0;fi; }
 read_before;read_after
 mkdir -- "$out/prefix-changes"
 : > "$out/prefix-changed.tsv";: > "$out/prefix-archive.tsv"
 archive_row() {
  changed=$((changed+1));printf '%s\n' "$1" >> "$out/prefix-changed.tsv"
  IFS=$'\t' read -r path kind size digest link <<< "$1"
  valid_path "$path"
  if [[ "$kind" == f ]]; then
   total=$((total+size));[[ "$total" -le "$MAX_ARCHIVE" ]] || return 31
   [[ -f "$target/$path" && ! -L "$target/$path" ]]
   dest="$out/prefix-changes/$path";mkdir -p -- "$(dirname -- "$dest")"
   cp --no-dereference --no-preserve=ownership -- "$target/$path" "$dest"
   [[ -f "$dest" && ! -L "$dest" && $(stat -c '%s' -- "$dest") == "$size" ]]
   info=$(sha256sum -- "$dest");[[ ${info%% *} == "$digest" ]]
   printf '%s\n' "$1" >> "$out/prefix-archive.tsv"
  fi
 }
 while [[ "$before_ok" -eq 1 || "$after_ok" -eq 1 ]]; do
  if [[ "$after_ok" -eq 0 || ( "$before_ok" -eq 1 && "$before_path" < "$after_path" ) ]]; then
   archive_row "$before_path"$'\tdeleted\t0\t-\t-';read_before
  elif [[ "$before_ok" -eq 0 || "$after_path" < "$before_path" ]]; then
   archive_row "$after_row";read_after
  else
   if [[ "$before_row" != "$after_row" ]]; then archive_row "$after_row";fi
   read_before;read_after
  fi
 done
 exec 3<&- 4<&-
 [[ ${PREFIX_QUIESCENCE:-0} == 1 ]]
 printf 'complete\t1\nwriters_quiescent\t1\nchanged_count\t%s\nchanged_regular_bytes\t%s\n' "$changed" "$total" > "$out/prefix-finished.tsv"
}
case ${1-} in
 manifest) [[ $# == 3 ]];manifest "$2" "$3";;
 prepare) prepare;;
 finish) finish;;
 *) exit 64;;
esac
