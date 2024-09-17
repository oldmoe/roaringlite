#include <stddef.h>
#include <sqlite3ext.h>
#include "roaring.c"
SQLITE_EXTENSION_INIT1

/* Insert your extension code here */


static void roaringFreeFunc(roaring_bitmap_t *p){
  roaring_bitmap_free(p);
}


static void roaringArrayFreeFunc(int *p){
  sqlite3_free(p);
}

/*********************************************
  rb_create(e1, e2, e3, .. , en)
  --------------------------------------------
  creates a new bitmap comprised of all the supplied integers
*********************************************/
static void roaringCreateFunc(  
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  roaring_bitmap_t *r = roaring_bitmap_create();
  for(int i=0; i < argc; i++){
    if( sqlite3_value_type(argv[i])!=SQLITE_INTEGER ){
      sqlite3_result_error(context, "invalid argument", -1);
      return;
    }
    roaring_bitmap_add(r, sqlite3_value_int(argv[i]));
  }
  int nSize = (int )roaring_bitmap_size_in_bytes(r);
  char *pOut = sqlite3_malloc(nSize);
  int nOut = (int) roaring_bitmap_serialize(r, pOut);
  roaring_bitmap_free(r);  
  sqlite3_result_blob(context, pOut, nOut, sqlite3_free); 
}

/*
  struct to hold a roaring bitmap
*/
typedef struct RoaringContext RoaringContext;
struct RoaringContext {
  unsigned init;
  roaring_bitmap_t *rb;
};

/*********************************************
  rb_group_create(col) 
  --------------------------------------------
  creates a bitmap from a SQL aggregation
  
  example: SELECT rb_group_create(col) FROM table;
*********************************************/
static void roaringCreateStep(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){

  RoaringContext *rc;

  // bail out if the value supplied is not an integer
  if( sqlite3_value_type(argv[0])!=SQLITE_INTEGER ){
    sqlite3_result_error(context, "invalid argument", -1);
    return;
  }

  // check if the context already has the roaring bitmap  
  rc = (RoaringContext*)sqlite3_aggregate_context(context, sizeof(*rc));
  if(rc->init == 0){
    // create a roaring bitmap
    rc->rb = roaring_bitmap_create();
    if(rc->rb == NULL){
      memset(rc, 0, sizeof(*rc)); 
      sqlite3_result_error(context, "failed to create bitmap in step", -1);
      return;
    }
    rc->init = 1;
  }
  roaring_bitmap_add(rc->rb, sqlite3_value_int64(argv[0]));
}

static void roaringCreateFinal(sqlite3_context *context){
  RoaringContext *rc;
  int nOut, nSize;
  
  rc = (RoaringContext*)sqlite3_aggregate_context(context, sizeof(*rc));
  if(rc->rb == NULL){
    // no rb was created, must be an empty result set
    rc->rb = roaring_bitmap_create();    
  }
  nSize = (int )roaring_bitmap_size_in_bytes(rc->rb);
  char *pOut = sqlite3_malloc(nSize);
  nOut = (int) roaring_bitmap_serialize(rc->rb, pOut);
  roaring_bitmap_free(rc->rb); 
  memset(rc, 0, sizeof(*rc)); 
  sqlite3_result_blob(context, pOut, nOut, sqlite3_free);  
}
/*
static void roaringCreatePFinal(sqlite3_context *context){
  RoaringContext *rc;
  int nOut, nSize;
  rc = (RoaringContext*)sqlite3_aggregate_context(context, sizeof(*rc));
  nSize = (int )roaring_bitmap_size_in_bytes(rc->rb);
  sqlite3_result_pointer(context, rc->rb, "rbitmap", roaringFreeFunc);  
}
*/
/*********************************************
  rb_add(bitmap, element)
  --------------------------------------------
  adds an element to the bitmap and returns the new bitmap
*********************************************/
static void roaringAddFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *pIn;
  unsigned int nIn;  
  pIn = sqlite3_value_blob(argv[0]);
  nIn = sqlite3_value_bytes(argv[0]);
  roaring_bitmap_t *r = roaring_bitmap_deserialize_safe(pIn, nIn);
  if( r == NULL ){
    sqlite3_result_error(context, "invalid bitmap", -1);
    return;
  }
  if( sqlite3_value_type(argv[1])!=SQLITE_INTEGER ){
    sqlite3_result_error(context, "invalid argument", -1);
    return;
  }
  roaring_bitmap_add(r, sqlite3_value_int(argv[1]));
  int nSize = (int) roaring_bitmap_size_in_bytes(r);
  char *pOut = sqlite3_malloc(nSize);
  int nOut = (int) roaring_bitmap_serialize(r, pOut);
  roaring_bitmap_free(r);  
  sqlite3_result_blob(context, pOut, nOut, sqlite3_free);  
}

