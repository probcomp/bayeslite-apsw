/*
  Utility macros and functions

  See the accompanying LICENSE file.
*/

/* These macros are to address several issues:

  - Prevent simultaneous calls on the same object while the GIL is
  released in one thread.  For example if a Cursor is executing
  sqlite3_step with the GIL released, we don't want Cursor_execute
  called on another thread since that will thrash what the first
  thread is doing.  We use a member of Connection, Blob and Cursor
  named 'inuse' to provide the simple exclusion.

  - The GIL has to be released around all SQLite calls that take the
  database mutex (which is most of them).  If the GIL is kept even for
  trivial calls then deadlock will arise.  This is because if you have
  multiple mutexes you must always acquire them in the same order, or
  never hold more than one at a time.

  - The SQLite error code is not threadsafe.  This is because the
  error string is per database connection.  The call to sqlite3_errmsg
  will return a pointer but that can be replaced by any other thread
  with an error.  Consequently SQLite added sqlite3_db_mutex (see
  sqlite-dev mailing list for 4 Nov 2008).  A far better workaround
  would have been to make the SQLite error stuff be per thread just
  like errno.  Instead I have had to roll my own thread local storage
  system for storing the error message.
*/

/* call where no error is returned */
#define _PYSQLITE_CALL_V(x) \
  do { Py_BEGIN_ALLOW_THREADS { x; } Py_END_ALLOW_THREADS ; } while(0)

/* Calls where error could be set.  We assume that a variable 'res' is set.  Also need the db to take
   the mutex on */
#define _PYSQLITE_CALL_E(db, x)                     \
do {                                                \
  Py_BEGIN_ALLOW_THREADS                            \
    {                                               \
      sqlite3_mutex_enter(sqlite3_db_mutex(db));    \
      x;                                            \
      if(res!=SQLITE_OK && res!=SQLITE_DONE && res!=SQLITE_ROW) \
        apsw_set_errmsg(sqlite3_errmsg((db)));      \
      sqlite3_mutex_leave(sqlite3_db_mutex(db));    \
    }                                               \
  Py_END_ALLOW_THREADS;                             \
 } while(0)

#define INUSE_CALL(x)                               \
  do {                                              \
       assert(self->inuse==0); self->inuse=1;       \
       { x; }                                       \
       assert(self->inuse==1); self->inuse=0;       \
  } while(0)

/* call from blob code */
#define PYSQLITE_BLOB_CALL(y) INUSE_CALL(_PYSQLITE_CALL_E(self->connection->db, y))

/* call from connection code */
#define PYSQLITE_CON_CALL(y)  INUSE_CALL(_PYSQLITE_CALL_E(self->db, y))

/* call from cursor code - same as blob */
#define PYSQLITE_CUR_CALL PYSQLITE_BLOB_CALL

/* from statement cache */
#define PYSQLITE_SC_CALL(y)   _PYSQLITE_CALL_E(sc->db, y)

/* call to sqlite code that doesn't return an error */
#define PYSQLITE_VOID_CALL(y) INUSE_CALL(_PYSQLITE_CALL_V(y))

/* call from backup code */
#define PYSQLITE_BACKUP_CALL(y) INUSE_CALL(_PYSQLITE_CALL_E(self->dest->db, y))

#ifdef __GNUC__
#define APSW_ARGUNUSED __attribute__ ((unused))
#else
#define APSW_ARGUNUSED
#endif



/* used to decide if we will use int (4 bytes) or long long (8 bytes) */
#define APSW_INT32_MIN (-2147483647-1)
#define APSW_INT32_MAX 2147483647


