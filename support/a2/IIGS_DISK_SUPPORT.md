# Apple IIgs Disk Support — Design & Implementation Spec

Status: **design document** (no code yet). This is the implementation plan for adding
multi-format disk support to the new Apple IIgs core in MiSTer Main.

---

## 1. Goal

The IIgs core accepts only two native on-the-wire formats:

- **Hard disk** — raw ProDOS blocks, **no header**.
- **Floppy (5.25″ and 3.5″)** — **WOZ** bitstream (the core's HDL parses TMAP/TRKS itself).

Users, however, distribute IIgs software in many formats (`.2mg`, `.dsk`, `.do`, `.po`,
`.nib`, `.hdv`, `.dc42`, `.woz`). The job of MiSTer Main is to **detect the format,
validate it against the slot, route it to the correct slot, and translate it on the fly**
to one of the two native formats — without breaking copy protection on native WOZ images.

### Decisions locked in
| Topic | Decision |
|---|---|
| Wrong-disk guard | **Respect the chosen slot when valid; block broken combos** with a guiding OSD message. Not a router (see §6). |
| Slot tolerance | **HDD slots permissive** (any ProDOS-order block image, any size); **floppy slots strict** on geometry. |
| Auto-routing | **Never.** A disk is only ever mounted in the slot the user picked, or blocked with guidance. No disk is ever silently moved. |
| WOZ normalization | **No** — native `.woz` is served byte-for-byte (flat block I/O). Rewriting risks breaking copy protection. |
| Converter architecture | **On-the-fly** per-LBA translation (the live `SD_TYPE_A2` pattern), **not** one-shot temp files. |
| Converted-floppy write-back | **Implemented.** On each core write the affected WOZ track is decoded and persisted back to the source `.po/.dsk/.do/.2mg` (per-track, re-skewed to ProDOS where needed). `.nib`/DC42 sources stay read-only. |
| DiskCopy 4.2 (DC42) | **Demoted.** Mac-native format IIgs software essentially never ships as; kept only as a cheap `probeDC42()` content-detected fallback, no dedicated fixtures/effort (§10, §12). |
| Dead code | Remove unused `dsk2nib()` from `DiskImage.cpp` (see §9). |

---

## 2. Target core: slot layout

From `Apple-IIgs.sv` (`CONF_STR`, `VDNUM=4`):

| Slot | CONF_STR | Class | HDL routing |
|---|---|---|---|
| **S0** | `S0,HDVPO ;` | Hard disk | `HDD_*` unit (raw blocks) |
| **S1** | `S1,HDVPO ;` | Hard disk | `HDD_*` unit (raw blocks) |
| **S2** | `S2,WOZ,WOZ 3.5;` | 3.5″ floppy | 3.5″ WOZ controller |
| **S3** | `S3,WOZ,WOZ 5.25;` | 5.25″ floppy | 5.25″ WOZ controller |

The CONF_STR extension list is the **first-pass filter** (S0/S1 only offer `HDV`/`PO`,
S2/S3 only `WOZ`). To let users pick the new formats we must **widen these lists** in the
core, e.g. `S0,HDVPO2MGDC;` and `S3,WOZDSKDOPONIB2MG;`. Widening reintroduces ambiguity
(a `.2mg` can be HDD *or* floppy), which is exactly what the MiSTer-side guard resolves.

> **Future expansion (3–4 floppies):** the HDL side is the constraint (WOZ track buffers
> must fit in BRAM). The MiSTer side must therefore be **table-driven** (§6) so adding a
> slot is one table row, not new logic.

---

## 3. Architecture overview

```
   File browser (menu.cpp)
        │  user_io_file_mount(name, index)        index = slot the user picked
        ▼
   ┌─────────────────────────────────────────────┐
   │ MOUNT-TIME (user_io.cpp:2084)                │
   │  1. classify(name)  → DiskClass + header_off │  (§5 parsers)
   │  2. route: pick correct slot for the class   │  (§6 routing)
   │  3. validate: strict reject + InfoMessage     │  (§7 guard)
   │  4. set sd_type[slot], header_offset[slot]   │
   └─────────────────────────────────────────────┘
        │
        ▼
   ┌─────────────────────────────────────────────┐
   │ RUN-TIME per-LBA (user_io.cpp:3256 dispatch) │
   │  HDD  → offset-mapped passthrough            │  (§8.1)
   │  WOZ native → flat block passthrough         │  (§8.2)
   │  DSK/NIB/PO floppy → on-the-fly ⇄ WOZ        │  (§8.3, v2 write)
   └─────────────────────────────────────────────┘
```

The existing `SD_TYPE_A2` path (`a2_readDSK`/`a2_writeDSK` → `dsk2nib_lib.cpp`) is the
proven template: per-LBA, bidirectional, writable. New converters follow the same shape.

---

## 4. Device classes

```c
enum DiskClass {
    DC_UNKNOWN,
    DC_HDD,        // raw ProDOS blocks, any size (typically > 800K)
    DC_FLOPPY_525, // 140K logical (143360) or NIB (232960) or WOZ type 1
    DC_FLOPPY_35,  // 800K logical (819200) or WOZ type 2
};
```

### Canonical sizes
| Geometry | Bytes |
|---|---|
| 5.25″ logical (35×16×256) | 143,360 |
| 5.25″ NIB (35×6656) | 232,960 |
| 3.5″ logical (1600×512) | 819,200 |
| Hard disk | any 512-multiple, usually > 819,200 |

### Classification rule
Probe by **content first** (magic/structure), fall back to extension+size only for bare images.
1. **WOZ** (`magic "WOZ1"/"WOZ2"`): read INFO `disk type` byte → 1 ⇒ `DC_FLOPPY_525`, 2 ⇒ `DC_FLOPPY_35`.
2. **2MG** (`magic "2IMG"`): parse header (§5.1). `image format` 2 (NIB) ⇒ 525. Otherwise use payload size: 143360 ⇒ 525, 819200 ⇒ 35, else ⇒ HDD.
3. **DC42** (`probeDC42()` content match, §5.2 — *not* by extension): use `dataSize`: 819200 ⇒ 35, 409600 ⇒ 35 (400K), else HDD.
4. **Bare by size/ext** (raw, no header): `.nib` ⇒ 525; 143360 (`.dsk/.do/.po`) ⇒ 525; 819200 (`.po`) ⇒ 35; `.hdv` or other ⇒ HDD.

**The one genuine ambiguity:** a ProDOS hard-disk volume that is *exactly* 819,200 bytes is
indistinguishable from a 3.5″ floppy by size. Rule: **819,200 ⇒ `DC_FLOPPY_35`** (real HDD
volumes are essentially never exactly 800K). Log the assumption; do not hard-fail.

---

## 5. Format parsers (the byte-level reference)

All multi-byte fields are **little-endian** unless noted. DC42 is **big-endian**.

### 5.1 2MG / 2IMG (`.2mg`, `.2img`) — 64-byte header

| Off | Sz | Field | Use |
|---|---|---|---|
| 0x00 | 4 | magic `"2IMG"` | identify |
| 0x04 | 4 | creator | ignore |
| 0x08 | 2 | header length (=64) | validate |
| 0x0A | 2 | version (=1) | — |
| **0x0C** | 4 | **image format** | 0=DOS order, 1=ProDOS order, 2=NIB, 3=CP/M |
| 0x10 | 4 | flags + DOS volume# | bit31 = write-protected |
| 0x14 | 4 | block count | × 512 = size when data length is 0 |
| **0x18** | 4 | **data offset** | bytes to strip (the header_offset) |
| **0x1C** | 4 | **data length** | payload size (excludes trailing chunks) |
| 0x20 | 4 | comment offset | — |
| 0x24 | 4 | comment length | — |
| 0x28 | 4 | creator-data offset | — |
| 0x2C | 4 | creator-data length | — |
| 0x30 | 16 | reserved | — |

**Parser logic — port verbatim from `sim_blkdevice.cpp:157–185` (it is correct):**
- `header_offset = data_offset` (NOT a hardcoded 64 — handles larger headers).
- `payload_size  = data_length`; if `data_length == 0`, `payload_size = block_count × 512`.
- This correctly ignores **trailing** comment/creator chunks after the data.
- **Validate before trusting:** `header_length == 64 && data_offset >= 64 &&
  data_offset + payload_size <= file_size`. On failure, log and treat the file as raw
  (no header adjustment).
- `image format` + `payload_size` feed classification (§4) and DOS-vs-ProDOS sector order
  for the 5.25″ WOZ converter (§8.3).

### 5.2 DiskCopy 4.2 — **detected by content, never by extension** — 84-byte **big-endian** header

| Off | Sz | Field | Use |
|---|---|---|---|
| 0x00 | 1 | name length (Pascal) | — (must be ≤ 63) |
| 0x01 | 63 | disk name | — |
| 0x40 | 4 | **dataSize** (BE) | payload size (800K=819200) |
| 0x44 | 4 | tagSize (BE) | tag fork length (usually 0 for IIgs) |
| 0x48 | 4 | dataChecksum (BE) | **goes stale on write** (see §8.1) |
| 0x4C | 4 | tagChecksum (BE) | — |
| 0x50 | 1 | diskFormat | 0=400K,1=800K GCR,2/3=MFM |
| 0x51 | 1 | formatByte | 0x24 = 800K Apple II/ProDOS |
| 0x52 | 2 | magic `0x01 0x00` (BE) | validate |

- `header_offset = 84`, `payload_size = dataSize`. Data fork is in ProDOS block order;
  tag fork (12 bytes/block, after the data fork) is dropped.

> **Why content-detection (verified):** the `.dsk` extension is overloaded — on Apple II it
> means *raw/headerless*, and on Mac it *also* usually means *raw*, not DC42. Real DC42 files
> use `.image`/`.img`/`.dc42` (or no extension, type/creator `dImg`/`dCpy`). So **do not key
> DC42 off any extension.** `probeDC42()` runs on every floppy/HDD candidate and matches only
> when ALL hold: magic `0x01 0x00` at 0x52 **and** `84 + dataSize + tagSize == filesize`
> **and** `dataSize % 512 == 0` **and** `tagSize % 12 == 0` **and** name-length ≤ 63. The
> magic-plus-size-equation pair has no realistic false positives against raw 140K/800K images
> (a raw 819,200-byte file is 84 bytes too short to satisfy the size equation). This way a
> DC42 mislabeled `.dsk` and a raw image labeled `.dsk` both parse correctly.

### 5.3 WOZ (`.woz`) — served as-is, parsed only to classify

12-byte file header: `"WOZ1"`/`"WOZ2"` (0x00), `0xFF` (0x04), `0A 0D 0A` (0x05),
CRC32 of bytes 12+ (0x08; 0 = "not computed"). Then chunks (`ID`,`u32 size`,`data`).

For **classification only** we read the INFO chunk:
- INFO is the first chunk; payload `disk type` at INFO-data **+1**: `1`=5.25″, `2`=3.5″.

We do **not** parse TMAP/TRKS — the core does. We do **not** rewrite the file. (TMAP/TRKS
layout is documented in `doc/wozreference.html`; relevant only if we ever build the
DSK→WOZ converter in §8.3.)

### 5.4 Bare (raw, headerless) formats

These have no header, so order comes from the extension. **`.dsk` is raw on *both* Apple II
and Mac** (it is *not* a DC42 indicator — see §5.2); only a positive `probeDC42()` content
match makes a file DC42, regardless of its extension.

| Ext | Order | header_offset | Notes |
|---|---|---|---|
| `.po` | ProDOS | 0 | 140K floppy or HDD volume by size |
| `.do` | DOS 3.3 | 0 | 140K 5.25″ floppy |
| `.dsk` | DOS 3.3 (assumed) | 0 | raw; order not recorded — assume DOS for 5.25″ |
| `.nib` | nibbles | 0 | 232,960; 5.25″ only |
| `.hdv` | ProDOS | 0 | hard disk, any size |

---

## 6. Slot validation (respect the user's choice; don't auto-route)

**Guiding principle: the slot the user picked is their intent — honor it whenever the
image is valid there.** Auto-routing by detected class is *wrong* as a default because some
combinations are legitimately a user choice, not a mistake:

- An **800K `.2mg` in a hard-disk slot** is a deliberate, valid choice — it runs as a fast
  raw-block volume instead of as a (slower) emulated 3.5″ floppy. Silently rerouting it to
  the floppy slot would break that workflow.

So this is a **validator**, not a router. It leans on a slot→class table plus the
**asymmetric tolerance** of the two classes:

```c
struct SlotDef { int index; DiskClass cls; };
static const SlotDef IIGS_SLOTS[] = {
    {0, DC_HDD},        // permissive: any ProDOS-order block image, any size
    {1, DC_HDD},        // permissive
    {2, DC_FLOPPY_35},  // strict: must be 800K-equivalent (or 3.5″ WOZ)
    {3, DC_FLOPPY_525}, // strict: must be 140K/NIB-equivalent (or 5.25″ WOZ)
};
```

**`accepts(slot, image)` — the compatibility test:**

| Slot class | Accepts | Rejects |
|---|---|---|
| `DC_HDD` | any **ProDOS-order** block image of any size: `.hdv`, `.po`, `.2mg` fmt 1, `.dc42`, raw — incl. 140K/800K volumes | DOS-order-only images (`.do`, `.dsk`, `.2mg` fmt 0), `.nib`, `.woz` |
| `DC_FLOPPY_35` | 800K (819,200) payload: `.po`/`.2mg`/`.dc42`; or `.woz` INFO type 2 | anything not 800K; 5.25″ WOZ; HDD-sized images |
| `DC_FLOPPY_525` | 140K (143,360) `.dsk/.do/.po`/`.2mg` fmt 0/2; `.nib` (232,960); or `.woz` INFO type 1 | 800K; 3.5″ WOZ; HDD-sized images |

**Mount algorithm** (inside / wrapping `user_io_file_mount`):

1. `cls = classify(name)` (§4–5). If `DC_UNKNOWN` → block + error (§7).
2. **If `accepts(picked_slot, image)` → mount there. Done. No message, no move.**
   (This is the common path, and it preserves intentional choices like 800K-as-HDD.)
3. Else the combo is **broken** → **block + guiding error** (§7). Do not mount; leave the
   slot empty. The message names where the image *should* go, e.g.:
   "This looks like a hard-disk image — load it into a Hard Disk slot (S0/S1)."

**No disk is ever auto-routed.** The mount either lands in the slot the user picked (when
valid) or is blocked with guidance. This keeps behavior fully predictable and never places
a disk where the user wasn't looking — including the intentional 800K-as-HDD choice, which
is simply a *valid* combo that lands where picked.

---

## 7. Broken-combo guard (OSD message)

Fires only when §6 step 3 determines the image can't work in the chosen slot (and isn't
being rerouted). **Detection is entirely MiSTer-side** (we already parsed the header and
know the slot), so use the **direct OSD popup**, not the core's `info_req` path:

