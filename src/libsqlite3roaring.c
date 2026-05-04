#include <stddef.h>
#include <sqlite3ext.h>
#include "roaring.c"
SQLITE_EXTENSION_INIT1

/* Insert your extension code here */

static void roaringFreeFunc(roaring_bitmap_t *p){
  roaring_bitmap_free(p);
}

static void roaring64FreeFunc(roaring64_bitmap_t *p){
  roaring64_bitmap_free(p);  
}

static void roaringArrayFreeFunc(int *p){
  sqlite3_free(p);
}

static void roaring64ArrayFreeFunc(int64_t *p){
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

static void roaring64CreateFunc(  
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  roaring64_bitmap_t *r = roaring64_bitmap_create();
  for(int i=0; i < argc; i++){
    if( sqlite3_value_type(argv[i])!=SQLITE_INTEGER ){
      sqlite3_result_error(context, "invalid argument", -1);
      return;
    }
    roaring64_bitmap_add(r, sqlite3_value_int64(argv[i]));
  }
  int nSize = (int )roaring64_bitmap_portable_size_in_bytes(r);
  char *pOut = sqlite3_malloc(nSize);
  int nOut = (int) roaring64_bitmap_portable_serialize(r, pOut);
  roaring64_bitmap_free(r);  
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

typedef struct Roaring64Context Roaring64Context;
struct Roaring64Context {
  unsigned init;
  roaring64_bitmap_t *rb;
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
  roaring_bitmap_add(rc->rb, sqlite3_value_int(argv[0]));
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

static void roaring64CreateStep(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){

  Roaring64Context *rc;

  // bail out if the value supplied is not an integer
  if( sqlite3_value_type(argv[0])!=SQLITE_INTEGER ){
    sqlite3_result_error(context, "invalid argument", -1);
    return;
  }

  // check if the context already has the roaring bitmap  
  rc = (Roaring64Context*)sqlite3_aggregate_context(context, sizeof(*rc));
  if(rc->init == 0){
    // create a roaring bitmap
    rc->rb = roaring64_bitmap_create();
    if(rc->rb == NULL){
      memset(rc, 0, sizeof(*rc)); 
      sqlite3_result_error(context, "failed to create bitmap in step", -1);
      return;
    }
    rc->init = 1;
  }
  roaring64_bitmap_add(rc->rb, sqlite3_value_int64(argv[0]));
}

static void roaring64CreateFinal(sqlite3_context *context){
  Roaring64Context *rc;
  int nOut, nSize;
  
  rc = (Roaring64Context*)sqlite3_aggregate_context(context, sizeof(*rc));
  if(rc->rb == NULL){
    // no rb was created, must be an empty result set
    rc->rb = roaring64_bitmap_create();    
  }
  nSize = (int )roaring64_bitmap_portable_size_in_bytes(rc->rb);
  char *pOut = sqlite3_malloc(nSize);
  nOut = (int) roaring64_bitmap_portable_serialize(rc->rb, pOut);
  roaring64_bitmap_free(rc->rb); 
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

static void roaring64AddFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *pIn;
  unsigned int nIn;  
  pIn = sqlite3_value_blob(argv[0]);
  nIn = sqlite3_value_bytes(argv[0]);
  roaring64_bitmap_t *r = roaring64_bitmap_portable_deserialize_safe(pIn, nIn);
  if( r == NULL ){
    sqlite3_result_error(context, "invalid bitmap", -1);
    return;
  }
  if( sqlite3_value_type(argv[1])!=SQLITE_INTEGER ){
    sqlite3_result_error(context, "invalid argument", -1);
    return;
  }
  roaring64_bitmap_add(r, sqlite3_value_int64(argv[1]));
  int nSize = (int) roaring64_bitmap_portable_size_in_bytes(r);
  char *pOut = sqlite3_malloc(nSize);
  int nOut = (int) roaring64_bitmap_portable_serialize(r, pOut);
  roaring64_bitmap_free(r);  
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

static void roaring64RemoveFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *pIn;
  unsigned int nIn;  
  pIn = sqlite3_value_blob(argv[0]);
  nIn = sqlite3_value_bytes(argv[0]);
  roaring64_bitmap_t *r = roaring64_bitmap_portable_deserialize_safe(pIn, nIn);
  if( r == NULL ){
    sqlite3_result_error(context, "invalid bitmap", -1);
    return;
  }
  if( sqlite3_value_type(argv[1])!=SQLITE_INTEGER ){
    sqlite3_result_error(context, "invalid argument", -1);
    return;
  }
  roaring64_bitmap_remove(r, sqlite3_value_int64(argv[1]));
  int nSize = (int) roaring64_bitmap_portable_size_in_bytes(r);
  char *pOut = sqlite3_malloc(nSize);
  int nOut = (int) roaring64_bitmap_portable_serialize(r, pOut);
  roaring64_bitmap_free(r);  
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

static void roaring64AndLengthFunc(
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
  roaring64_bitmap_t *r1 = roaring64_bitmap_portable_deserialize_safe(pIn1, nIn1);
  roaring64_bitmap_t *r2 = roaring64_bitmap_portable_deserialize_safe(pIn2, nIn2);
  if( r1 == NULL || r2 == NULL){
    sqlite3_result_error(context, "invalid bitmap(s)", -1);
    return;
  }
  int nOut = (int) roaring64_bitmap_and_cardinality(r1, r2);
  roaring64_bitmap_free(r1);  
  roaring64_bitmap_free(r2);  
  sqlite3_result_int64(context, nOut);
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

static void roaring64AndManyFunc(  
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *pIn;
  unsigned int nIn;  
  roaring64_bitmap_t *rfinal;
  roaring64_bitmap_t *r;
  for(int i=0; i < argc; i++){
    if(sqlite3_value_type(argv[i])!=SQLITE_BLOB){
      continue;
    }
    r = roaring64_bitmap_portable_deserialize_safe(sqlite3_value_blob(argv[i]), sqlite3_value_bytes(argv[i]));     
    if( r == NULL){
      sqlite3_result_error(context, "invalid bitmap(s)", -1);
      return;
    }
    if(rfinal == NULL){
      rfinal = roaring64_bitmap_copy(r);
    } else {
      roaring64_bitmap_and_inplace(rfinal, r);
      roaring64_bitmap_free(r);
    }
  }
  if(rfinal == NULL){
    rfinal = roaring64_bitmap_create();
  }
  int nSize = (int) roaring64_bitmap_portable_size_in_bytes(rfinal);
  char *pOut = sqlite3_malloc(nSize);
  int nOut = (int) roaring64_bitmap_portable_serialize(rfinal, pOut);
  roaring64_bitmap_free(rfinal);  
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

static void roaring64OrManyFunc(  
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *pIn;
  unsigned int nIn;  
  roaring64_bitmap_t *rfinal;
  roaring64_bitmap_t *r;
  rfinal = roaring64_bitmap_create();
  for(int i=0; i < argc; i++){
    if(sqlite3_value_type(argv[i])!=SQLITE_BLOB){
      continue;
    }
    r = roaring64_bitmap_portable_deserialize_safe(sqlite3_value_blob(argv[i]), sqlite3_value_bytes(argv[i]));     
    if( r == NULL){
      sqlite3_result_error(context, "invalid bitmap(s)", -1);
      return;
    }
    roaring64_bitmap_or_inplace(rfinal, r);
    roaring64_bitmap_free(r);
  }
  int nSize = (int )roaring64_bitmap_portable_size_in_bytes(rfinal);
  char *pOut = sqlite3_malloc(nSize);
  int nOut = (int) roaring64_bitmap_portable_serialize(rfinal, pOut);
  roaring64_bitmap_free(rfinal);  
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

static void roaring64AndFunc(
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
  roaring64_bitmap_t *r1 = roaring64_bitmap_portable_deserialize_safe(pIn1, nIn1);
  roaring64_bitmap_t *r2 = roaring64_bitmap_portable_deserialize_safe(pIn2, nIn2);
  if( r1 == NULL || r2 == NULL){
    sqlite3_result_error(context, "invalid bitmap(s)", -1);
    return;
  }
  roaring64_bitmap_and_inplace(r1, r2);
  int nOut, nSize;
  nSize = (int) roaring64_bitmap_portable_size_in_bytes(r1);
  char *pOut = sqlite3_malloc(nSize);
  nOut = (int) roaring64_bitmap_portable_serialize(r1, pOut);
  roaring64_bitmap_free(r1);  
  roaring64_bitmap_free(r2);  
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

static void roaring64NotFunc(
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
  roaring64_bitmap_t *r1 = roaring64_bitmap_portable_deserialize_safe(pIn1, nIn1);
  roaring64_bitmap_t *r2 = roaring64_bitmap_portable_deserialize_safe(pIn2, nIn2);
  if( r1 == NULL || r2 == NULL){
    sqlite3_result_error(context, "invalid bitmap(s)", -1);
    return;
  }
  roaring64_bitmap_andnot_inplace(r1, r2);
  int64_t nOut, nSize;
  nSize = (int64_t) roaring64_bitmap_portable_size_in_bytes(r1);
  char *pOut = sqlite3_malloc(nSize);
  nOut = (int64_t) roaring64_bitmap_portable_serialize(r1, pOut);
  roaring64_bitmap_free(r1);  
  roaring64_bitmap_free(r2);  
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
  //roaring_bitmap_and_inplace(r1, r2);
}

static void roaring64NotLengthFunc(
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
  roaring64_bitmap_t *r1 = roaring64_bitmap_portable_deserialize_safe(pIn1, nIn1);
  roaring64_bitmap_t *r2 = roaring64_bitmap_portable_deserialize_safe(pIn2, nIn2);
  if( r1 == NULL || r2 == NULL){
    sqlite3_result_error(context, "invalid bitmap(s)", -1);
    return;
  }
  int64_t nOut = (int64_t) roaring64_bitmap_andnot_cardinality(r1, r2);
  roaring64_bitmap_free(r1);  
  roaring64_bitmap_free(r2);  
  sqlite3_result_int64(context, nOut);
  //roaring_bitmap_and_inplace(r1, r2);
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

static void roaring64XorFunc(
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
  roaring64_bitmap_t *r1 = roaring64_bitmap_portable_deserialize_safe(pIn1, nIn1);
  roaring64_bitmap_t *r2 = roaring64_bitmap_portable_deserialize_safe(pIn2, nIn2);
  if( r1 == NULL || r2 == NULL){
    sqlite3_result_error(context, "invalid bitmap(s)", -1);
    return;
  }
  roaring64_bitmap_xor_inplace(r1, r2);
  int64_t nOut, nSize;
  nSize = (int64_t) roaring64_bitmap_portable_size_in_bytes(r1);
  char *pOut = sqlite3_malloc(nSize);
  nOut = (int64_t) roaring64_bitmap_portable_serialize(r1, pOut);
  roaring64_bitmap_free(r1);  
  roaring64_bitmap_free(r2);  
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

static void roaring64XorLengthFunc(
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
  roaring64_bitmap_t *r1 = roaring64_bitmap_portable_deserialize_safe(pIn1, nIn1);
  roaring64_bitmap_t *r2 = roaring64_bitmap_portable_deserialize_safe(pIn2, nIn2);
  if( r1 == NULL || r2 == NULL){
    sqlite3_result_error(context, "invalid bitmap(s)", -1);
    return;
  }
  int nOut = (int) roaring64_bitmap_xor_cardinality(r1, r2);
  roaring64_bitmap_free(r1);  
  roaring64_bitmap_free(r2);  
  sqlite3_result_int64(context, nOut);
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

static void roaring64OrLengthFunc(
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
  roaring64_bitmap_t *r1 = roaring64_bitmap_portable_deserialize_safe(pIn1, nIn1);
  roaring64_bitmap_t *r2 = roaring64_bitmap_portable_deserialize_safe(pIn2, nIn2);
  if( r1 == NULL || r2 == NULL){
    sqlite3_result_error(context, "invalid bitmap(s)", -1);
    return;
  }
  int nOut = (int) roaring64_bitmap_or_cardinality(r1, r2);
  roaring64_bitmap_free(r1);  
  roaring64_bitmap_free(r2);  
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

static void roaring64OrFunc(
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
  roaring64_bitmap_t *r1 = roaring64_bitmap_portable_deserialize_safe(pIn1, nIn1);
  roaring64_bitmap_t *r2 = roaring64_bitmap_portable_deserialize_safe(pIn2, nIn2);
  if( r1 == NULL || r2 == NULL){
    sqlite3_result_error(context, "invalid bitmap(s)", -1);
    return;
  }
  roaring64_bitmap_or_inplace(r1, r2);
  int nOut, nSize;
  nSize = (int) roaring64_bitmap_portable_size_in_bytes(r1);
  char *pOut = sqlite3_malloc(nSize);
  nOut = (int) roaring64_bitmap_portable_serialize(r1, pOut);
  roaring64_bitmap_free(r1);  
  roaring64_bitmap_free(r2);  
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

static void roaring64AndAllStep(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){

  const unsigned char *pIn;
  unsigned int nIn;  
  //const unsigned char *pInAnd;
  //unsigned int nInAnd;
  Roaring64Context *rc;
  rc = (Roaring64Context*)sqlite3_aggregate_context(context, sizeof(*rc));
  pIn = sqlite3_value_blob(argv[0]);
  nIn = sqlite3_value_bytes(argv[0]);

  if(rc->init == 0){
    rc->init = 1;
    rc->rb = roaring64_bitmap_portable_deserialize_safe(pIn, nIn);
    if( rc->rb == NULL ){
      sqlite3_result_error(context, "invalid bitmap", -1);
      return;
    }
  }else{
    roaring64_bitmap_t *r = roaring64_bitmap_portable_deserialize_safe(pIn, nIn);
    if( r == NULL ){
      sqlite3_result_error(context, "invalid bitmap", -1);
      return;
    }
    roaring64_bitmap_and_inplace(rc->rb, r);
    roaring64_bitmap_free(r);
  }

}

static void roaring64AndAllFinal(sqlite3_context *context){
  Roaring64Context *rc;
  int64_t nOut, nSize;
  rc = (Roaring64Context*)sqlite3_aggregate_context(context, sizeof(*rc));
  if(rc->rb == NULL){
    // no rb was created, must be an empty result set
    rc->rb = roaring64_bitmap_create();    
  }
  nSize = (int64_t) roaring64_bitmap_portable_size_in_bytes(rc->rb);
  char *pOut = sqlite3_malloc(nSize);
  nOut = (int64_t) roaring64_bitmap_portable_serialize(rc->rb, pOut);
  roaring64_bitmap_free(rc->rb); 
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

static void roaring64OrAllStep(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){

  const unsigned char *pIn;
  unsigned int nIn;  
  //const unsigned char *pInAnd;
  //unsigned int nInAnd;
  Roaring64Context *rc;
  rc = (Roaring64Context*)sqlite3_aggregate_context(context, sizeof(*rc));
  if(rc->init == 0){
    rc->init = 1;
    rc->rb = roaring64_bitmap_create();
  }
  pIn = sqlite3_value_blob(argv[0]);
  nIn = sqlite3_value_bytes(argv[0]);
  roaring64_bitmap_t *r = roaring64_bitmap_portable_deserialize_safe(pIn, nIn);
  if( r == NULL ){
    sqlite3_result_error(context, "invalid bitmap", -1);
    return;
  }
  roaring64_bitmap_or_inplace(rc->rb, r);
  roaring64_bitmap_free(r);
}

static void roaring64OrAllFinal(sqlite3_context *context){
  Roaring64Context *rc;
  int64_t nOut, nSize;
  rc = (Roaring64Context*)sqlite3_aggregate_context(context, sizeof(*rc));
  if(rc->rb == NULL){
    // no rb was created, must be an empty result set
    rc->rb = roaring64_bitmap_create();    
  }
  nSize = (int64_t) roaring64_bitmap_portable_size_in_bytes(rc->rb);
  char *pOut = sqlite3_malloc(nSize);
  nOut = (int64_t) roaring64_bitmap_portable_serialize(rc->rb, pOut);
  roaring64_bitmap_free(rc->rb); 
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


static void roaring64ArrayFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *pIn;
  unsigned int nIn;  
  pIn = sqlite3_value_blob(argv[0]);
  nIn = sqlite3_value_bytes(argv[0]);
  roaring64_bitmap_t *r = roaring64_bitmap_portable_deserialize_safe(pIn, nIn);
  if( r == NULL ){
    sqlite3_result_error(context, "invalid bitmap", -1);
    return;
  }
  int nSize = roaring64_bitmap_get_cardinality(r);
  uint64_t *ids;
  ids = sqlite3_malloc(nSize * sizeof(uint64_t));
  roaring64_bitmap_to_uint64_array(r, ids);
  roaring64_bitmap_free(r); 
  sqlite3_result_pointer(context, ids, "carray", (void*)roaring64ArrayFreeFunc);
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

static void roaring64LengthFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *pIn;
  unsigned int nIn;  
  pIn = sqlite3_value_blob(argv[0]);
  nIn = sqlite3_value_bytes(argv[0]);
  roaring64_bitmap_t *r = roaring64_bitmap_portable_deserialize_safe(pIn, nIn);
  if( r == NULL ){
    sqlite3_result_error(context, "invalid bitmap", -1);
    return;
  }
  int64_t nSize = roaring64_bitmap_get_cardinality(r);
  roaring64_bitmap_free(r); 
  sqlite3_result_int64(context, nSize);
}

#ifdef _WIN32
__declspec(dllexport)
#endif

/* ── rb_each(bitmap BLOB) ────────────────────────────────────────────────────
**
** Table-valued function returning one row per integer in a roaring bitmap.
** Uses a fixed 4K-value staging buffer filled via roaring_uint32_iterator_read
** — O(1) peak memory with bulk-extraction throughput.  The buffer size targets
** L1 cache (4K uint32 = 16 KB; L1D is typically 32-64 KB) so the buffer
** stays hot between the roaring fill and the SQLite xColumn drain.
**
** Usage:  SELECT value FROM rb_each(bitmap_blob)
*/

#ifndef SQLITE_OMIT_VIRTUALTABLE

#define RB_EACH_CHUNK (4u * 1024u)

typedef struct RbEachVtab   RbEachVtab;
typedef struct RbEachCursor RbEachCursor;

struct RbEachVtab {
  sqlite3_vtab base;
};

struct RbEachCursor {
  sqlite3_vtab_cursor       base;
  roaring_uint32_iterator_t it;
  roaring_bitmap_t         *bm;
  uint32_t                 *buf;
  uint32_t                  cap;
  uint32_t                  nBuf;
  uint32_t                  iBuf;
};

static int rbEachConnect(
  sqlite3 *db,
  void *pUnused,
  int argcUnused, const char *const *argvUnused,
  sqlite3_vtab **ppVtab,
  char **pzErrUnused
){
  int rc;
  RbEachVtab *pNew;
  (void)pUnused; (void)argcUnused; (void)argvUnused; (void)pzErrUnused;
  rc = sqlite3_declare_vtab(db, "CREATE TABLE x(value INTEGER, bitmap BLOB HIDDEN)");
  if( rc==SQLITE_OK ){
    pNew = sqlite3_malloc(sizeof(*pNew));
    if( !pNew ) return SQLITE_NOMEM;
    memset(pNew, 0, sizeof(*pNew));
    sqlite3_vtab_config(db, SQLITE_VTAB_INNOCUOUS);
    *ppVtab = &pNew->base;
  }
  return rc;
}

static int rbEachDisconnect(sqlite3_vtab *pVtab){
  sqlite3_free(pVtab);
  return SQLITE_OK;
}

static int rbEachOpen(sqlite3_vtab *pUnused, sqlite3_vtab_cursor **ppCursor){
  RbEachCursor *pCur;
  (void)pUnused;
  pCur = sqlite3_malloc(sizeof(*pCur));
  if( !pCur ) return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  *ppCursor = &pCur->base;
  return SQLITE_OK;
}

static int rbEachClose(sqlite3_vtab_cursor *cur){
  RbEachCursor *pCur = (RbEachCursor *)cur;
  if( pCur->bm  ) roaring_bitmap_free(pCur->bm);
  if( pCur->buf ) sqlite3_free(pCur->buf);
  sqlite3_free(pCur);
  return SQLITE_OK;
}

static int rbEachBestIndex(sqlite3_vtab *pUnused, sqlite3_index_info *pIdx){
  int i;
  (void)pUnused;
  for( i=0; i<pIdx->nConstraint; i++ ){
    if( pIdx->aConstraint[i].iColumn != 1 ) continue;
    if( pIdx->aConstraint[i].op != SQLITE_INDEX_CONSTRAINT_EQ ) continue;
    if( !pIdx->aConstraint[i].usable ) continue;
    pIdx->aConstraintUsage[i].argvIndex = 1;
    pIdx->aConstraintUsage[i].omit      = 1;
    pIdx->idxNum        = 1;
    pIdx->estimatedRows = 1000;
    pIdx->estimatedCost = 1.0;
    return SQLITE_OK;
  }
  pIdx->estimatedRows = 0;
  pIdx->estimatedCost = 1.0;
  return SQLITE_OK;
}

static int rbEachFilter(
  sqlite3_vtab_cursor *cur,
  int idxNum, const char *idxStr,
  int argc, sqlite3_value **argv
){
  RbEachCursor *pCur = (RbEachCursor *)cur;
  uint64_t card;
  (void)idxStr;
  if( pCur->bm  ){ roaring_bitmap_free(pCur->bm); pCur->bm  = NULL; }
  if( pCur->buf ){ sqlite3_free(pCur->buf);        pCur->buf = NULL; }
  pCur->nBuf = pCur->iBuf = pCur->cap = 0;

  if( argc<1 || sqlite3_value_type(argv[0])==SQLITE_NULL ) return SQLITE_OK;

  pCur->bm = roaring_bitmap_deserialize_safe(
      sqlite3_value_blob(argv[0]),
      (size_t)sqlite3_value_bytes(argv[0]));
  if( !pCur->bm ){
    pCur->base.pVtab->zErrMsg = sqlite3_mprintf("rb_each: invalid bitmap");
    return SQLITE_ERROR;
  }

  card = roaring_bitmap_get_cardinality(pCur->bm);
  if( card==0 ) return SQLITE_OK;

  pCur->cap = (uint32_t)(card < RB_EACH_CHUNK ? card : RB_EACH_CHUNK);
  pCur->buf = (uint32_t *)sqlite3_malloc64(pCur->cap * sizeof(uint32_t));
  if( !pCur->buf ){ roaring_bitmap_free(pCur->bm); pCur->bm=NULL; return SQLITE_NOMEM; }

  roaring_iterator_init(pCur->bm, &pCur->it);
  pCur->nBuf = roaring_uint32_iterator_read(&pCur->it, pCur->buf, pCur->cap);
  return SQLITE_OK;
}

static int rbEachNext(sqlite3_vtab_cursor *cur){
  RbEachCursor *pCur = (RbEachCursor *)cur;
  if( ++pCur->iBuf >= pCur->nBuf ){
    pCur->nBuf = roaring_uint32_iterator_read(&pCur->it, pCur->buf, pCur->cap);
    pCur->iBuf = 0;
  }
  return SQLITE_OK;
}

static int rbEachEof(sqlite3_vtab_cursor *cur){
  return ((RbEachCursor *)cur)->nBuf == 0;
}

static int rbEachColumn(sqlite3_vtab_cursor *cur, sqlite3_context *ctx, int col){
  RbEachCursor *pCur = (RbEachCursor *)cur;
  if( col==0 ) sqlite3_result_int64(ctx, (sqlite3_int64)pCur->buf[pCur->iBuf]);
  return SQLITE_OK;
}

static int rbEachRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid){
  RbEachCursor *pCur = (RbEachCursor *)cur;
  *pRowid = (sqlite_int64)pCur->buf[pCur->iBuf];
  return SQLITE_OK;
}

static sqlite3_module rbEachModule = {
  0,                /* iVersion */
  rbEachConnect,    /* xCreate (== xConnect → eponymous) */
  rbEachConnect,    /* xConnect */
  rbEachBestIndex,  /* xBestIndex */
  rbEachDisconnect, /* xDisconnect */
  rbEachDisconnect, /* xDestroy */
  rbEachOpen,       /* xOpen */
  rbEachClose,      /* xClose */
  rbEachFilter,     /* xFilter */
  rbEachNext,       /* xNext */
  rbEachEof,        /* xEof */
  rbEachColumn,     /* xColumn */
  rbEachRowid,      /* xRowid */
  0,                /* xUpdate */
  0, 0, 0, 0,       /* xBegin..xRollback */
  0,                /* xFindMethod */
  0,                /* xRename */
  0, 0, 0,          /* xSavepoint..xShadowName */
};

#endif /* SQLITE_OMIT_VIRTUALTABLE */

/* ── rb64_each(bitmap BLOB) ──────────────────────────────────────────────────
**
** Table-valued function returning one row per integer in a roaring64 bitmap.
** Mirrors rb_each but operates on 64-bit bitmaps using roaring64_iterator_t
** (heap-allocated) and a uint64_t staging buffer.
**
** Usage:  SELECT value FROM rb64_each(bitmap_blob)
*/

#ifndef SQLITE_OMIT_VIRTUALTABLE

#define RB64_EACH_CHUNK (4u * 1024u)

typedef struct Rb64EachVtab   Rb64EachVtab;
typedef struct Rb64EachCursor Rb64EachCursor;

struct Rb64EachVtab {
  sqlite3_vtab base;
};

struct Rb64EachCursor {
  sqlite3_vtab_cursor  base;
  roaring64_iterator_t *it;
  roaring64_bitmap_t   *bm;
  uint64_t             *buf;
  uint32_t              cap;
  uint64_t              nBuf;
  uint64_t              iBuf;
};

static int rb64EachConnect(
  sqlite3 *db,
  void *pUnused,
  int argcUnused, const char *const *argvUnused,
  sqlite3_vtab **ppVtab,
  char **pzErrUnused
){
  int rc;
  Rb64EachVtab *pNew;
  (void)pUnused; (void)argcUnused; (void)argvUnused; (void)pzErrUnused;
  rc = sqlite3_declare_vtab(db, "CREATE TABLE x(value INTEGER, bitmap BLOB HIDDEN)");
  if( rc==SQLITE_OK ){
    pNew = sqlite3_malloc(sizeof(*pNew));
    if( !pNew ) return SQLITE_NOMEM;
    memset(pNew, 0, sizeof(*pNew));
    sqlite3_vtab_config(db, SQLITE_VTAB_INNOCUOUS);
    *ppVtab = &pNew->base;
  }
  return rc;
}

static int rb64EachDisconnect(sqlite3_vtab *pVtab){
  sqlite3_free(pVtab);
  return SQLITE_OK;
}

static int rb64EachOpen(sqlite3_vtab *pUnused, sqlite3_vtab_cursor **ppCursor){
  Rb64EachCursor *pCur;
  (void)pUnused;
  pCur = sqlite3_malloc(sizeof(*pCur));
  if( !pCur ) return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  *ppCursor = &pCur->base;
  return SQLITE_OK;
}

static int rb64EachClose(sqlite3_vtab_cursor *cur){
  Rb64EachCursor *pCur = (Rb64EachCursor *)cur;
  if( pCur->it  ) roaring64_iterator_free(pCur->it);
  if( pCur->bm  ) roaring64_bitmap_free(pCur->bm);
  if( pCur->buf ) sqlite3_free(pCur->buf);
  sqlite3_free(pCur);
  return SQLITE_OK;
}

static int rb64EachBestIndex(sqlite3_vtab *pUnused, sqlite3_index_info *pIdx){
  int i;
  (void)pUnused;
  for( i=0; i<pIdx->nConstraint; i++ ){
    if( pIdx->aConstraint[i].iColumn != 1 ) continue;
    if( pIdx->aConstraint[i].op != SQLITE_INDEX_CONSTRAINT_EQ ) continue;
    if( !pIdx->aConstraint[i].usable ) continue;
    pIdx->aConstraintUsage[i].argvIndex = 1;
    pIdx->aConstraintUsage[i].omit      = 1;
    pIdx->idxNum        = 1;
    pIdx->estimatedRows = 1000;
    pIdx->estimatedCost = 1.0;
    return SQLITE_OK;
  }
  pIdx->estimatedRows = 0;
  pIdx->estimatedCost = 1.0;
  return SQLITE_OK;
}

static int rb64EachFilter(
  sqlite3_vtab_cursor *cur,
  int idxNum, const char *idxStr,
  int argc, sqlite3_value **argv
){
  Rb64EachCursor *pCur = (Rb64EachCursor *)cur;
  uint64_t card;
  (void)idxNum; (void)idxStr;
  if( pCur->it  ){ roaring64_iterator_free(pCur->it); pCur->it  = NULL; }
  if( pCur->bm  ){ roaring64_bitmap_free(pCur->bm);   pCur->bm  = NULL; }
  if( pCur->buf ){ sqlite3_free(pCur->buf);            pCur->buf = NULL; }
  pCur->nBuf = pCur->iBuf = pCur->cap = 0;

  if( argc<1 || sqlite3_value_type(argv[0])==SQLITE_NULL ) return SQLITE_OK;

  pCur->bm = roaring64_bitmap_portable_deserialize_safe(
      sqlite3_value_blob(argv[0]),
      (size_t)sqlite3_value_bytes(argv[0]));
  if( !pCur->bm ){
    pCur->base.pVtab->zErrMsg = sqlite3_mprintf("rb64_each: invalid bitmap");
    return SQLITE_ERROR;
  }

  card = roaring64_bitmap_get_cardinality(pCur->bm);
  if( card==0 ) return SQLITE_OK;

  pCur->cap = (uint32_t)(card < RB64_EACH_CHUNK ? (uint32_t)card : RB64_EACH_CHUNK);
  pCur->buf = (uint64_t *)sqlite3_malloc64(pCur->cap * sizeof(uint64_t));
  if( !pCur->buf ){ roaring64_bitmap_free(pCur->bm); pCur->bm=NULL; return SQLITE_NOMEM; }

  pCur->it = roaring64_iterator_create(pCur->bm);
  if( !pCur->it ){ sqlite3_free(pCur->buf); pCur->buf=NULL; roaring64_bitmap_free(pCur->bm); pCur->bm=NULL; return SQLITE_NOMEM; }
  pCur->nBuf = roaring64_iterator_read(pCur->it, pCur->buf, pCur->cap);
  return SQLITE_OK;
}

static int rb64EachNext(sqlite3_vtab_cursor *cur){
  Rb64EachCursor *pCur = (Rb64EachCursor *)cur;
  if( ++pCur->iBuf >= pCur->nBuf ){
    pCur->nBuf = roaring64_iterator_read(pCur->it, pCur->buf, pCur->cap);
    pCur->iBuf = 0;
  }
  return SQLITE_OK;
}

static int rb64EachEof(sqlite3_vtab_cursor *cur){
  return ((Rb64EachCursor *)cur)->nBuf == 0;
}

static int rb64EachColumn(sqlite3_vtab_cursor *cur, sqlite3_context *ctx, int col){
  Rb64EachCursor *pCur = (Rb64EachCursor *)cur;
  if( col==0 ) sqlite3_result_int64(ctx, (sqlite3_int64)pCur->buf[pCur->iBuf]);
  return SQLITE_OK;
}

static int rb64EachRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid){
  Rb64EachCursor *pCur = (Rb64EachCursor *)cur;
  *pRowid = (sqlite_int64)pCur->buf[pCur->iBuf];
  return SQLITE_OK;
}

static sqlite3_module rb64EachModule = {
  0,                  /* iVersion */
  rb64EachConnect,    /* xCreate (== xConnect → eponymous) */
  rb64EachConnect,    /* xConnect */
  rb64EachBestIndex,  /* xBestIndex */
  rb64EachDisconnect, /* xDisconnect */
  rb64EachDisconnect, /* xDestroy */
  rb64EachOpen,       /* xOpen */
  rb64EachClose,      /* xClose */
  rb64EachFilter,     /* xFilter */
  rb64EachNext,       /* xNext */
  rb64EachEof,        /* xEof */
  rb64EachColumn,     /* xColumn */
  rb64EachRowid,      /* xRowid */
  0,                  /* xUpdate */
  0, 0, 0, 0,         /* xBegin..xRollback */
  0,                  /* xFindMethod */
  0,                  /* xRename */
  0, 0, 0,            /* xSavepoint..xShadowName */
};

#endif /* SQLITE_OMIT_VIRTUALTABLE */

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
  // 64 bit versions
  rc = sqlite3_create_function(db, "rb64_create", -1, flags, 0, roaring64CreateFunc, 0, 0);
  rc = sqlite3_create_function(db, "rb64_count", 1, flags, 0, roaring64LengthFunc, 0, 0);
  rc = sqlite3_create_function(db, "rb64_add", 2, flags, 0, roaring64AddFunc, 0, 0);
  rc = sqlite3_create_function(db, "rb64_remove", 2, flags, 0, roaring64RemoveFunc, 0, 0);
  rc = sqlite3_create_function(db, "rb64_and", 2, flags, 0, roaring64AndFunc, 0, 0);
  rc = sqlite3_create_function(db, "rb64_or", 2, flags, 0, roaring64OrFunc, 0, 0);
  rc = sqlite3_create_function(db, "rb64_not", 2, flags, 0, roaring64NotFunc, 0, 0);
  rc = sqlite3_create_function(db, "rb64_xor", 2, flags, 0, roaring64XorFunc, 0, 0);
  rc = sqlite3_create_function(db, "rb64_and_count", 2, flags, 0, roaring64AndLengthFunc, 0, 0);
  rc = sqlite3_create_function(db, "rb64_or_count", 2, flags, 0, roaring64OrLengthFunc, 0, 0);
  rc = sqlite3_create_function(db, "rb64_not_count", 2, flags, 0, roaring64NotLengthFunc, 0, 0);
  rc = sqlite3_create_function(db, "rb64_xor_count", 2, flags, 0, roaring64XorLengthFunc, 0, 0);

  //rc = sqlite3_create_function(db, "rb_and_many", -1, flags, 0, roaringAndManyFunc, 0, 0);
  //rc = sqlite3_create_function(db, "rb_or_many", -1, flags, 0, roaringOrManyFunc, 0, 0);
  // aggregate SQL functions
  rc = sqlite3_create_function(db, "rb_group_create", 1, flags, 0, 0, roaringCreateStep, roaringCreateFinal);
  rc = sqlite3_create_function(db, "rb_group_and", 1, flags, 0, 0, roaringAndAllStep, roaringAndAllFinal);
  rc = sqlite3_create_function(db, "rb_group_or", 1, flags, 0, 0, roaringOrAllStep, roaringOrAllFinal);
  // 64 bit versions
  rc = sqlite3_create_function(db, "rb64_group_create", 1, flags, 0, 0, roaring64CreateStep, roaring64CreateFinal);
  rc = sqlite3_create_function(db, "rb64_group_and", 1, flags, 0, 0, roaring64AndAllStep, roaring64AndAllFinal);
  rc = sqlite3_create_function(db, "rb64_group_or", 1, flags, 0, 0, roaring64OrAllStep, roaring64OrAllFinal);

  // carray based SQL functions (for conversion to a virtual table)
  rc = sqlite3_create_function(db, "rb_array", 1, flags, 0, roaringArrayFunc, 0, 0);
  // 64 bit version
  rc = sqlite3_create_function(db, "rb64_array", 1, flags, 0, roaring64ArrayFunc, 0, 0);
#ifndef SQLITE_OMIT_VIRTUALTABLE
  rc = sqlite3_create_module(db, "rb_each", &rbEachModule, 0);
  rc = sqlite3_create_module(db, "rb64_each", &rb64EachModule, 0);
#endif
  return rc;
}