/*********************************************
  rb_remove(bitmap, element)
  --------------------------------------------
  removes an element from a bitmap and returns the new bitmap
*********************************************/
static void roaringRemoveFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *pIn;
  unsigned int nIn;  
  pIn = sqlite3_value_blob(argv[0]);
  nIn = sqlite3_value_bytes(argv[0]);
  roaring_bitmap_t *r = roaring_bitmap_deserialize_safe(pIn, nIn);
  if( r == NULL ){
    sqlite3_result_error(context, "invalid bitmap", -1);
    return;
  }
  if( sqlite3_value_type(argv[1])!=SQLITE_INTEGER ){
    sqlite3_result_error(context, "invalid argument", -1);
    return;
  }
  roaring_bitmap_remove(r, sqlite3_value_int(argv[1]));
  int nSize = (int) roaring_bitmap_size_in_bytes(r);
  char *pOut = sqlite3_malloc(nSize);
  int nOut = (int) roaring_bitmap_serialize(r, pOut);
  roaring_bitmap_free(r);  
  sqlite3_result_blob(context, pOut, nOut, sqlite3_free);  
}

/*********************************************
  rb_and_length(bitmap1, bitmap2)
  --------------------------------------------
  and the second bitmap with the first one (first one is modified) and returns the length of the result
*********************************************/
static void roaringAndLengthFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *pIn1;
  unsigned int nIn1;  
  const unsigned char *pIn2;
  unsigned int nIn2;  
  pIn1 = sqlite3_value_blob(argv[0]);
  nIn1 = sqlite3_value_bytes(argv[0]);
  pIn2 = sqlite3_value_blob(argv[1]);
  nIn2 = sqlite3_value_bytes(argv[1]);
  roaring_bitmap_t *r1 = roaring_bitmap_deserialize_safe(pIn1, nIn1);
  roaring_bitmap_t *r2 = roaring_bitmap_deserialize_safe(pIn2, nIn2);
  if( r1 == NULL || r2 == NULL){
    sqlite3_result_error(context, "invalid bitmap(s)", -1);
    return;
  }
  int nOut = (int) roaring_bitmap_and_cardinality(r1, r2);
  roaring_bitmap_free(r1);  
  roaring_bitmap_free(r2);  
  sqlite3_result_int(context, nOut);
}

/*********************************************
  rb_and_many(bitmap1, bitmap2, bitmap3, ...)
  --------------------------------------------
  Bitwise AND all bitmaps and return the result
*********************************************/

