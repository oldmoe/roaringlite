require 'minitest/autorun'
require 'extralite'

DB = Extralite::Database.new(":memory:")
#DB.enable_load_extension(true)
DB.load_extension("../dist/libroaring.so")
CARRAY_AVAILABLE = begin
  DB.load_extension("../dist/carray.so")
  true
rescue
  false
end
DB.execute("CREATE TABLE bitmaps(id INTEGER PRIMARY KEY, bitmap BLOB)")

class TestRoaringBitmap < Minitest::Test

  def test_rb_create
    result = DB.query_single_splat("SELECT rb_count(rb_create(1,2,3,7)) AS length")
    assert_equal 4, result
  end

  def test_rb64_create
    result = DB.query_single_splat("SELECT rb64_count(rb64_create(1,2,3,7)) AS length")
    assert_equal 4, result
  end
  

  def test_rb_create_and_serialize
    id = DB.query_single_splat("INSERT INTO bitmaps VALUES (NULL, rb_create(4,5,6,7,8)) RETURNING id")
    result = DB.query_single_splat("DELETE FROM bitmaps WHERE id = ? RETURNING rb_count(bitmap) AS length", id)
    assert_equal 5, result
  end

  def test_rb64_create_and_serialize
    id = DB.query_single_splat("INSERT INTO bitmaps VALUES (NULL, rb64_create(4,5,6,7,8)) RETURNING id")
    result = DB.query_single_splat("DELETE FROM bitmaps WHERE id = ? RETURNING rb64_count(bitmap) AS length", id)
    assert_equal 5, result
  end

  def test_rb_create_and_serialize_modify_and_reserialize
    id = DB.query_single_splat("INSERT INTO bitmaps VALUES (NULL, rb_create(4,5,6,7,8)) RETURNING id")
    DB.execute("UPDATE bitmaps SET bitmap = rb_add(bitmap, 1) WHERE id=?", id)
    result = DB.query_single_splat("DELETE FROM bitmaps WHERE id = ? RETURNING rb_count(bitmap) AS length", id)
    assert_equal 6, result
  end

  def test_rb64_create_and_serialize_modify_and_reserialize
    id = DB.query_single_splat("INSERT INTO bitmaps VALUES (NULL, rb64_create(4,5,6,7,8)) RETURNING id")
    DB.execute("UPDATE bitmaps SET bitmap = rb64_add(bitmap, 1) WHERE id=?", id)
    result = DB.query_single_splat("DELETE FROM bitmaps WHERE id = ? RETURNING rb64_count(bitmap) AS length", id)
    assert_equal 6, result
  end

  def test_rb_group_create
    result = DB.query_single_splat("SELECT rb_count(rb_group_create(value)) FROM JSON_EACH('[1,2,3,4,5]')")
    assert_equal 5, result
  end

  def test_rb64_group_create
    result = DB.query_single_splat("SELECT rb64_count(rb64_group_create(value)) FROM JSON_EACH('[1,2,3,4,5]')")
    assert_equal 5, result
  end


  def test_rb_add
    result = DB.query_single_splat("SELECT rb_count(rb_add(rb_create(1,2,3,4), 5))")
    assert_equal 5, result
  end

  def test_rb64_add
    result = DB.query_single_splat("SELECT rb64_count(rb64_add(rb64_create(1,2,3,4), 5))")
    assert_equal 5, result
  end

  def test_rb_add_duplicate
    result = DB.query_single_splat("SELECT rb_count(rb_add(rb_create(1,2,3,4), 4))")
    assert_equal 4, result
  end

  def test_rb64_add_duplicate
    result = DB.query_single_splat("SELECT rb64_count(rb64_add(rb64_create(1,2,3,4), 4))")
    assert_equal 4, result
  end

  def test_rb_remove
    result = DB.query_single_splat("SELECT rb_count(rb_remove(rb_create(1,2,3,4), 4))")
    assert_equal 3, result
  end

  def test_rb64_remove
    result = DB.query_single_splat("SELECT rb64_count(rb64_remove(rb64_create(1,2,3,4), 4))")
    assert_equal 3, result
  end

  def test_rb_remove_not_existing
    result = DB.query_single_splat("SELECT rb_count(rb_remove(rb_create(1,2,3,4), 5))")
    assert_equal 4, result
  end

  def test_rb64_remove_not_existing
    result = DB.query_single_splat("SELECT rb64_count(rb64_remove(rb64_create(1,2,3,4), 5))")
    assert_equal 4, result
  end

  def test_rb_and
    result = DB.query_single_splat("SELECT rb_count(rb_and(rb_create(1,2,3,4), rb_create(2,6,7,8)))")
    assert_equal 1, result
  end

  def test_rb64_and
    result = DB.query_single_splat("SELECT rb64_count(rb64_and(rb64_create(1,2,3,4), rb64_create(2,6,7,8)))")
    assert_equal 1, result
  end

  def test_rb_and_count
    result = DB.query_single_splat("SELECT rb_and_count(rb_create(1,2,3,4), rb_create(2,6,7,8))")
    assert_equal 1, result
  end

  def test_rb64_and_count
    result = DB.query_single_splat("SELECT rb64_and_count(rb64_create(1,2,3,4), rb64_create(2,6,7,8))")
    assert_equal 1, result
  end

  def test_rb_or
    result = DB.query_single_splat("SELECT rb_count(rb_or(rb_create(1,2,3,4), rb_create(2,6,7,8)))")
    assert_equal 7, result
  end

  def test64_rb_or
    result = DB.query_single_splat("SELECT rb64_count(rb64_or(rb64_create(1,2,3,4), rb64_create(2,6,7,8)))")
    assert_equal 7, result
  end

  def test_rb_or_count
    result = DB.query_single_splat("SELECT rb_or_count(rb_create(1,2,3,4), rb_create(2,6,7,8))")
    assert_equal 7, result
  end

  def test_rb64_or_count
    result = DB.query_single_splat("SELECT rb64_or_count(rb64_create(1,2,3,4), rb64_create(2,6,7,8))")
    assert_equal 7, result
  end

  def test_rb_xor
    result = DB.query_single_splat("SELECT rb_count(rb_xor(rb_create(1,2,3,4), rb_create(2,6,7,8)))")
    assert_equal 6, result
  end

  def test_rb64_xor
    result = DB.query_single_splat("SELECT rb64_count(rb64_xor(rb64_create(1,2,3,4), rb64_create(2,6,7,8)))")
    assert_equal 6, result
  end

  def test_rb_xor_count
    result = DB.query_single_splat("SELECT rb_xor_count(rb_create(1,2,3,4), rb_create(2,6,7,8))")
    assert_equal 6, result
  end

  def test_rb64_xor_count
    result = DB.query_single_splat("SELECT rb64_xor_count(rb64_create(1,2,3,4), rb64_create(2,6,7,8))")
    assert_equal 6, result
  end

  def test_rb_not
    result = DB.query_single_splat("SELECT rb_count(rb_not(rb_create(1,2,3,4), rb_create(2,6,7,8)))")
    assert_equal 3, result
  end

  def test_rb64_not
    result = DB.query_single_splat("SELECT rb64_count(rb64_not(rb64_create(1,2,3,4), rb64_create(2,6,7,8)))")
    assert_equal 3, result
  end


  def test_rb_not_count
    result = DB.query_single_splat("SELECT rb_not_count(rb_create(1,2,3,4), rb_create(2,6,7,8))")
    assert_equal 3, result
  end

  def test_rb64_not_count
    result = DB.query_single_splat("SELECT rb64_not_count(rb64_create(1,2,3,4), rb64_create(2,6,7,8))")
    assert_equal 3, result
  end

  def test_rb_array
    skip "carray not available" unless CARRAY_AVAILABLE
    result = DB.query_single_splat("SELECT sum(value) FROM carray(rb_array(rb_create(1, 10, 100, 1000)), 4)")
    assert_equal 1111, result
  end

  def test_rb64_array
    skip "carray not available" unless CARRAY_AVAILABLE
    result = DB.query_single_splat("SELECT sum(value) FROM carray(rb64_array(rb64_create(1, 10, 100, 1000)), 4, 'int64')")
    assert_equal 1111, result
  end


  def test_rb_group_and
    DB.execute("INSERT INTO bitmaps(bitmap) VALUES (rb_create(1,2,3,4)), (rb_create(4)), (rb_create(4,7))")
    result = DB.query_single_splat("SELECT rb_count(rb_group_and(bitmap)) FROM bitmaps")
    DB.execute("DELETE FROM bitmaps")
    assert_equal 1, result
  end

  def test_rb64_group_and
    DB.execute("INSERT INTO bitmaps(bitmap) VALUES (rb64_create(1,2,3,4)), (rb64_create(4)), (rb64_create(4,7))")
    result = DB.query_single_splat("SELECT rb64_count(rb64_group_and(bitmap)) FROM bitmaps")
    DB.execute("DELETE FROM bitmaps")
    assert_equal 1, result
  end

  def test_rb_group_and_with_null
    DB.execute("INSERT INTO bitmaps(bitmap) VALUES (rb_create(1,2,3,4)), (NULL), (rb_create(4,7))")
    assert_raises do
      DB.query_single_splat("SELECT rb_count(rb_group_and(bitmap)) FROM bitmaps")
    end
    DB.execute("DELETE FROM bitmaps")
  end

  def test_rb64_group_and_with_null
    DB.execute("INSERT INTO bitmaps(bitmap) VALUES (rb64_create(1,2,3,4)), (NULL), (rb64_create(4,7))")
    assert_raises do
      DB.query_single_splat("SELECT rb64_count(rb64_group_and(bitmap)) FROM bitmaps")
    end
    DB.execute("DELETE FROM bitmaps")
  end

  def test_rb_group_or
    DB.execute("INSERT INTO bitmaps(bitmap) VALUES (rb_create(1,2,3,4)), (rb_create(4)), (rb_create(4,7))")
    result = DB.query_single_splat("SELECT rb_count(rb_group_or(bitmap)) AS length FROM bitmaps")
    DB.execute("DELETE FROM bitmaps")
    assert_equal 5, result
  end

  def test_rb64_group_or
    DB.execute("INSERT INTO bitmaps(bitmap) VALUES (rb64_create(1,2,3,4)), (rb64_create(4)), (rb64_create(4,7))")
    result = DB.query_single_splat("SELECT rb64_count(rb64_group_or(bitmap)) AS length FROM bitmaps")
    DB.execute("DELETE FROM bitmaps")
    assert_equal 5, result
  end

  def test_rb_each_count
    result = DB.query_single_splat("SELECT count(*) FROM rb_each(rb_create(1,2,3,7))")
    assert_equal 4, result
  end

  def test_rb_each_sum
    result = DB.query_single_splat("SELECT sum(value) FROM rb_each(rb_create(1,10,100))")
    assert_equal 111, result
  end

  def test_rb_each_min
    assert_equal 1, DB.query_single_splat("SELECT min(value) FROM rb_each(rb_create(5,3,9,1))")
  end

  def test_rb_each_max
    assert_equal 9, DB.query_single_splat("SELECT max(value) FROM rb_each(rb_create(5,3,9,1))")
  end

  def test_rb_each_empty
    result = DB.query_single_splat("SELECT count(*) FROM rb_each(rb_create())")
    assert_equal 0, result
  end

  def test_rb_each_null
    result = DB.query_single_splat("SELECT count(*) FROM rb_each(NULL)")
    assert_equal 0, result
  end

  def test_rb_each_roundtrip
    result = DB.query_single_splat("SELECT rb_count(rb_group_create(value)) FROM rb_each(rb_create(4,3,2,1,5))")
    assert_equal 5, result
  end

  def test_rb64_each_count
    result = DB.query_single_splat("SELECT count(*) FROM rb64_each(rb64_create(1,2,3,7))")
    assert_equal 4, result
  end

  def test_rb64_each_sum
    result = DB.query_single_splat("SELECT sum(value) FROM rb64_each(rb64_create(1,10,100))")
    assert_equal 111, result
  end

  def test_rb64_each_min
    assert_equal 1, DB.query_single_splat("SELECT min(value) FROM rb64_each(rb64_create(5,3,9,1))")
  end

  def test_rb64_each_max
    assert_equal 9, DB.query_single_splat("SELECT max(value) FROM rb64_each(rb64_create(5,3,9,1))")
  end

  def test_rb64_each_empty
    result = DB.query_single_splat("SELECT count(*) FROM rb64_each(rb64_create())")
    assert_equal 0, result
  end

  def test_rb64_each_null
    result = DB.query_single_splat("SELECT count(*) FROM rb64_each(NULL)")
    assert_equal 0, result
  end

  def test_rb64_each_large_values
    result = DB.query_single_splat("SELECT count(*) FROM rb64_each(rb64_create(9000000000,9000000001,9000000002))")
    assert_equal 3, result
  end

  def test_rb64_each_large_sum
    result = DB.query_single_splat("SELECT sum(value) FROM rb64_each(rb64_create(9000000000,9000000001,9000000002))")
    assert_equal 27000000003, result
  end

  def test_rb64_each_roundtrip
    result = DB.query_single_splat("SELECT rb64_count(rb64_group_create(value)) FROM rb64_each(rb64_create(4,3,2,1,5))")
    assert_equal 5, result
  end

end

