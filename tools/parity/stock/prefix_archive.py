"""Strict host bridge for the same-image Bash prefix fixture."""

from pathlib import Path
import hashlib, json, re, stat

MAX_ENTRIES = 20000
MAX_TABLE = 16 * 1024**2
MAX_FILE = 128 * 1024**2
MAX_ARCHIVE = 32 * 1024**2


def text(value):
    if (
        type(value) is not str
        or not value
        or len(value.encode("utf-8")) > 4096
        or "\\" in value
        or any(ord(c) < 32 or ord(c) == 127 for c in value)
    ):
        raise ValueError("unsafe TSV value")
    return value


def path(value):
    text(value)
    if value.startswith("/") or any(x in ("", ".", "..") for x in value.split("/")):
        raise ValueError("unsafe relative path")
    return value


def row(key, value):
    path(key)
    if value.keys() == {"directory"} and value["directory"] is True:
        return [key, "d", "0", "-", "-"]
    if value.keys() == {"link"}:
        return [key, "l", "0", "-", text(value["link"])]
    if (
        value.keys() == {"sha256", "size"}
        and type(value["size"]) is int
        and 0 <= value["size"] <= MAX_FILE
        and re.fullmatch("[0-9a-f]{64}", value["sha256"])
    ):
        return [key, "f", str(value["size"]), value["sha256"], "-"]
    raise ValueError("unsupported manifest shape")


def encode_manifest(manifest):
    if not isinstance(manifest, dict) or len(manifest) > MAX_ENTRIES:
        raise ValueError("manifest entry bound")
    data = "".join(
        "\t".join(row(k, manifest[k])) + "\n"
        for k in sorted(manifest, key=lambda k: k.encode("utf-8"))
    ).encode("utf-8")
    if len(data) > MAX_TABLE:
        raise ValueError("manifest byte bound")
    return data


def table(filename):
    data = Path(filename).read_bytes()
    if len(data) > MAX_TABLE or (data and not data.endswith(b"\n")):
        raise ValueError("manifest byte/line bound")
    lines = data.decode("utf-8", "strict").split("\n")[:-1]
    if len(lines) > MAX_ENTRIES:
        raise ValueError("manifest entry bound")
    result = []
    previous = None
    for line in lines:
        columns = line.split("\t")
        if len(columns) != 5:
            raise ValueError("manifest column count")
        key = path(columns[0])
        encoded = key.encode("utf-8")
        if previous is not None and encoded <= previous:
            raise ValueError("manifest duplicate/order")
        previous = encoded
        result.append(columns)
    return result


def parse_manifest(filename, *, deletions=False):
    result = {}
    for key, kind, size, digest, link in table(filename):
        if kind == "d" and (size, digest, link) == ("0", "-", "-"):
            value = {"directory": True}
        elif kind == "l" and (size, digest) == ("0", "-"):
            value = {"link": text(link)}
        elif (
            kind == "f"
            and re.fullmatch("0|[1-9][0-9]*", size)
            and int(size) <= MAX_FILE
            and re.fullmatch("[0-9a-f]{64}", digest)
            and link == "-"
        ):
            value = {"sha256": digest, "size": int(size)}
        elif (
            deletions and kind == "deleted" and (size, digest, link) == ("0", "-", "-")
        ):
            value = None
        else:
            raise ValueError("unsupported manifest row")
        result[key] = value
    # A listed symlink/file must never be an ancestor of another listed entry.
    for key in result:
        parts = key.split("/")
        for length in range(1, len(parts)):
            parent = "/".join(parts[:length])
            if parent not in result and not deletions:
                raise ValueError("manifest missing directory ancestor")
            if parent in result and result[parent] not in ({"directory": True}, None):
                raise ValueError("manifest non-directory ancestor")
    return result


def properties(filename, expected):
    data = Path(filename).read_bytes()
    if len(data) > 4096 or not data.endswith(b"\n"):
        raise ValueError("property byte/line bound")
    result = {}
    for line in data.decode("ascii", "strict").split("\n")[:-1]:
        columns = line.split("\t")
        if len(columns) != 2 or columns[0] in result:
            raise ValueError("property shape/duplicate")
        result[columns[0]] = columns[1]
    if set(result) != set(expected):
        raise ValueError("property keys differ")
    return result


def sha(filename):
    h = hashlib.sha256()
    with Path(filename).open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            h.update(chunk)
    return h.hexdigest()