/* 
   The default Python PyErr_WriteUnraiseable is almost useless.  It
   only prints the str() of the exception and the str() of the object
   passed in.  This gives the developer no clue whatsoever where in
   the code it is happening.  It also does funky things to the passed
   in object which can cause the destructor to fire twice.
   Consequently we use our version here.  It makes the traceback
   complete, and then tries the following, going to the next if
   the hook isn't found or returns an error:

   * excepthook of hookobject (if not NULL)
   * excepthook of sys module
   * PyErr_Display

   If any return an error then then the next one is tried.  When we
   return, any error will be cleared.
*/
static void 
apsw_write_unraiseable(PyObject *hookobject)
{
  PyObject *err_type=NULL, *err_value=NULL, *err_traceback=NULL;
  PyObject *excepthook=NULL;
  PyObject *result=NULL;
  PyFrameObject *frame=NULL;

#ifndef PYPY_VERSION
  /* fill in the rest of the traceback */
  frame = PyThreadState_GET()->frame;
  while(frame)
    {
      PyTraceBack_Here(frame);
      frame=frame->f_back;
    }
#endif 
  
  /* Get the exception details */
  PyErr_Fetch(&err_type, &err_value, &err_traceback);
  PyErr_NormalizeException(&err_type, &err_value, &err_traceback);

  if(hookobject)
    {
      excepthook=PyObject_GetAttrString(hookobject, "excepthook");
      PyErr_Clear();
      if(excepthook)
        {
          result=PyEval_CallFunction(excepthook, "(OOO)", err_type?err_type:Py_None, err_value?err_value:Py_None, err_traceback?err_traceback:Py_None);
          if(result)
            goto finally;
        }
      Py_XDECREF(excepthook);
    }

  excepthook=PySys_GetObject("excepthook");
  if(excepthook)
    {
      Py_INCREF(excepthook); /* borrowed reference from PySys_GetObject so we increment */
      PyErr_Clear();
      result=PyEval_CallFunction(excepthook, "(OOO)", err_type?err_type:Py_None, err_value?err_value:Py_None, err_traceback?err_traceback:Py_None);
      if(result) 
        goto finally;
    }

  /* remove any error from callback failure */
  PyErr_Clear();
#ifndef PYPY_VERSION
  PyErr_Display(err_type, err_value, err_traceback);
#endif

  finally:
  Py_XDECREF(excepthook);
  Py_XDECREF(result);
  Py_XDECREF(err_traceback);
  Py_XDECREF(err_value);
  Py_XDECREF(err_type);
  PyErr_Clear(); /* being paranoid - make sure no errors on return */
}



/* 
   Python's handling of Unicode is horrible.  It can use 2 or 4 byte
   unicode chars and the conversion routines like to put out BOMs
   which makes life even harder.  These macros are used in pairs to do
   the right form of conversion and tell us whether to use the plain
   or -16 version of the SQLite function that is about to be called.
*/

#if Py_UNICODE_SIZE==2
#define UNIDATABEGIN(obj) \
{                                                        \
  size_t strbytes=2*PyUnicode_GET_SIZE(obj);             \
  const void *strdata=PyUnicode_AS_DATA(obj);            

#define UNIDATAEND(obj)                                  \
}

#define USE16(x) x##16

#else  /* Py_UNICODE_SIZE!=2 */

#define UNIDATABEGIN(obj) \
{                                                        \
  Py_ssize_t strbytes=0;				 \
  const char *strdata=NULL;                              \
  PyObject *_utf8=NULL;                                  \
  _utf8=PyUnicode_AsUTF8String(obj);                     \
  if(_utf8)                                              \
    {                                                    \
      strbytes=PyBytes_GET_SIZE(_utf8);                  \
      strdata=PyBytes_AS_STRING(_utf8);                  \
    } 

#define UNIDATAEND(obj)                                  \
  Py_XDECREF(_utf8);                                     \
}

#define USE16(x) x

#endif /* Py_UNICODE_SIZE */