static void roaringAndManyFunc(  
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *pIn;
  unsigned int nIn;  
  roaring_bitmap_t *rfinal;
  roaring_bitmap_t *r;
  for(int i=0; i < argc; i++){
    if(sqlite3_value_type(argv[i])!=SQLITE_BLOB){
      continue;
    }
    r = roaring_bitmap_deserialize_safe(sqlite3_value_blob(argv[i]), sqlite3_value_bytes(argv[i]));     
    if( r == NULL){
      sqlite3_result_error(context, "invalid bitmap(s)", -1);
      return;
    }
    if(rfinal == NULL){
      rfinal = roaring_bitmap_copy(r);
    } else {
      roaring_bitmap_and_inplace(rfinal, r);
      roaring_bitmap_free(r);
    }
  }
  if(rfinal == NULL){
    rfinal = roaring_bitmap_create();
  }
  int nSize = (int )roaring_bitmap_size_in_bytes(rfinal);
  char *pOut = sqlite3_malloc(nSize);
  int nOut = (int) roaring_bitmap_serialize(rfinal, pOut);
  roaring_bitmap_free(rfinal);  
  sqlite3_result_blob(context, pOut, nOut, sqlite3_free); 
}

/*********************************************
  rb_or_many(bitmap1, bitmap2, bitmap3, ...)
  --------------------------------------------
  Bitwise OR all bitmaps and return the result
*********************************************/

static void roaringOrManyFunc(  
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *pIn;
  unsigned int nIn;  
  roaring_bitmap_t *rfinal;
  roaring_bitmap_t *r;
  rfinal = roaring_bitmap_create();
  for(int i=0; i < argc; i++){
    if(sqlite3_value_type(argv[i])!=SQLITE_BLOB){
      continue;
    }
    r = roaring_bitmap_deserialize_safe(sqlite3_value_blob(argv[i]), sqlite3_value_bytes(argv[i]));     
    if( r == NULL){
      sqlite3_result_error(context, "invalid bitmap(s)", -1);
      return;
    }
    roaring_bitmap_or_inplace(rfinal, r);
    roaring_bitmap_free(r);
  }
  int nSize = (int )roaring_bitmap_size_in_bytes(rfinal);
  char *pOut = sqlite3_malloc(nSize);
  int nOut = (int) roaring_bitmap_serialize(rfinal, pOut);
  roaring_bitmap_free(rfinal);  
  sqlite3_result_blob(context, pOut, nOut, sqlite3_free); 
}



/*********************************************
  rb_and(bitmap1, bitmap2)
  --------------------------------------------
  and the second bitmap with the first one (first one is modified) and returns the first bitmap
*********************************************/
static void roaringAndFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *pIn1;
  unsigned int nIn1;  
  const unsigned char *pIn2;
  unsigned int nIn2;  
  pIn1 = sqlite3_value_blob(argv[0]);
  nIn1 = sqlite3_value_bytes(argv[0]);
  pIn2 = sqlite3_value_blob(argv[1]);
  nIn2 = sqlite3_value_bytes(argv[1]);
  roaring_bitmap_t *r1 = roaring_bitmap_deserialize_safe(pIn1, nIn1);
  roaring_bitmap_t *r2 = roaring_bitmap_deserialize_safe(pIn2, nIn2);
  if( r1 == NULL || r2 == NULL){
    sqlite3_result_error(context, "invalid bitmap(s)", -1);
    return;
  }
  roaring_bitmap_and_inplace(r1, r2);
  int nOut, nSize;
  nSize = (int) roaring_bitmap_size_in_bytes(r1);
  char *pOut = sqlite3_malloc(nSize);
  nOut = (int) roaring_bitmap_serialize(r1, pOut);
  roaring_bitmap_free(r1);  
  roaring_bitmap_free(r2);  
  sqlite3_result_blob(context, pOut, nOut, sqlite3_free);  
}

