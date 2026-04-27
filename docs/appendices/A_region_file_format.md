# Appendix A — Region file binary format

**Status:** v1.0 (closed at Phase 0 gate; on-disk format frozen until 1.0)
**Consumers:** voxel_store subsystem; save/load orchestration; CI invariant #7.
**Related:** DESIGN.md §2.3, §3.4 invariant #7.

---

## A.1 Purpose

A *region* is the on-disk unit of voxel persistence: **32 × 32 chunk columns**,
covering a 2048 m × 2048 m horizontal footprint at 2 m voxels. One region file
per region; absent file = absent region (not an error).

## A.2 File name convention

```
regions/r.<rx>.<rz>.dem
```

`<rx>`, `<rz>` are signed decimal region coordinates. `.dem` extension is
reserved — the loader ignores any other extension in the regions/ folder.

## A.3 File layout

```
  offset  size        field
  ------  ----------  ---------------------------------------------------
  0x0000  8           magic             "DEMEN\0\0\0"
  0x0008  1           version_byte      (invariant #7; see A.5)
  0x0009  1           flags             bit0 = LZ4, bits1..7 reserved=0
  0x000A  2           reserved          must be 0x0000
  0x000C  4           created_utc_secs  u32 seconds since epoch / 60
  0x0010  4           tick_counter      u32 sim tick at last save
  0x0014  4           rng_seed          u32 deterministic-replay seed
  0x0018  8           world_id          u64 stable id for this world
  0x0020  1024 * 4    index_table       32*32 entries of u32 = offset/4096
                                        (0 = absent column, else sector idx)
  0x1020  1024 * 2    size_table        32*32 entries of u16 = sector count
  0x1820  padding     zero-fill to 0x2000 (one 4 KiB sector)
  0x2000  ...         packed column blobs, each aligned to 4 KiB sectors
```

Sector size is **4096 bytes**. All multi-byte integers are **little-endian**.

## A.4 Chunk-column blob layout

Each column blob begins at the sector indicated by `index_table`. Blob
structure:

```
  offset  size        field
  ------  ----------  ---------------------------------------------------
  0x0000  4           payload_size      u32 decompressed length, bytes
  0x0004  4           compressed_size   u32 compressed length, bytes
  0x0008  1           compression       0=none, 1=LZ4 block
  0x0009  1           column_version    u8, incremented on schema change
  0x000A  2           reserved          must be 0x0000
  0x000C  payload     LZ4 frame (or raw if compression=0)
```

Decompressed payload is a `ChunkColumnV1`:

```
  offset  size            field
  ------  --------------  -----------------------------------------------
  0x00    2               num_chunks          u16 (typically 1..8)
  0x02    2               base_chunk_y        i16 lowest chunk Y in column
  0x04    8*1024          column_cells        ColumnCell[1024] (§2.3.1)
  ...     variable        chunks[num_chunks]  each = PalettedChunkV1
```

### A.4.1 `PalettedChunkV1`

```
  u8   bits_per_index    // 0 = uniform chunk (palette[0] fills all), 1/2/4/8 = bit-packed, 16 = uncompressed
  u8   palette_size
  u16  reserved
  u16  palette[palette_size]   // block-type ids
  u8   indices[]                // bits_per_index * 32768 / 8 bytes, or 0 if uniform
```

## A.5 Version byte (invariant #7)

The `version_byte` at offset 0x0008 is **mandatory from Phase 1 onward.** Its
value through Phase 0 stays at `0x01`. No migration logic exists pre-1.0 —
the loader refuses mismatched files cleanly with:

```
DemEn cannot read this save: region file version 0xNN is not supported by
engine build 0xMM. This save was created by a different engine version.
```

CI gate (§3.4): a PR that writes a region file without the magic + version
byte fails `tests/native/test_region_header.cpp` immediately.

## A.6 Deterministic serialisation

Column ordering inside a region is **row-major** (z-major, then x). Palette
entries are written in **insertion order**, not sorted, so the same world
state produces the same bytes on disk — required for the replay-hash test
(invariant #2).

## A.7 Resolutions

All three open questions closed at the Phase 0 gate. The on-disk format
is frozen until 1.0 — changing any field below bumps
`DEMEN_REGION_FORMAT_VERSION` and breaks save compatibility (invariant #7).

### A.7.1 Column-size policy — no split

Columns are capped, not split. A single chunk column stores at most
`num_chunks == 64` chunks along Y (the `num_chunks` field in §A.4 is
`u16` so the hard limit is 65535, but 64 is the **enforced limit**: it
keeps a decompressed column under 16 MiB worst case and eliminates the
whole category of "which index entry owns this chunk" bugs).

Worlds that need taller vertical range use multiple columns at the same
(x, z) stacked with a `column_y_base` offset already provided by §A.4's
`base_chunk_y`. Phase 1 Specialist enforces the 64-chunk cap with a
hard assert at write time; a column that would exceed it is a Planner
escalation, not a silent truncation.

Why no split: the simplest structural win (§2.11 rung 1 — eliminate the
work). Split-column bookkeeping would add two fields to the index table,
one state machine to the loader, one more failure mode to the save/load
test matrix. For zero gain in Layer 1.

### A.7.2 Endianness — little-endian asserted in CI, no runtime swap

Every multi-byte field is little-endian on disk. We do not target
big-endian hosts (no Windows-on-PowerPC in the 2020s). A CI job
(added Phase 1) runs a one-liner C++ test that fails the build on any
host where `std::endian::native != std::endian::little`. This keeps the
reader a straight `memcpy` into aligned structs — §2.11 rung 1 again:
the cheapest byte-swap is the one you never have to do.

### A.7.3 Backup-on-write — temp file + atomic rename

Every region write goes through this sequence:

1. Write the new contents to `r.<rx>.<rz>.dem.tmp` in the same
   directory (same filesystem — rename is atomic on NTFS and ext4).
2. `fsync` the temp file (Windows: `FlushFileBuffers`).
3. Atomic rename `r.<rx>.<rz>.dem.tmp` -> `r.<rx>.<rz>.dem`.
4. On startup, any leftover `*.tmp` file is deleted before opening the
   world. (The rename step either fully succeeded or did not happen;
   a stale `.tmp` always represents an aborted write that can be safely
   discarded.)

No separate backup/rollback slot. The temp-plus-rename pattern is the
standard POSIX/NTFS answer to "don't corrupt existing saves on crash";
anything more elaborate would be speculative complexity until a user
actually loses a save.

Specialist may NOT move to a write-ahead-log or a double-buffered slot
scheme without a Planner sign-off plus benchmark data showing the
temp-file pattern is the actual bottleneck in the save path.

