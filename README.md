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

34 SQL functions in total: 24 scalar, 6 aggregate, 4 table-valued.

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
Returns one row per element in a 32-bit bitmap. The single output column is `value INTEGER`.

```sql
SELECT value FROM rb_each(rb_create(1, 2, 3));
-- 1
-- 2
-- 3

SELECT sum(value) FROM rb_each(bitmap) WHERE value BETWEEN 100 AND 200;
```

#### rb64_each(bitmap)
Same as `rb_each` but for 64-bit bitmaps. Supports values above 2³².

```sql
SELECT value FROM rb64_each(rb64_create(9000000000, 9000000001));
-- 9000000000
-- 9000000001
```

#### rb_array(bitmap)
Serializes a 32-bit bitmap to a raw `int32` array for use with the [carray](https://www.sqlite.org/carray.html) extension.

```sql
.load ./carray
SELECT sum(value) FROM carray(rb_array(bitmap), rb_count(bitmap));
```

#### rb64_array(bitmap)
Same as `rb_array` but produces an `int64` array.

```sql
SELECT sum(value) FROM carray(rb64_array(bitmap), rb64_count(bitmap), 'int64');
```

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