- `InfoMessage(const char *msg, int timeout=2000, const char *title="Message")` — titled
  message box (`menu.cpp:7900`).  ← use this.
- `Info(const char *msg, ...)` — lighter overlay (`menu.cpp:7930`).

On any validation failure: **do not mount**, leave the slot empty, and show a specific
message, e.g.:
- "3.5″ drive needs an 800 KB disk — this image is a 32 MB hard disk."
- "5.25″ drive needs a 140 KB disk — this is an 800 KB 3.5″ image."
- "Unrecognized disk image."

> The core-driven `info_req`/`info` + CONF_STR `I` mechanism (`user_io.cpp:3669`,
> `show_core_info`) is **not used here** — it's for problems only the HDL can detect at
> runtime. Mount-time validation lives where the data already is.

---

## 8. Run-time translation (per-LBA)

Dispatched from `user_io.cpp:3256` (where `SD_TYPE_A2` is handled today). Add a per-slot
`header_offset[16]` array (model: the sim's `header_size[]`). Note `fileTYPE.offset` is the
running cursor, **not** a base offset, so header stripping must be explicit.

### 8.1 Hard disk — offset-mapped passthrough  (read/write, v1)
- Read/write `512×lba + header_offset[slot]`. No byte transform.
- `.hdv`/`.po`/raw: `header_offset = 0`.
- `.2mg`: `header_offset = data_offset`. Writes land in the payload; the static header is
  untouched → **fully writable, correct.**
- `.dc42`: `header_offset = 84`. Writes are correct, **but the header `dataChecksum`
  becomes stale.** v1 policy: **mount DC42 read-only** (set `img_readonly`), or (v2)
  recompute and rewrite the checksum on eject. Recommend read-only for v1.

### 8.2 Native WOZ floppy — flat block passthrough  (read; write within allocation)
- Served byte-for-byte; the core navigates TMAP/TRKS and requests blocks. `header_offset=0`.
- **Writes**: the core writes 512-byte blocks back into the existing TRKS BITS region;
  we pass them straight through. This works **only within the blocks already allocated to
  a track**. Per `wozreference.html`: unused tracks are `0xFF` in TMAP with zero-length
  TRKS, and tracks can vary in length. We **do not** grow/relocate/normalize (would risk
  copy protection), so:
  - Writes to already-formatted tracks: persist.
  - Writes that would need a new/larger track allocation: **cannot persist** in v1.
  - After any write the file's CRC32 is stale → **zero the CRC** field on first write (a
    0 CRC is explicitly "not computed" and accepted by readers).