/*********************************************
  rb_not(bitmap1, bitmap2)
  --------------------------------------------
  and the second bitmap with the first one (first one is modified) and returns the first bitmap
*********************************************/
static void roaringNotFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *pIn1;
  unsigned int nIn1;  
  const unsigned char *pIn2;
  unsigned int nIn2;  
  pIn1 = sqlite3_value_blob(argv[0]);
  nIn1 = sqlite3_value_bytes(argv[0]);
  pIn2 = sqlite3_value_blob(argv[1]);
  nIn2 = sqlite3_value_bytes(argv[1]);
  roaring_bitmap_t *r1 = roaring_bitmap_deserialize_safe(pIn1, nIn1);
  roaring_bitmap_t *r2 = roaring_bitmap_deserialize_safe(pIn2, nIn2);
  if( r1 == NULL || r2 == NULL){
    sqlite3_result_error(context, "invalid bitmap(s)", -1);
    return;
  }
  roaring_bitmap_andnot_inplace(r1, r2);
  int nOut, nSize;
  nSize = (int) roaring_bitmap_size_in_bytes(r1);
  char *pOut = sqlite3_malloc(nSize);
  nOut = (int) roaring_bitmap_serialize(r1, pOut);
  roaring_bitmap_free(r1);  
  roaring_bitmap_free(r2);  
  sqlite3_result_blob(context, pOut, nOut, sqlite3_free);  
}

/*********************************************
  rb_not_length(bitmap1, bitmap2)
  --------------------------------------------
  and the second bitmap with the first one (first one is modified) and returns the first bitmap
*********************************************/
static void roaringNotLengthFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *pIn1;
  unsigned int nIn1;  
  const unsigned char *pIn2;
  unsigned int nIn2;  
  pIn1 = sqlite3_value_blob(argv[0]);
  nIn1 = sqlite3_value_bytes(argv[0]);
  pIn2 = sqlite3_value_blob(argv[1]);
  nIn2 = sqlite3_value_bytes(argv[1]);
  roaring_bitmap_t *r1 = roaring_bitmap_deserialize_safe(pIn1, nIn1);
  roaring_bitmap_t *r2 = roaring_bitmap_deserialize_safe(pIn2, nIn2);
  if( r1 == NULL || r2 == NULL){
    sqlite3_result_error(context, "invalid bitmap(s)", -1);
    return;
  }
  int nOut = (int) roaring_bitmap_andnot_cardinality(r1, r2);
  roaring_bitmap_free(r1);  
  roaring_bitmap_free(r2);  
  sqlite3_result_int(context, nOut);
  roaring_bitmap_and_inplace(r1, r2);
}

/*********************************************
  rb_xor(bitmap1, bitmap2)
  --------------------------------------------
  and the second bitmap with the first one (first one is modified) and returns the first bitmap
*********************************************/
static void roaringXorFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *pIn1;
  unsigned int nIn1;  
  const unsigned char *pIn2;
  unsigned int nIn2;  
  pIn1 = sqlite3_value_blob(argv[0]);
  nIn1 = sqlite3_value_bytes(argv[0]);
  pIn2 = sqlite3_value_blob(argv[1]);
  nIn2 = sqlite3_value_bytes(argv[1]);
  roaring_bitmap_t *r1 = roaring_bitmap_deserialize_safe(pIn1, nIn1);
  roaring_bitmap_t *r2 = roaring_bitmap_deserialize_safe(pIn2, nIn2);
  if( r1 == NULL || r2 == NULL){
    sqlite3_result_error(context, "invalid bitmap(s)", -1);
    return;
  }
  roaring_bitmap_xor_inplace(r1, r2);
  int nOut, nSize;
  nSize = (int) roaring_bitmap_size_in_bytes(r1);
  char *pOut = sqlite3_malloc(nSize);
  nOut = (int) roaring_bitmap_serialize(r1, pOut);
  roaring_bitmap_free(r1);  
  roaring_bitmap_free(r2);  
  sqlite3_result_blob(context, pOut, nOut, sqlite3_free);  
}


