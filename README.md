![roaring](https://github.com/oldmoe/roaringlite/blob/main/media/roaring_cat_s.jpg?raw=true)

Image courtesy of Madeleine on Flickr (creative commons license)

# RoaringLite
Roaring Bitmaps extension for SQLite

This is a SQLite wrapper for the [CRoaring](https://github.com/RoaringBitmap/CRoaring) implementation of the Roaring Bitmaps data structure.

## What are Roaring Bitmaps
Roaring bitmaps are compressed bitsets that can store integer values and apply set operations to them (AND, OR, NOT, XOR). The compressed internal representation is much smaller than typical bitsets while remaining fast thanks to heavy use of SIMD CPU instructions.

Two variants are supported:
- **32-bit** (`rb_*`) — stores `uint32` values (0 to 4,294,967,295)
- **64-bit** (`rb64_*`) — stores `uint64` values, including values above 2³²

## Compiling this extension

First generate the CRoaring amalgamation (requires the CRoaring submodule):

```bash
git submodule update --init
mkdir -p src
cd CRoaring && ./amalgamation.sh ../src && cd ..
```

Then compile:

```bash
gcc -fPIC -shared -O2 -I src src/libsqlite3roaring.c -o libroaring.so
```

### Cross-platform builds

`build.Dockerfile` cross-compiles for macOS (x86_64 + aarch64) and Linux (x86_64 + aarch64) using Zig and also runs the test suite:

```bash
docker build -f build.Dockerfile --output dist .
```

## Using Roaring bitmaps with SQLite

Load the extension before use:

#### SQLite3 CLI
```
.load ./libroaring
```

#### Ruby's Extralite
```ruby
require 'extralite'
db = Extralite::Database.new(":memory:")
db.load_extension("./libroaring")
```

## API

Each function has a 32-bit (`rb_*`) and a 64-bit (`rb64_*`) variant. The 64-bit variants accept and return `INTEGER` values up to `2^63 - 1` (SQLite's signed 64-bit range) and can store values beyond the 32-bit limit.

---

### Scalar functions

#### rb_create(arg1, arg2, .., argN) / rb64_create(...)
Creates and serializes a new bitmap. Accepts any number of arguments. Returns an empty bitmap if called with no arguments.

#### rb_count(bitmap) / rb64_count(bitmap)
Returns the number of elements in the bitmap.

#### rb_add(bitmap, value) / rb64_add(bitmap, value)
Adds a value to the bitmap. No-op if the value already exists.

#### rb_remove(bitmap, value) / rb64_remove(bitmap, value)
Removes a value from the bitmap. No-op if the value doesn't exist.

#### rb_and(bitmap1, bitmap2) / rb64_and(...)
Returns a bitmap that is the intersection (AND) of the two inputs.

#### rb_and_count(bitmap1, bitmap2) / rb64_and_count(...)
Returns the cardinality of the intersection without materializing the bitmap.

```sql
SELECT rb_count(rb_and(bitmap1, bitmap2)); -- slower
SELECT rb_and_count(bitmap1, bitmap2);     -- faster
```

#### rb_or(bitmap1, bitmap2) / rb64_or(...)
Returns a bitmap that is the union (OR) of the two inputs.

#### rb_or_count(bitmap1, bitmap2) / rb64_or_count(...)
Returns the cardinality of the union without materializing the bitmap.

#### rb_xor(bitmap1, bitmap2) / rb64_xor(...)
Returns a bitmap that is the symmetric difference (XOR) of the two inputs.

#### rb_xor_count(bitmap1, bitmap2) / rb64_xor_count(...)
Returns the cardinality of the symmetric difference without materializing the bitmap.

#### rb_not(bitmap1, bitmap2) / rb64_not(...)
Returns a bitmap that is the difference (ANDNOT) of bitmap1 minus bitmap2.

#### rb_not_count(bitmap1, bitmap2) / rb64_not_count(...)
Returns the cardinality of the difference without materializing the bitmap.

#### rb_contains(bitmap, value) / rb64_contains(bitmap, value)
Returns `1` if `value` is present in the bitmap, `0` otherwise. Returns `0` for a `NULL` bitmap or an out-of-range value.

```sql
SELECT rb_contains(rb_create(1, 2, 3), 2); -- 1
SELECT rb_contains(rb_create(1, 2, 3), 4); -- 0
SELECT rb_contains(NULL, 1);               -- 0
```

The deserialized bitmap is cached in SQLite auxiliary data for the duration of the prepared statement, so calling `rb_contains` repeatedly with the same bitmap argument (e.g. in a `WHERE` clause over many rows) only deserializes once.

---

### Aggregate functions

#### rb_group_create(col) / rb64_group_create(col)
Aggregates a column of integers into a single bitmap.

```sql
SELECT rb_group_create(id) FROM books; -- bitmap of all book ids
```

#### rb_group_and(col) / rb64_group_and(col)
ANDs all bitmaps in a column. Faster than chaining `rb_and` pairwise. Expects no NULL values.

#### rb_group_or(col) / rb64_group_or(col)
ORs all bitmaps in a column. Faster than chaining `rb_or` pairwise.

---

### Table-valued functions

#### rb_each(bitmap)
Returns one row per element in a 32-bit bitmap. The output column is `value INTEGER`.

Iterates values in ascending order by default. Supports `ORDER BY value ASC` and `ORDER BY value DESC` with `orderByConsumed`, so SQLite will not apply a redundant sort.

```sql
SELECT value FROM rb_each(rb_create(1, 2, 3));
-- 1
-- 2
-- 3

-- Descending, no extra sort step
SELECT value FROM rb_each(rb_create(1, 2, 3)) ORDER BY value DESC;
-- 3
-- 2
-- 1

-- Descending with limit — stops early without scanning the full bitmap
SELECT value FROM rb_each(bitmap) ORDER BY value DESC LIMIT 10;
```

#### rb64_each(bitmap)
Same as `rb_each` but for 64-bit bitmaps. Supports values above 2³².

```sql
SELECT value FROM rb64_each(rb64_create(9000000000, 9000000001));
-- 9000000000
-- 9000000001
```

#### rb_range(bitmap, offset, limit) / rb_range_desc(bitmap, offset, limit)
Returns a slice of sorted bitmap values without expanding the full bitmap.

- `offset` — number of values to skip from the start (ascending) or end (descending). Must be ≥ 0.
- `limit` — maximum number of values to return. Must be ≥ 0. Omit for no limit.

`rb_range` advertises `orderByConsumed` for `ORDER BY value ASC`; `rb_range_desc` advertises it for `ORDER BY value DESC`.

```sql
-- Values at positions 10–29 (0-based)
SELECT value FROM rb_range(bitmap, 10, 20);

-- Last 20 values
SELECT value FROM rb_range_desc(bitmap, 0, 20);

-- Page 2 of 20, descending
SELECT value FROM rb_range_desc(bitmap, 20, 20);
```

#### rb_array(bitmap) / rb64_array(bitmap)
Serializes a bitmap to a raw integer array for use with the [carray](https://www.sqlite.org/carray.html) extension.

```sql
.load ./carray
SELECT sum(value) FROM carray(rb_array(bitmap), rb_count(bitmap));
```

---

## Choosing the right primitive

**Use `rb_each(bitmap)`** when:
- The bitmap is small
- You need all values
- The desired order is ascending integer value

**Use `rb_contains(bitmap, value)`** when:
- Another table or index should drive the scan order
- The bitmap is large and you only need a small `LIMIT`
- The desired order comes from an external index, not the bitmap itself

```sql
-- Scan an ordered index, use the bitmap as a membership filter
WITH bm AS MATERIALIZED (
  SELECT rb_group_and(bitmap) AS b FROM bitmap_table WHERE key IN (?, ?)
)
SELECT t.*
FROM bm JOIN ordered_table t INDEXED BY ordered_index
WHERE rb_contains(bm.b, t.id)
ORDER BY t.sort_key DESC
LIMIT 20;
```

**Use `rb_range(bitmap, offset, limit)`** when:
- The bitmap's integer values are themselves the desired order (e.g. rank IDs, timestamp buckets)
- You want cursor/page-style access into the bitmap without expanding it fully

```sql
-- Page 3 of results, 20 per page, ascending
SELECT value FROM rb_range(bitmap, 40, 20);

-- Top 20 by descending value
SELECT value FROM rb_range_desc(bitmap, 0, 20);
```

---

## NULL and error behaviour

| Input | Behaviour |
|---|---|
| `NULL` bitmap in `rb_contains` | returns `0` |
| `NULL` bitmap in table-valued functions | zero rows |
| Invalid bitmap blob | SQLite error |
| Value < 0 or > `UINT32_MAX` in 32-bit functions | returns `0` |
| Value < 0 in 64-bit functions | returns `0` |
| `rb_range` with negative offset or limit | SQLite error |
| `rb_range` with `limit = 0` | zero rows |
| `rb_range` with offset ≥ cardinality | zero rows |
| `rb_first` / `rb_last` / `rb_nth` on empty bitmap | `NULL` |

---

## Testing

Tests are written in Ruby using Minitest. Run them via Docker (no local dependencies needed):

```bash
docker build -f build.Dockerfile .
```

The test stage installs Ruby and the Extralite gem, compiles a native `libroaring.so`, and runs `test/test_roaring_bitmaps.rb`. The build fails if any test fails.

To run locally, install the `minitest` and `extralite` gems and compile the library first:

```bash
cd test && ruby test_roaring_bitmaps.rb
```