- Document this as a known v1 limitation; full WOZ write support is a v2 topic.

### 8.3 Converted floppy (DSK/DO/PO/NIB/2MG-wrapped) → WOZ  (v2 write)
The core reads only WOZ, so non-WOZ floppies must be **synthesized into a WOZ on the fly**,
exactly like `a2_readDsk2Nib` synthesizes NIB per offset — but emitting a WOZ layout we
control. Because we choose the layout, we make it an **"easy WOZ"**: uniform per-track
block allocation, standard 6-and-2 (5.25″) / zoned GCR (3.5″) encoding, no copy-protection
tricks. That makes the inverse (WOZ→sectors→original file) trivial, so **writes round-trip**
back to the source `.dsk`/`.po` — the same model as the live `a2_writeNib2Dsk`.

- **5.25″** (`.dsk/.do/.po/.nib`, 140K / 2MG fmt 0/2): reuse the 6-and-2 nibblizer in
  `dsk2nib_lib.cpp`; bit-pack MSB-first into an easy-WOZ track; DOS vs ProDOS sector order
  comes from the source (`.do`/2MG fmt0 = DOS, `.po`/2MG fmt1 = ProDOS).
- **3.5″** (`.po`/`.2mg`/`.dc42`, 800K): zoned layout (12/11/10/9/8 sectors), 524-byte
  tag-padded sectors, the 3-byte scrambling GCR checksum. Hardest piece — crib from
  MAME / CiderPress2 / Applesauce reference code rather than deriving.