/*********************************************
  rb_xor_length(bitmap1, bitmap2)
  --------------------------------------------
  and the second bitmap with the first one (first one is modified) and returns the first bitmap
*********************************************/
static void roaringXorLengthFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *pIn1;
  unsigned int nIn1;  
  const unsigned char *pIn2;
  unsigned int nIn2;  
  pIn1 = sqlite3_value_blob(argv[0]);
  nIn1 = sqlite3_value_bytes(argv[0]);
  pIn2 = sqlite3_value_blob(argv[1]);
  nIn2 = sqlite3_value_bytes(argv[1]);
  roaring_bitmap_t *r1 = roaring_bitmap_deserialize_safe(pIn1, nIn1);
  roaring_bitmap_t *r2 = roaring_bitmap_deserialize_safe(pIn2, nIn2);
  if( r1 == NULL || r2 == NULL){
    sqlite3_result_error(context, "invalid bitmap(s)", -1);
    return;
  }
  int nOut = (int) roaring_bitmap_xor_cardinality(r1, r2);
  roaring_bitmap_free(r1);  
  roaring_bitmap_free(r2);  
  sqlite3_result_int(context, nOut);
}

/*********************************************
  rb_or_length(bitmap1, bitmap2)
  --------------------------------------------
  or the second bitmap with the first one (first one is modified) and returns the length of the result
*********************************************/
static void roaringOrLengthFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *pIn1;
  unsigned int nIn1;  
  const unsigned char *pIn2;
  unsigned int nIn2;  
  pIn1 = sqlite3_value_blob(argv[0]);
  nIn1 = sqlite3_value_bytes(argv[0]);
  pIn2 = sqlite3_value_blob(argv[1]);
  nIn2 = sqlite3_value_bytes(argv[1]);
  roaring_bitmap_t *r1 = roaring_bitmap_deserialize_safe(pIn1, nIn1);
  roaring_bitmap_t *r2 = roaring_bitmap_deserialize_safe(pIn2, nIn2);
  if( r1 == NULL || r2 == NULL){
    sqlite3_result_error(context, "invalid bitmap(s)", -1);
    return;
  }
  int nOut = (int) roaring_bitmap_or_cardinality(r1, r2);
  roaring_bitmap_free(r1);  
  roaring_bitmap_free(r2);  
  sqlite3_result_int(context, nOut);
}

/*********************************************
  rb_or(bitmap1, bitmap2)
  --------------------------------------------
  or the second bitmap with the first one (first one is modified) and returns the first bitmap
*********************************************/
static void roaringOrFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *pIn1;
  unsigned int nIn1;  
  const unsigned char *pIn2;
  unsigned int nIn2;  
  pIn1 = sqlite3_value_blob(argv[0]);
  nIn1 = sqlite3_value_bytes(argv[0]);
  pIn2 = sqlite3_value_blob(argv[1]);
  nIn2 = sqlite3_value_bytes(argv[1]);
  roaring_bitmap_t *r1 = roaring_bitmap_deserialize_safe(pIn1, nIn1);
  roaring_bitmap_t *r2 = roaring_bitmap_deserialize_safe(pIn2, nIn2);
  if( r1 == NULL || r2 == NULL){
    sqlite3_result_error(context, "invalid bitmap(s)", -1);
    return;
  }
  roaring_bitmap_or_inplace(r1, r2);
  int nOut, nSize;
  nSize = (int) roaring_bitmap_size_in_bytes(r1);
  char *pOut = sqlite3_malloc(nSize);
  nOut = (int) roaring_bitmap_serialize(r1, pOut);
  roaring_bitmap_free(r1);  
  roaring_bitmap_free(r2);  
  sqlite3_result_blob(context, pOut, nOut, sqlite3_free);  
}


/*********************************************
  rb_group_and(col)
  --------------------------------------------
  and all the values in col
  
  example: SELECT rb_group_and(col) FROM table
*********************************************/