def validate_capture(work, before, *, write_reports=True):
    work = Path(work)
    if (
        parse_manifest(work / "prefix-seed.tsv") != before
        or parse_manifest(work / "prefix-before.tsv") != before
    ):
        raise ValueError("guest seed/copy mismatch")
    prepared = properties(
        work / "prefix-prepared.tsv",
        (
            "complete",
            "seed_mount_readonly",
            "seed_bytes_match",
            "private_inode_identities",
            "entry_count",
            "target_fs_type",
            "target_mount_executable",
        ),
    )
    if (
        any(
            prepared[x] != "1"
            for x in (
                "complete",
                "seed_mount_readonly",
                "seed_bytes_match",
                "private_inode_identities",
            )
        )
        or prepared["entry_count"] != str(len(before))
        or prepared["target_fs_type"] != "tmpfs"
        or prepared["target_mount_executable"] != "1"
    ):
        raise ValueError("prefix prepare incomplete")
    inode_data = (work / "prefix-inodes.tsv").read_bytes()
    if len(inode_data) > MAX_TABLE or (inode_data and not inode_data.endswith(b"\n")):
        raise ValueError("inode byte/line bound")
    inodes = {}
    for line in inode_data.decode("utf-8", "strict").split("\n")[:-1]:
        columns = line.split("\t")
        if len(columns) != 3:
            raise ValueError("inode column count")
        key = path(columns[0])
        a, b = columns[1:]
        if (
            key in inodes
            or not re.fullmatch("(0|[1-9][0-9]*):(0|[1-9][0-9]*)", a)
            or not re.fullmatch("(0|[1-9][0-9]*):(0|[1-9][0-9]*)", b)
            or tuple(map(int, a.split(":"))) == tuple(map(int, b.split(":")))
        ):
            raise ValueError("inode identity not independent")
        inodes[key] = (a, b)
    if set(inodes) != {k for k, v in before.items() if "sha256" in v}:
        raise ValueError("inode coverage differs")
    after = parse_manifest(work / "prefix-after.tsv")
    changed = {
        k: after.get(k)
        for k in sorted(set(before) | set(after), key=lambda k: k.encode("utf-8"))
        if before.get(k) != after.get(k)
    }
    if parse_manifest(work / "prefix-changed.tsv", deletions=True) != changed:
        raise ValueError("changed metadata differs")
    archived = {k: v for k, v in changed.items() if v is not None and "sha256" in v}
    if parse_manifest(work / "prefix-archive.tsv", deletions=True) != archived:
        raise ValueError("archive index differs")
    total = sum(v["size"] for v in archived.values())
    if total > MAX_ARCHIVE:
        raise ValueError("archive byte bound")
    final = properties(
        work / "prefix-finished.tsv",
        ("complete", "writers_quiescent", "changed_count", "changed_regular_bytes"),
    )
    if final != {
        "complete": "1",
        "writers_quiescent": "1",
        "changed_count": str(len(changed)),
        "changed_regular_bytes": str(total),
    }:
        raise ValueError("prefix finish incomplete")
    if (work / "prefix-quiescence.txt").read_text().split() != ["0", "0"]:
        raise ValueError("Wine writers not quiescent")
    root = work / "prefix-changes"
    if root.resolve(strict=True) != root or root.is_symlink() or not root.is_dir():
        raise ValueError("archive root isolation")
    found = set()
    directories = set()
    count = 0
    for entry in root.rglob("*"):
        count += 1
        if count > MAX_ENTRIES:
            raise ValueError("archive entry bound")
        key = path(str(entry.relative_to(root)))
        info = entry.lstat()
        if stat.S_ISDIR(info.st_mode):
            directories.add(key)
        elif stat.S_ISREG(info.st_mode):
            if (
                key not in archived
                or entry.resolve(strict=True) != entry
                or info.st_size != archived[key]["size"]
                or sha(entry) != archived[key]["sha256"]
            ):
                raise ValueError("archive content/hash differs")
            found.add(key)
        else:
            raise ValueError("archive special/link entry")
    if found != set(archived):
        raise ValueError("archive regular set differs")
    parents = {
        str(parent)
        for k in archived
        for parent in Path(k).parents
        if str(parent) != "."
    }
    if directories != parents:
        raise ValueError("archive directory set differs")
    fixture = {
        "private_inode_identities": True,
        "seed_bytes_match": True,
        "entry_count": len(before),
        "seed_mount_readonly": True,
        "target_fs_type": "tmpfs_from_inspected_docker_mount",
    }
    finished = {
        "scope": "full postmanifest plus changed regular bytes; unchanged bytes bound to preserved seed",
        "changed": list(changed),
        "complete": True,
        "writers_quiescent": True,
        "changed_regular_bytes": total,
    }
    if write_reports:
        for name, value in [
            ("prefix-prepared.json", fixture),
            ("prefix-after.json", after),
            ("prefix-finished.json", finished),
        ]:
            (work / name).write_text(json.dumps(value, sort_keys=True) + "\n")
    return fixture, finished, after
