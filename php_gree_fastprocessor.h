#ifndef PHP_GREE_FASTPROCESSOR_H
#define PHP_GREE_FASTPROCESSOR_H

extern zend_module_entry gree_fastprocessor_module_entry;
#define phpext_gree_fastprocessor_ptr &gree_fastprocessor_module_entry

#define PHP_GREE_FASTPROCESSOR_VERSION "0.0.1"

#ifdef PHP_WIN32
#define PHP_GREE_FASTPROCESSOR_API __declspec(dllexport)
#else
#define PHP_GREE_FASTPROCESSOR_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

PHP_MINIT_FUNCTION(gree_fastprocessor);
PHP_MSHUTDOWN_FUNCTION(gree_fastprocessor);
PHP_RINIT_FUNCTION(gree_fastprocessor);
PHP_RSHUTDOWN_FUNCTION(gree_fastprocessor);
PHP_MINFO_FUNCTION(gree_fastprocessor);

PHP_FUNCTION(gree_fastprocessor_listen);
PHP_FUNCTION(gree_fastprocessor_startup);

typedef struct _gree_fastprocessor_handler {
	int		rfd;
	int		wfd;
	pid_t	pid;

	int		request;

	int		active;
	int		kill;
	int		restart;
	int		reload;

	struct _gree_fastprocessor_handler *prev;
	struct _gree_fastprocessor_handler *next;
} gree_fastprocessor_handler;

typedef struct _gree_fast_processor_thread_ctx {
	int     sock;
	void*** ctx;
} gree_fast_processor_thread_ctx;

ZEND_BEGIN_MODULE_GLOBALS(gree_fastprocessor)
	int terminate;
	int concurrency;
	int max_request;
	int restart;
	gree_fastprocessor_handler *list;
	gree_fastprocessor_handler *inactive;
	gree_fastprocessor_handler *inactive_last;
	pthread_mutex_t mutex;
	pthread_mutex_t mutex_err;
ZEND_END_MODULE_GLOBALS(gree_fastprocessor)

/* In every utility function you add that needs to use variables 
   in php_gree_fastprocessor_globals, call TSRMLS_FETCH(); after declaring other 
   variables used by that function, or better yet, pass in TSRMLS_CC
   after the last function argument and declare your utility function
   with TSRMLS_DC after the last declared argument.  Always refer to
   the globals in your function as GREE_FASTPROCESSOR_G(variable).  You are 
   encouraged to rename these macros something shorter, see
   examples in any other php module directory.
*/

#ifdef ZTS
#define GREE_FASTPROCESSOR_G(v) TSRMG(gree_fastprocessor_globals_id, zend_gree_fastprocessor_globals *, v)
#else
#define GREE_FASTPROCESSOR_G(v) (gree_fastprocessor_globals.v)
#endif

#endif	/* PHP_GREE_FASTPROCESSOR_H */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * End:
 */