static void roaringAndAllStep(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){

  const unsigned char *pIn;
  unsigned int nIn;  
  //const unsigned char *pInAnd;
  //unsigned int nInAnd;
  RoaringContext *rc;
  rc = (RoaringContext*)sqlite3_aggregate_context(context, sizeof(*rc));
  pIn = sqlite3_value_blob(argv[0]);
  nIn = sqlite3_value_bytes(argv[0]);

  if(rc->init == 0){
    rc->init = 1;
    rc->rb = roaring_bitmap_deserialize_safe(pIn, nIn);
    if( rc->rb == NULL ){
      sqlite3_result_error(context, "invalid bitmap", -1);
      return;
    }
  }else{
    roaring_bitmap_t *r = roaring_bitmap_deserialize_safe(pIn, nIn);
    if( r == NULL ){
      sqlite3_result_error(context, "invalid bitmap", -1);
      return;
    }
    roaring_bitmap_and_inplace(rc->rb, r);
    roaring_bitmap_free(r);
  }

}


static void roaringAndAllFinal(sqlite3_context *context){
  RoaringContext *rc;
  int nOut, nSize;
  rc = (RoaringContext*)sqlite3_aggregate_context(context, sizeof(*rc));
  if(rc->rb == NULL){
    // no rb was created, must be an empty result set
    rc->rb = roaring_bitmap_create();    
  }
  nSize = (int) roaring_bitmap_size_in_bytes(rc->rb);
  char *pOut = sqlite3_malloc(nSize);
  nOut = (int) roaring_bitmap_serialize(rc->rb, pOut);
  roaring_bitmap_free(rc->rb); 
  memset(rc, 0, sizeof(*rc)); 
  sqlite3_result_blob(context, pOut, nOut, sqlite3_free);  
}

/*********************************************
  rb_group_or(col)
  --------------------------------------------
  or all the values in col
  
  example: SELECT rb_group_or(col) FROM table
*********************************************/
static void roaringOrAllStep(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){

  const unsigned char *pIn;
  unsigned int nIn;  
  //const unsigned char *pInAnd;
  //unsigned int nInAnd;
  RoaringContext *rc;
  rc = (RoaringContext*)sqlite3_aggregate_context(context, sizeof(*rc));
  if(rc->init == 0){
    rc->init = 1;
    rc->rb = roaring_bitmap_create();
  }
  pIn = sqlite3_value_blob(argv[0]);
  nIn = sqlite3_value_bytes(argv[0]);
  roaring_bitmap_t *r = roaring_bitmap_deserialize_safe(pIn, nIn);
  if( r == NULL ){
    sqlite3_result_error(context, "invalid bitmap", -1);
    return;
  }
  roaring_bitmap_or_inplace(rc->rb, r);
  roaring_bitmap_free(r);
}

static void roaringOrAllFinal(sqlite3_context *context){
  RoaringContext *rc;
  int nOut, nSize;
  rc = (RoaringContext*)sqlite3_aggregate_context(context, sizeof(*rc));
  if(rc->rb == NULL){
    // no rb was created, must be an empty result set
    rc->rb = roaring_bitmap_create();    
  }
  nSize = (int) roaring_bitmap_size_in_bytes(rc->rb);
  char *pOut = sqlite3_malloc(nSize);
  nOut = (int) roaring_bitmap_serialize(rc->rb, pOut);
  roaring_bitmap_free(rc->rb); 
  memset(rc, 0, sizeof(*rc)); 
  sqlite3_result_blob(context, pOut, nOut, sqlite3_free);  
}


typedef struct ArrayContext ArrayContext;
struct ArrayContext {
  unsigned init;
  unsigned count;
  int64_t *array;
};



/*********************************************
  rb_array(bitmap, limit=null, offset=0)
  --------------------------------------------
  returns the first limit elements starting from offset
  if the limit is not provided or equal to -1 then all the entries are returned
  if the offset is not set then elements will be returned starting from offset 0
*********************************************/
static void roaringArrayFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *pIn;
  unsigned int nIn;  
  pIn = sqlite3_value_blob(argv[0]);
  nIn = sqlite3_value_bytes(argv[0]);
  roaring_bitmap_t *r = roaring_bitmap_deserialize_safe(pIn, nIn);
  if( r == NULL ){
    sqlite3_result_error(context, "invalid bitmap", -1);
    return;
  }
  int nSize = roaring_bitmap_get_cardinality(r);
  uint32_t *ids;
  ids = sqlite3_malloc(nSize * sizeof(uint32_t));
  roaring_bitmap_to_uint32_array(r, ids);
  roaring_bitmap_free(r); 
  sqlite3_result_pointer(context, ids, "carray", (void*)roaringArrayFreeFunc);
}