/* Converts sqlite3_value to PyObject.  Returns a new reference. */
static PyObject *
convert_value_to_pyobject(sqlite3_value *value)
{
  int coltype=sqlite3_value_type(value);

  APSW_FAULT_INJECT(UnknownValueType,,coltype=123456);

  switch(coltype)
    {
    case SQLITE_INTEGER:
      {
        sqlite3_int64 val=sqlite3_value_int64(value);
#if PY_MAJOR_VERSION<3
        if (val>=LONG_MIN && val<=LONG_MAX)
          return PyInt_FromLong((long)val);
#endif
        return PyLong_FromLongLong(val);
      }

    case SQLITE_FLOAT:
      return PyFloat_FromDouble(sqlite3_value_double(value));
      
    case SQLITE_TEXT:
      return convertutf8stringsize((const char*)sqlite3_value_text(value), sqlite3_value_bytes(value));

    case SQLITE_NULL:
      Py_RETURN_NONE;

    case SQLITE_BLOB:
      return converttobytes(sqlite3_value_blob(value), sqlite3_value_bytes(value));

    default:
      return PyErr_Format(APSWException, "Unknown sqlite column type %d!", coltype);
    }
  /* can't get here */
  assert(0);
  return NULL;
}

/* Converts column to PyObject.  Returns a new reference. Almost identical to above 
   but we cannot just use sqlite3_column_value and then call the above function as 
   SQLite doesn't allow that ("unprotected values") */
static PyObject *
convert_column_to_pyobject(sqlite3_stmt *stmt, int col)
{
  int coltype;

  _PYSQLITE_CALL_V(coltype=sqlite3_column_type(stmt, col));

  APSW_FAULT_INJECT(UnknownColumnType,,coltype=12348);

  switch(coltype)
    {
    case SQLITE_INTEGER:
      {
        sqlite3_int64 val;
        _PYSQLITE_CALL_V(val=sqlite3_column_int64(stmt, col));
#if PY_MAJOR_VERSION<3
        if (val>=LONG_MIN && val<=LONG_MAX)
          return PyInt_FromLong((long)val);
#endif
        return PyLong_FromLongLong(val);
      }

    case SQLITE_FLOAT:
      { 
        double d;
        _PYSQLITE_CALL_V(d=sqlite3_column_double(stmt, col));
        return PyFloat_FromDouble(d);
      }
    case SQLITE_TEXT:
      {
        const char *data;
        size_t len;
        _PYSQLITE_CALL_V( (data=(const char*)sqlite3_column_text(stmt, col), len=sqlite3_column_bytes(stmt, col)) );
        return convertutf8stringsize(data, len);
      }

    case SQLITE_NULL:
      Py_RETURN_NONE;

    case SQLITE_BLOB:
      {
        const void *data;
        size_t len;
        _PYSQLITE_CALL_V( (data=sqlite3_column_blob(stmt, col), len=sqlite3_column_bytes(stmt, col)) );
        return converttobytes(data, len);
      }

    default:
      return PyErr_Format(APSWException, "Unknown sqlite column type %d!", coltype);
    }
  /* can't get here */
  assert(0);
  return NULL;
}


/* Some macros used for frequent operations */

/* used by Connection and Cursor */
#define CHECK_USE(e)                                                \
  do \
  { if(self->inuse)                                                                                 \
      {    /* raise exception if we aren't already in one */                                                                         \
           if (!PyErr_Occurred())                                                                                                    \
             PyErr_Format(ExcThreadingViolation, "You are trying to use the same object concurrently in two threads or re-entrantly within the same thread which is not allowed."); \
           return e;                                                                                                                 \
      }                                                                                                                              \
  } while(0)

/* used by Connection */
#define CHECK_CLOSED(connection,e) do \
    { if(!(connection) || !(connection)->db) { PyErr_Format(ExcConnectionClosed, "The connection has been closed"); return e; } } while(0)

/* used by cursor */
#define CHECK_CURSOR_CLOSED(e)                                          \
  do                                                                    \
    {                                                                   \
      if(!self->connection)                                            \
        { PyErr_Format(ExcCursorClosed, "The cursor has been closed"); return e; } \
      else if(!self->connection->db)                                    \
        { PyErr_Format(ExcConnectionClosed, "The connection has been closed"); return e; } \
    } while(0)
         
/* It is 2009 - why do I have to write this? */
static char *apsw_strdup(const char *source)
{
  char *res=PyMem_Malloc(strlen(source)+1);
  if(res)
    strcpy(res, source);
  return res;
}
