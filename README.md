![roaring](https://github.com/oldmoe/roaringlite/blob/main/media/roaring_cat_s.jpg?raw=true)

Image courtesty of Madeleine on Flickr (creative commons license)

# RoaringLite
Roaring Bitmaps extension for SQLite

This is a SQLite wrapper for the [CRoaring](https://github.com/RoaringBitmap/CRoaring) implementation of the Roaring Bitmaps data structure.

## What are Roaring Bitmaps
Roaring bitmaps are compressed bitsets that are able to store int32 values and apply set operations to them (AND, OR, NOT, XOR). The compressed nature of Roaring bitmaps results in much smaller sizes than typical bitsets

For example a bitset containing just the number 37 would look like this

```
00000000 00000000 00000000 00000000 00001000  
```
You can imagine if we want to store larger numbers, those will consume a lot of storage

Roaring bitmaps have a compressed internal representation that is a lot more efficient at storing those bits yet still sufficiently fast in set operations (pretty fast actually with heavy usage of modern SIMD CPU instructions)

## Compiling this extension

To compile and use this extension you should run the following command line

```bash
gcc -g -fPIC -shared libsqlite3roaring.c -o libroaring.so
```

## Using Roaring bitmaps with SQLite

In order to use the library you need to load the extension first

#### SQLite3 CLI
```
.load ./libraoring
```
Or you can load it via your favourite SQLite3 driver, e.g.

#### Ruby's amazing Extralite 
```ruby
require 'extralite'
db = Extralite::Database.new(":memory:")
db.load_extension("./libroaring")  
```

## API
This extension exposes 16 sql functions, 12 scalar, 3 aggregate and 1 that is an interface to the carray virtual table extension

### Scalar functions

#### rb_create(arg1, arg2, arg3, .., argN)
Creates and serializes a new Roaring Bitmap, accepts an arbitrary number of arguments which are all added to the created bitmap. Will create an empty bitmap if no arguments are provided

#### rb_count(bitmap)
Returns the number of elements (int32 values) in the bitmap

#### rb_add(bitmap, value)
Adds a value to the bitmap, won't complain if the value already exists

#### rb_remove(bitmap, value)
Removes a value from the bitmap, won't complain if the value doesn't exists

#### rb_and(bitmap1, bitmap2)
Creates and serializes a bitmap that is the result of ANDing the two supplied bitmaps

#### rb_and_count(bitmap1, bitmap2)
Returns the count of the ANDed values (faster since no bitmap is created)

```sql
SELECT rb_count(rb_and(bitmap1, bitmap2)); -- slower
SELECT rb_and_count(bitmap1, bitmap2); -- faster
```
#### rb_or(bitmap1, bitmap2)
Creates and serializes a bitmap that is the result of ORing the two supplied bitmaps

#### rb_or_count(bitmap1, bitmap2)
Returns the count of the ORed values (faster since no bitmap is created)

#### rb_xor(bitmap1, bitmap2)
Creates and serializes a bitmap that is the result of XORing the two supplied bitmaps

#### rb_xor_count(bitmap1, bitmap2)
Returns the count of the XORed values (faster since no bitmap is created)

#### rb_not(bitmap1, bitmap2)
Creates and serializes a bitmap that is the result of ANDNOTing the two supplied bitmaps

#### rb_not_count(bitmap1, bitmap2)
Returns the count of the ANDNOTed values (faster since no bitmap is created)

### Aggregate functions

#### rb_group_create(col)
Creates and serializes a bitmap from aggregated column values, e.g.

```sql
SELECT rb_group_create(id) FROM books; -- generates a bitmap with all the book ids
```
#### rb_group_and(col)
Performs an AND on all the values returned from a query, much faster than using rb_and on each pair due to saved de/serialization time. Expects no null values.

#### rb_group_or(col)
Performs an OR on all the values returned from a query, much faster than using rb_and on each pair due to saved de/serialization time. Expects no null values.

### Table valued functions

#### rb_array(bitmap)
rb_array transforms the bitmap to an int32 array that interfaces with the carray sqlite3 exetnsion

```sql
.load ./libroraing
.load ./carray
SELECT sum(value) FROM carray(rb_array(bitmap), rb_count(bitmap)); -- the count must be supplied
```

## Testing
A test script (in Ruby) is supplied and it requires the Extralite gem

```bash
ruby test_roaring_bitmaps.rb
```

## TODO

- Implement the rest of the Roaring bitmap functions
- Implement a native table valued function interface instead of relying on carray
- Implement the Roaring64 version (once an official release is out)