**Phasing:**
- **v1:** converted floppies are read-only.
- **v2 (done):** write-back enabled. On each block write the affected WOZ track is located
  (`a2_woz_track_for_lba`), decoded (`a2_woz35_decode_track` / `a2_woz525_decode_track`), and
  the resulting sectors are written to the source file — at the 2MG/DC42 header offset, and
  re-skewed DOS→ProDOS for `.po` 5.25 sources. Persisted per-write (survives no clean eject);
  unwritten blocks of an in-progress track still hold valid prior data, so partial-track
  writes converge correctly. `.nib` and DC42 sources remain read-only (DC42 = stale-checksum).

---

## 9. Code changes (integration map)

| File | Change |
|---|---|
| `DiskImage.cpp` / `DiskImage.h` | **Remove dead** `dsk2nib()` (defn `DiskImage.cpp:3315`, decl `DiskImage.h:6`) — no callers. Leave `x2trd*` (TR-DOS, still used). |
| `support/a2/` | New `iigs_disk.{cpp,h}`: `classify()`, 2MG/DC42/WOZ header parsers, slot routing, the easy-WOZ encoder (5.25″ now, 3.5″ later). Reuse `dsk2nib_lib.cpp` nibblizer. |
| `user_io.cpp` | (a) new `SD_TYPE_*` values for HDD-offset / WOZ-native / WOZ-converted; (b) classification + routing + strict guard in/around `user_io_file_mount` (~2084); (c) add `header_offset[]` and apply it in the block dispatch (~3256 / generic seek ~3312); (d) per-class branch in the §3256 dispatch. |
| `Apple-IIgs.sv` (core repo) | Widen `CONF_STR` slot extension lists (S0/S1 add `2MG`,`DC`; S2/S3 add `DSK/DO/PO/NIB/2MG`). HDL only — no logic change. |