/*********************************************
  rb_count(bitmap)
  --------------------------------------------
  returns the cardinality of the bitmap
*********************************************/
static void roaringLengthFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *pIn;
  unsigned int nIn;  
  pIn = sqlite3_value_blob(argv[0]);
  nIn = sqlite3_value_bytes(argv[0]);
  roaring_bitmap_t *r = roaring_bitmap_deserialize_safe(pIn, nIn);
  if( r == NULL ){
    sqlite3_result_error(context, "invalid bitmap", -1);
    return;
  }
  int nSize = roaring_bitmap_get_cardinality(r);
  roaring_bitmap_free(r); 
  sqlite3_result_int(context, nSize);
}

#ifdef _WIN32
__declspec(dllexport)
#endif

int sqlite3_roaring_init(
  sqlite3 *db, 
  char **pzErrMsg, 
  const sqlite3_api_routines *pApi
){
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);
  int flags = SQLITE_UTF8 | SQLITE_INNOCUOUS | SQLITE_DETERMINISTIC;
  // Scalar SQL functions
  rc = sqlite3_create_function(db, "rb_create", -1, flags, 0, roaringCreateFunc, 0, 0);
  rc = sqlite3_create_function(db, "rb_count", 1, flags, 0, roaringLengthFunc, 0, 0);
  rc = sqlite3_create_function(db, "rb_add", 2, flags, 0, roaringAddFunc, 0, 0);
  rc = sqlite3_create_function(db, "rb_remove", 2, flags, 0, roaringRemoveFunc, 0, 0);
  rc = sqlite3_create_function(db, "rb_and", 2, flags, 0, roaringAndFunc, 0, 0);
  rc = sqlite3_create_function(db, "rb_or", 2, flags, 0, roaringOrFunc, 0, 0);
  rc = sqlite3_create_function(db, "rb_not", 2, flags, 0, roaringNotFunc, 0, 0);
  rc = sqlite3_create_function(db, "rb_xor", 2, flags, 0, roaringXorFunc, 0, 0);
  rc = sqlite3_create_function(db, "rb_and_count", 2, flags, 0, roaringAndLengthFunc, 0, 0);
  rc = sqlite3_create_function(db, "rb_or_count", 2, flags, 0, roaringOrLengthFunc, 0, 0);
  rc = sqlite3_create_function(db, "rb_not_count", 2, flags, 0, roaringNotLengthFunc, 0, 0);
  rc = sqlite3_create_function(db, "rb_xor_count", 2, flags, 0, roaringXorLengthFunc, 0, 0);
  //rc = sqlite3_create_function(db, "rb_and_many", -1, flags, 0, roaringAndManyFunc, 0, 0);
  //rc = sqlite3_create_function(db, "rb_or_many", -1, flags, 0, roaringOrManyFunc, 0, 0);
  // aggregate SQL functions
  rc = sqlite3_create_function(db, "rb_group_create", 1, flags, 0, 0, roaringCreateStep, roaringCreateFinal);
  rc = sqlite3_create_function(db, "rb_group_and", 1, flags, 0, 0, roaringAndAllStep, roaringAndAllFinal);
  rc = sqlite3_create_function(db, "rb_group_or", 1, flags, 0, 0, roaringOrAllStep, roaringOrAllFinal);
  // carray based SQL functions (for conversion to a virtual table) 
  rc = sqlite3_create_function(db, "rb_array", 1, flags, 0, roaringArrayFunc, 0, 0);
  return rc;
}