The Makefile globs `support/*/*.cpp`, so new files in `support/a2/` need no Makefile edit.

---

## 10. Implementation phases

1. **Cleanup** — delete dead `dsk2nib()`; add `header_offset[]`; verify HDD `.po/.hdv`
   mount + read/write unchanged.
2. **HDD formats** — `classify()` + 2MG (port sim parser) → HDD offset passthrough.
   Strict guard + `InfoMessage`. (High value, low risk, writable.) **DC42 demoted** to a
   cheap `probeDC42()` content-detected fallback only — see §12; it is a Mac-native format
   that IIgs software essentially never ships as, so it gets no dedicated effort.
3. **Validation** — `accepts(slot,image)` check (HDD permissive, floppy strict); honor valid choices silently; block-with-guidance on broken combos.
4. **Native WOZ** — pass-through mount on S2/S3 with class validation (read; constrained write per §8.2).
5. **5.25″ conversion** — easy-WOZ encoder reusing the nibblizer; read-only (v1).
6. **3.5″ conversion** — zoned GCR easy-WOZ encoder; read-only (v1). **Done — boots GS/OS in sim.**
7. **v2 — done** — write round-trip for converted floppies (per-track decode → source).
   Remaining v2+: DC42 checksum rewrite on eject (to make DC42 floppies writable too).

> **Codec status:** the pure codec (`support/a2/iigs_fmt.{h,cpp}`) for phases 2/4/5/6 is
> written and tested (native round-trips + sim boot).
>
> **Integration status (done):** `support/a2/iigs_disk.{h,cpp}` wires it into MiSTer for
> the `Apple-IIgs` core only (guarded by `iigs_is_core()`), via a new `SD_TYPE_IIGS`
> dispatch branch in `user_io.cpp` (alongside, not replacing, `SD_TYPE_A2`). At mount it
> classifies + strictly validates the image against the slot (`InfoMessage` guidance on
> mismatch), strips 2MG/DC42 headers for HDD slots (offset serving, read/write), and
> converts `.po/.dsk/.do/.nib/.2mg` floppies to an in-memory WOZ (read-only, v1); native
> `.woz` and raw `.po/.hdv` pass through. Cross-compiles clean into the ARM binary; the
> Apple II NIB flow is untouched.
>
> **Remaining to expose the formats:** the core's `CONF_STR` extension lists must be
> widened (HDL, `Apple-IIgs.sv`) — today S0/S1 list only `HDVPO` and S2/S3 only `WOZ`, so
> the file browser won't *offer* `.2mg/.dsk/.po/...` for those slots even though the
> MiSTer side now handles them. Needs a one-line-per-slot edit + FPGA rebuild.

---

## 11. Open questions / risks

- **Easy-WOZ acceptance:** confirm the core's WOZ engine reads a minimal standard-layout
  WOZ (uniform track sizes) for converted disks. (Native protected WOZ is untouched.)
- **3.5″ WOZ — implemented and sim-validated.** Encode/decode ported from Clemens/CiderPress;
  round-trips on the real `System.Disk.po`, and a WOZ generated by `iigs_convert po2woz` from
  `System.Disk.po` **boots GS/OS 6.0.4** in the Verilator core (boot splash at frame 600;
  GS/OS running at frame 2200). The per-track block budget is fine as-is — no gap trimming
  needed. Covered by `iigs_regression.sh` (§12).
- **`.dsk` order ambiguity:** assume DOS 3.3 order (matches existing apple-ii behavior);
  revisit if IIgs `.dsk` images turn out ProDOS-ordered.
- **819,200-byte ProDOS HDD volume** misclassified as 3.5″ floppy (§4) — accepted edge case.
- **ShrinkIt `.shk/.sdk`** (compressed) is **out of scope** — needs a NuFX/LZW decoder.

---

## 12. Test fixtures

Real images collected in `artifacts_for_iigs/` (and the IIgs core repo). Every converter
input in this spec has at least one genuine fixture. Sizes are exact bytes.

### Hard disk (raw ProDOS blocks → offset passthrough, §8.1)
| File | Bytes | Exercises |
|---|---|---|
| `Live.Install.po` | 33,552,384 | large raw HDD `.po`, no header |
| `System.Disk.po` | 819,200 | 800K ProDOS `.po` (also valid as 3.5″ floppy — the intentional-overlap case, §6) |
| `iigs_simulation/.../blank.2mg` | 2,097,216 | **HDD 2MG** (fmt 1 ProDOS, 2 MB / 4096 blocks); `data_off=64`, `data_len=2,097,152` |

### 3.5″ floppy (800K → native WOZ §8.2 or convert §8.3)
| File | Bytes | Exercises |
|---|---|---|
| `Zany Golf.woz` | 1,296,664 | native **WOZ2, disk type 2 (3.5″)**, write-protected; slot-match check |
| `Arkanoid/Arkanoid.2mg` | 819,272 | 800K floppy 2MG with **8 trailing bytes** after payload → proves we strip via `data_len`, not `filesize−64` |
| `Airball/Airball.2mg` | 819,271 | 800K floppy 2MG, 7 trailing bytes (same edge case) |
| `System.Disk.po`, `Tour of the Apple IIGS *.po` | 819,200 | 800K ProDOS `.po` floppies |

### 5.25″ floppy (140K / NIB → native WOZ §8.2 or convert §8.3)
| File | Bytes | Exercises |
|---|---|---|
| `Zork I r15-UG3AU5.woz` | 234,846 | native **WOZ2, disk type 1 (5.25″)**; slot-match check |
| `Apple DOS 3.3 January 1983.dsk` (+ 22 more `.dsk`) | 143,360 | raw 140K DOS/ProDOS images → 6-and-2 nibblize → easy-WOZ |
| `Apple DOS 3.3 January 1983.do` | 143,360 | the `.do` extension branch (DOS-order; byte-identical copy of the `.dsk`) |
| `Apple DOS 3.3 January 1983.nib`, `Apple DOS 3.3 August 1980.nib` | 232,960 | pre-nibblized `.nib` → WOZ wrap path |

### WOZ skeleton (the "easy WOZ" blueprint, §8.3)
| File | Purpose |
|---|---|
| `iigs_simulation/tools/make_blank_woz.py` | Reference generator for the fully-pre-allocated WOZ the core's `woz_floppy_controller` needs to write back. Port to C for the converter. Defines 3.5″ speed-group block counts (19/18/16/14/13) and 5.25″ (51200 bits, 13 blocks/track). |
| `iigs_simulation/tools/blank_35.woz` | A pre-built blank 3.5″ easy-WOZ to compare encoder output against. |

### DC42 — parser-only, **not IIgs** (DC42 is demoted, §10)
| File | Bytes | Note |
|---|---|---|
| `Blank800K.img` | 819,284 | DiskCopy 4.2, `dataSize=819200`, `tagSize=0`, **formatByte 0x22 = Mac** (not 0x24 ProDOS). Mac HFS payload — exercises `probeDC42()` header parse only; will **not boot** a IIgs. |
| `Blank400K.img` | 419,284 | DiskCopy 4.2 with a real **9,600-byte tag fork** (800 blocks × 12); tests tag-skip + the `tagSize%12` / size-equation checks. Also Mac (formatByte 0x00). |

> Both DC42 files are Macintosh blanks kept **only** as unit-test inputs for `probeDC42()` —
> they are not representative IIgs disks. A genuine IIgs-bootable DC42 (formatByte 0x24) can
> be synthesized on demand by wrapping `System.Disk.po` with a correct big-endian header +
> DiskCopy checksum, but isn't needed unless DC42 is ever promoted past a fallback.

### Codec, tools & tests (in this repo)
| Path | Purpose |
|---|---|
| `support/a2/iigs_fmt.{h,cpp}` | Pure dependency-free codec (2MG/DC42/WOZ parse, DOS↔ProDOS, DSK↔NIB, DSK↔WOZ 5.25, PO↔WOZ 3.5). Cross-compiles into MiSTer. |
| `support/a2/tests/` | Native round-trip + real-fixture tests (`make run`). 64 checks, no sim needed. |
| `support/a2/tools/iigs_convert.cpp` | CLI: `po2woz`/`dsk2woz`/`woz2po`/`woz2dsk`/`classify`. Generates images for the sim. |
| `support/a2/tools/iigs_regression.sh` | End-to-end: native suite + convert real `.po`/`.dsk` → WOZ → **boot in the Verilator sim**, screenshot, diff against saved references (`--bless` to update). Mirrors the sim's `regression.sh`. |

**Validated:** `iigs_regression.sh` → PASS 4/0 — native suite green; `System.Disk.po` and
`Tour of the Apple IIGS 2.0.po` boot (BOOTED), `Apple DOS 3.3…dsk` boots (TEXT) — all via
WOZ produced by our codec.

### Gaps / synthesizable on demand
- A DOS-order **2MG (`image format`=0)** — all sample 2MGs are fmt 1; fake by flipping the
  format byte to test the "DOS-order can't go in a HDD slot" branch.
- Deliberately **wrong slot/disk combos** for the §7 guard — construct from the above
  (e.g. `Live.Install.po` into a floppy slot must block).
