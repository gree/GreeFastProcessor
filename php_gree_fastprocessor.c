#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <netdb.h>
#include <signal.h>

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_gree_fastprocessor.h"
#include "SAPI.h"

#undef sprintf

#define	_GREE_ADD_INACTIVE(h, skip, weak)	{ \
	if (skip == 0) { \
		pthread_mutex_lock(&GREE_FASTPROCESSOR_G(mutex)); \
	} \
	if (weak == 0 || (h)->active != 0) { \
		(h)->active = 0; \
		if (GREE_FASTPROCESSOR_G(inactive) == NULL) { \
			(h)->prev = NULL; \
			(h)->next = NULL; \
			GREE_FASTPROCESSOR_G(inactive) = (h); \
			GREE_FASTPROCESSOR_G(inactive_last) = (h); \
		} else { \
			GREE_FASTPROCESSOR_G(inactive_last)->next = (h); \
			(h)->prev = GREE_FASTPROCESSOR_G(inactive_last); \
			(h)->next = NULL; \
			GREE_FASTPROCESSOR_G(inactive_last) = (h); \
		} \
	} \
	if (skip == 0) { \
		pthread_mutex_unlock(&GREE_FASTPROCESSOR_G(mutex)); \
	} \
}

#define _GREE_GET_INACTIVE(h) { \
	pthread_mutex_lock(&GREE_FASTPROCESSOR_G(mutex)); \
	for (;;) { \
		if (GREE_FASTPROCESSOR_G(inactive) == NULL) { \
			(h) = NULL; \
			break; \
		} else { \
			(h) = GREE_FASTPROCESSOR_G(inactive); \
			if (GREE_FASTPROCESSOR_G(inactive)->next != NULL) { \
				GREE_FASTPROCESSOR_G(inactive) = GREE_FASTPROCESSOR_G(inactive)->next; \
				GREE_FASTPROCESSOR_G(inactive)->prev = NULL; \
			} else { \
				GREE_FASTPROCESSOR_G(inactive) = NULL; \
				GREE_FASTPROCESSOR_G(inactive_last) = NULL; \
			} \
			if ((h)->restart != 0) { \
				continue; \
			} \
			(h)->active = 1; \
			break; \
		} \
	} \
	pthread_mutex_unlock(&GREE_FASTPROCESSOR_G(mutex)); \
}

ZEND_DECLARE_MODULE_GLOBALS(gree_fastprocessor)

/* {{{ gree_fastprocessor_functions[] */
function_entry gree_fastprocessor_functions[] = {
	PHP_FE(gree_fastprocessor_listen,	NULL)
	PHP_FE(gree_fastprocessor_startup,	NULL)
	{NULL, NULL, NULL}
};
/* }}} */

/* {{{ gree_fastprocessor_module_entry */
zend_module_entry gree_fastprocessor_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"gree_fastprocessor",
	gree_fastprocessor_functions,
	PHP_MINIT(gree_fastprocessor),
	PHP_MSHUTDOWN(gree_fastprocessor),
	PHP_RINIT(gree_fastprocessor),		/* Replace with NULL if there's nothing to do at request start */
	PHP_RSHUTDOWN(gree_fastprocessor),	/* Replace with NULL if there's nothing to do at request end */
	PHP_MINFO(gree_fastprocessor),
#if ZEND_MODULE_API_NO >= 20010901
	"0.1", /* Replace with version number for your extension */
#endif
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_GREE_FASTPROCESSOR
ZEND_GET_MODULE(gree_fastprocessor)
#endif

/* {{{ php_gree_fastprocessor_init_globals
 */
static void php_gree_fastprocessor_init_globals(zend_gree_fastprocessor_globals *gree_fastprocessor_globals) {
	gree_fastprocessor_globals->terminate = 0;
	gree_fastprocessor_globals->concurrency = 0;
	gree_fastprocessor_globals->max_request = 0;
	gree_fastprocessor_globals->restart = 0;
	gree_fastprocessor_globals->list = NULL;
	gree_fastprocessor_globals->inactive = NULL;
	gree_fastprocessor_globals->inactive_last = NULL;
	pthread_mutex_init(&(gree_fastprocessor_globals->mutex), NULL);
	pthread_mutex_init(&(gree_fastprocessor_globals->mutex_err), NULL);
}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(gree_fastprocessor) {
	ZEND_INIT_MODULE_GLOBALS(gree_fastprocessor, php_gree_fastprocessor_init_globals, NULL);
	/* If you have INI entries, uncomment these lines 
	REGISTER_INI_ENTRIES();
	*/
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(gree_fastprocessor) {
	/* uncomment this line if you have INI entries
	UNREGISTER_INI_ENTRIES();
	*/
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request start */
/* {{{ PHP_RINIT_FUNCTION
 */
PHP_RINIT_FUNCTION(gree_fastprocessor) {
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request end */
/* {{{ PHP_RSHUTDOWN_FUNCTION
 */
PHP_RSHUTDOWN_FUNCTION(gree_fastprocessor) {
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(gree_fastprocessor) {
	php_info_print_table_start();
	php_info_print_table_row(2, "GREE fast processor", "enabled");
	php_info_print_table_row(2, "Version", PHP_GREE_FASTPROCESSOR_VERSION);
#ifdef GREE_FASTPROCESSOR_BUILDTIME
	php_info_print_table_row(2, "Build time", GREE_FASTPROCESSOR_BUILDTIME);
#endif
	php_info_print_table_end();

	/* Remove comments if you have entries in php.ini
	DISPLAY_INI_ENTRIES();
	*/
}
/* }}} */

static int _gree_fastprocessor_listen(char *sock_path, int sock_path_len TSRMLS_DC) {
	int sock = socket(PF_UNIX, SOCK_STREAM, 0);
	if (sock < 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "socket() failed (%s)", strerror(errno));
		return -1;
	}

	// socket attirbutes
	int tmp = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&tmp, sizeof(tmp)) < 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "setsockopt() failed (SO_REUSEADDR) (%s)", strerror(errno));
		return -1;
	}
	if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char*)&tmp, sizeof(tmp)) < 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "setsockopt() failed (SO_KEEPALIVE) (%s)", strerror(errno));
		return -1;
	}
	int flag = fcntl(sock, F_GETFL, 0);
	if (fcntl(sock, F_SETFL, flag | O_NONBLOCK) < 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "fcntl() failed (%s)", strerror(errno));
		return -1;
	}

	// bind + listen
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path)-1);
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "bind() failed (%s)", strerror(errno));
		return -1;
	}	
	if (listen(sock, 128) < 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "listen() failed (%s)", strerror(errno));
		return -1;
	}
	if (chmod(addr.sun_path, 0777) < 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "chmod() failed (%s)", strerror(errno));
		return -1;
	}

	return sock;
}

static int _gree_fastprocessor_exec(int sock, char *handler, int handler_len, gree_fastprocessor_handler *h, int skip_locking TSRMLS_DC) {
	int rfds[2];
	int wfds[2];
	pid_t pid;

	if (pipe(rfds) < 0) {
		pthread_mutex_lock(&GREE_FASTPROCESSOR_G(mutex_err));
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "pipe() failed (rfds) (%s)", strerror(errno));
		pthread_mutex_unlock(&GREE_FASTPROCESSOR_G(mutex_err));
		return -1;
	}
	if (pipe(wfds) < 0) {
		pthread_mutex_lock(&GREE_FASTPROCESSOR_G(mutex_err));
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "pipe() failed (wfds) (%s)", strerror(errno));
		pthread_mutex_unlock(&GREE_FASTPROCESSOR_G(mutex_err));
		return -1;
	}

	pid = fork();
	if (pid == -1) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "fork() failed (%s)", strerror(errno));
		return -1;
	}
	if (pid == 0) {
		// close current fds
		close(0);
		close(1);
		close(sock);
		int i;
		for (i = 0; i < GREE_FASTPROCESSOR_G(concurrency); i++) {
			if ((GREE_FASTPROCESSOR_G(list)+i)->rfd != wfds[0] && (GREE_FASTPROCESSOR_G(list)+i)->rfd != rfds[1]) {
				close((GREE_FASTPROCESSOR_G(list)+i)->rfd);
			}
			if ((GREE_FASTPROCESSOR_G(list)+i)->wfd != wfds[0] && (GREE_FASTPROCESSOR_G(list)+i)->wfd != rfds[1]) {
				close((GREE_FASTPROCESSOR_G(list)+i)->wfd);
			}
		}

		close(wfds[1]);
		close(rfds[0]);
		dup2(wfds[0], 0);
		close(wfds[0]);
		dup2(rfds[1], 1);
		close(rfds[1]);

		char *args[2];
		args[0] = handler;
		args[1] = NULL;
		if (execvp(handler, args) < 0) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "execvp() failed (%s)", strerror(errno));
			return -1;
		}
	} else {
		close(wfds[0]);
		close(rfds[1]);

		if (skip_locking == 0) {
			pthread_mutex_lock(&GREE_FASTPROCESSOR_G(mutex));
		}
		h->rfd = rfds[0];
		h->wfd = wfds[1];
		h->pid = pid;
		h->prev = NULL;
		h->next = NULL;
		h->request = 0;
		h->active = 0;
		h->kill = 0;
		h->restart = 0;
		h->reload = 0;
		if (skip_locking == 0) {
			pthread_mutex_unlock(&GREE_FASTPROCESSOR_G(mutex));
		}
	}

	return 0;
}

static int _gree_fastprocessor_kill(gree_fastprocessor_handler *list TSRMLS_DC) {
	int i;
	for (i = 0; i < GREE_FASTPROCESSOR_G(concurrency); i++) {
		pid_t pid = (list+i)->pid;
		if (pid == 0) {
			continue;
		}
		if ((list+i)->rfd > 0) {
			close((list+i)->rfd);
		}
		if ((list+i)->wfd > 0) {
			close((list+i)->wfd);
		}
		if (kill(pid, SIGTERM) == 0) {
			int status;
			waitpid(pid, &status, 0);
		}
	}

	return 0;
}

static gree_fastprocessor_handler* _gree_fastprocessor_get_inactive_handler(TSRMLS_D) {
	gree_fastprocessor_handler *h;
	int retry = 8;

	do {
		_GREE_GET_INACTIVE(h);
		if (h != NULL) {
			if (h->reload != 0) {
				kill(h->pid, SIGTERM);
				h = NULL;
			} else {
				break;
			}
		}
		retry--;
		if (retry <= 0) {
			break;
		}
		usleep(0.1 * 1000 * 1000);
	} while (h == NULL);
	if (h == NULL) {
		// failed to get inactive handler
		pthread_mutex_lock(&GREE_FASTPROCESSOR_G(mutex_err));
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "failed to get inactive handler");
		pthread_mutex_unlock(&GREE_FASTPROCESSOR_G(mutex_err));
		return NULL;
	}

	return h;
}

static int _gree_fastprocessor_read(int sock, char *p, int len TSRMLS_DC) {
	int n = 0;
	do {
		int r = read(sock, p+n, len-n);
		if (r <= 0) {
			return r;
		}
		n += r;
	} while ((len-n) > 0);

	return n;
}

static int _gree_fastprocessor_write(int sock, const char *p, int len TSRMLS_DC) {
	int n = 0;
	do {
		int r = write(sock, p+n, len-n);
		if (r <= 0) {
			return r;
		}
		n += r;
	} while ((len-n) > 0);

	return n;
}

static void* _gree_fastprocessor_thread(void* param) {
	// we care about thread race condition by myself and share thread context here, intentionally
    gree_fast_processor_thread_ctx* ctx = (gree_fast_processor_thread_ctx*)param;
	int sock = ctx->sock;
	TSRMLS_FETCH_FROM_CTX(ctx->ctx);
	free(ctx);

	gree_fastprocessor_handler *h = _gree_fastprocessor_get_inactive_handler(TSRMLS_C);
	if (h == NULL) {
		close(sock);
		return;
	}

	int len;
	char *p = NULL;

	// get request
	int n = read(sock, &len, sizeof(len));
	if (n <= 0) {
		pthread_mutex_lock(&GREE_FASTPROCESSOR_G(mutex_err));
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "read() failed (%s)", strerror(errno));
		pthread_mutex_unlock(&GREE_FASTPROCESSOR_G(mutex_err));
		goto cleanup;
	}
	p = malloc(len);
	n = _gree_fastprocessor_read(sock, p, len TSRMLS_CC);
	if (n != len) {
		pthread_mutex_lock(&GREE_FASTPROCESSOR_G(mutex_err));
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "read() failed (%s)", strerror(errno));
		pthread_mutex_unlock(&GREE_FASTPROCESSOR_G(mutex_err));
		goto cleanup;
	}

	// set request
	char q[64];
	sprintf(q, "%d\n", len);
	n = _gree_fastprocessor_write(h->wfd, q, strlen(q) TSRMLS_CC);
	if (n < 0) {
		pthread_mutex_lock(&GREE_FASTPROCESSOR_G(mutex_err));
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "write() failed (%s)", strerror(errno));
		pthread_mutex_unlock(&GREE_FASTPROCESSOR_G(mutex_err));
		goto cleanup;
	}
	n = _gree_fastprocessor_write(h->wfd, p, len TSRMLS_CC);
	if (n < 0) {
		pthread_mutex_lock(&GREE_FASTPROCESSOR_G(mutex_err));
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "write() failed (%s)", strerror(errno));
		pthread_mutex_unlock(&GREE_FASTPROCESSOR_G(mutex_err));
		goto cleanup;
	}
	free(p);
	p = NULL;

	// get response (magic)
	char magic[2];
	n = read(h->rfd, magic, sizeof(magic));
	if (n == 0) {
		goto cleanup;
	} else if (n < 0) {
		pthread_mutex_lock(&GREE_FASTPROCESSOR_G(mutex_err));
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "read() failed (magic bytes: %s)", strerror(errno));
		pthread_mutex_unlock(&GREE_FASTPROCESSOR_G(mutex_err));
		goto cleanup;
	}
	if (magic[0] != '\x01' || magic[1] != '\x02') {
		pthread_mutex_lock(&GREE_FASTPROCESSOR_G(mutex_err));
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "failed to read magic bytes, something is going wrong on handler process -> restarting");
		pthread_mutex_unlock(&GREE_FASTPROCESSOR_G(mutex_err));
		goto cleanup;
	}

	// get response
	n = read(h->rfd, &len, sizeof(len));
	if (n == 0) {
		goto cleanup;
	} else if (n < 0) {
		pthread_mutex_lock(&GREE_FASTPROCESSOR_G(mutex_err));
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "read() failed (content-length: %s)", strerror(errno));
		pthread_mutex_unlock(&GREE_FASTPROCESSOR_G(mutex_err));
		goto cleanup;
	}
	p = malloc(len);
	n = _gree_fastprocessor_read(h->rfd, p, len TSRMLS_CC);
	if (n != len) {
		pthread_mutex_lock(&GREE_FASTPROCESSOR_G(mutex_err));
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "read() failed (expected: %d, actual: %d, %s)", len, n, strerror(errno));
		pthread_mutex_unlock(&GREE_FASTPROCESSOR_G(mutex_err));
		goto cleanup;
	}

	// set response
	n = _gree_fastprocessor_write(sock, p, len TSRMLS_CC);
	if (n < 0) {
		pthread_mutex_lock(&GREE_FASTPROCESSOR_G(mutex_err));
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "write() failed (%s)", strerror(errno));
		pthread_mutex_unlock(&GREE_FASTPROCESSOR_G(mutex_err));
		goto cleanup;
	}

	h->request++;
	int killed = 0;
	if (GREE_FASTPROCESSOR_G(max_request) > 0 && h->request >= GREE_FASTPROCESSOR_G(max_request)) {
		kill(h->pid, SIGTERM);
		killed = 1;
	}

cleanup:
	shutdown(sock, SHUT_RDWR);
	close(sock);
	if (killed == 0) {
		_GREE_ADD_INACTIVE(h, 0, 1);
	}
	if (p) {
		free(p);
	}
}

static void _gree_fastprocessor_sigaction_chld(int signo) {
	TSRMLS_FETCH();
	int status;
	pid_t pid;
	
	for (;;) {
		pid = waitpid(-1, &status, WNOHANG);
		if (pid <= 0) {
			break;
		}
		// XXX: (><) - replace w/ zend_hash
		int i;
		for (i = 0; i < GREE_FASTPROCESSOR_G(concurrency); i++) {
			gree_fastprocessor_handler *h = GREE_FASTPROCESSOR_G(list)+i;
			if (h->pid == pid) {
				h->restart = 1;
				break;
			}
			h = h->next;
		}
	}
	GREE_FASTPROCESSOR_G(restart) = 1;
}

static void _gree_fastprocessor_sigaction_term(int signo) {
	TSRMLS_FETCH();
	GREE_FASTPROCESSOR_G(terminate) = 1;
}

static void _gree_fastprocessor_sigaction_hup(int signo) {
	TSRMLS_FETCH();
	int i;
	for (i = 0; i < GREE_FASTPROCESSOR_G(concurrency); i++) {
		gree_fastprocessor_handler *h = GREE_FASTPROCESSOR_G(list)+i;
		h->reload = 1;
	}
}

static int _gree_fastprocessor_set_sigaction(TSRMLS_D) {
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = _gree_fastprocessor_sigaction_chld;
	if (sigaction(SIGCHLD, &sa, NULL) < 0) {
		pthread_mutex_lock(&GREE_FASTPROCESSOR_G(mutex_err));
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "sigaction() failed (%s)", strerror(errno));
		pthread_mutex_unlock(&GREE_FASTPROCESSOR_G(mutex_err));
		return -1;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = _gree_fastprocessor_sigaction_term;
	if (sigaction(SIGTERM, &sa, NULL) < 0) {
		pthread_mutex_lock(&GREE_FASTPROCESSOR_G(mutex_err));
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "sigaction() failed (%s)", strerror(errno));
		pthread_mutex_unlock(&GREE_FASTPROCESSOR_G(mutex_err));
		return -1;
	}
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = _gree_fastprocessor_sigaction_term;
	if (sigaction(SIGINT, &sa, NULL) < 0) {
		pthread_mutex_lock(&GREE_FASTPROCESSOR_G(mutex_err));
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "sigaction() failed (%s)", strerror(errno));
		pthread_mutex_unlock(&GREE_FASTPROCESSOR_G(mutex_err));
		return -1;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = _gree_fastprocessor_sigaction_hup;
	if (sigaction(SIGHUP, &sa, NULL) < 0) {
		pthread_mutex_lock(&GREE_FASTPROCESSOR_G(mutex_err));
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "sigaction() failed (%s)", strerror(errno));
		pthread_mutex_unlock(&GREE_FASTPROCESSOR_G(mutex_err));
		return -1;
	}

	return 0;
}

static int _gree_fastprocessor_unset_sigaction(TSRMLS_D) {
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	if (sigaction(SIGCHLD, &sa, NULL) < 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "sigaction() failed (%s)", strerror(errno));
		return -1;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	if (sigaction(SIGTERM, &sa, NULL) < 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "sigaction() failed (%s)", strerror(errno));
		return -1;
	}
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	if (sigaction(SIGINT, &sa, NULL) < 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "sigaction() failed (%s)", strerror(errno));
		return -1;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	if (sigaction(SIGHUP, &sa, NULL) < 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "sigaction() failed (%s)", strerror(errno));
		return -1;
	}

	return 0;
}

/* {{{ proto string gree_fastprocessor_listen(string arg, [array options])
   Return a string to confirm that the module is compiled in */
PHP_FUNCTION(gree_fastprocessor_listen) {
	char *sock_path = NULL;
	int sock_path_len;
	char *handler = NULL;
	int handler_len;
	int concurrency = 0;
	int max_request = 0;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ssll", &sock_path, &sock_path_len, &handler, &handler_len, &concurrency, &max_request) == FAILURE) {
		return;
	}

	GREE_FASTPROCESSOR_G(concurrency) = concurrency;
	GREE_FASTPROCESSOR_G(max_request) = max_request;

	// signals
	_gree_fastprocessor_set_sigaction(TSRMLS_C);

	int sock = _gree_fastprocessor_listen(sock_path, sock_path_len TSRMLS_CC);
	if (sock < 0) {
		RETURN_FALSE;
	}

    int sock_epoll = epoll_create(4);
	if (sock_epoll < 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "epoll_create() failed (%s)", strerror(errno));
		RETURN_FALSE;
    }
	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN;
	ev.data.fd = sock;
	if (epoll_ctl(sock_epoll, EPOLL_CTL_ADD, sock, &ev) < 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "epoll_ctl() failed (%s)", strerror(errno));
		RETURN_FALSE;
	}

	int retval = 0;

	// run handlers
	GREE_FASTPROCESSOR_G(list) = malloc(concurrency * sizeof(gree_fastprocessor_handler));
	memset(GREE_FASTPROCESSOR_G(list), 0, sizeof(gree_fastprocessor_handler) * concurrency);
	int i;
	for (i = 0; i < concurrency; i++) {
		if (_gree_fastprocessor_exec(sock, handler, handler_len, GREE_FASTPROCESSOR_G(list)+i, 0 TSRMLS_CC) < 0) {
			goto cleanup;
		}
		_GREE_ADD_INACTIVE(GREE_FASTPROCESSOR_G(list)+i, 0, 0);
	}

	// accept (main loop)
	while (GREE_FASTPROCESSOR_G(terminate) == 0) {
		// XXX
		if (GREE_FASTPROCESSOR_G(restart) != 0) {
			pthread_mutex_lock(&GREE_FASTPROCESSOR_G(mutex));
			GREE_FASTPROCESSOR_G(restart) = 0;
			int i;
			for (i = 0; i < concurrency; i++) {
				gree_fastprocessor_handler *h = GREE_FASTPROCESSOR_G(list)+i;
				if (h->restart == 0) {
					continue;
				}
				int active = h->active;
				close(h->rfd);
				close(h->wfd);
				_gree_fastprocessor_exec(sock, handler, handler_len, h, 1 TSRMLS_CC);
				if (active != 0) {
					_GREE_ADD_INACTIVE(h, 1, 0);
				}
			}
			pthread_mutex_unlock(&GREE_FASTPROCESSOR_G(mutex));
		}

		struct epoll_event ev_list[4];
		int n = epoll_wait(sock_epoll, ev_list, 4, -1);
		if (n < 0) {
			if (errno == EAGAIN || errno == EINTR) {
				continue;
			}
			pthread_mutex_lock(&GREE_FASTPROCESSOR_G(mutex_err));
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "epoll_wait() failed (%s)", strerror(errno));
			pthread_mutex_unlock(&GREE_FASTPROCESSOR_G(mutex_err));
			goto cleanup;
		}

		struct sockaddr addr;
		socklen_t addr_len;
		int client, i;
		for (i = 0; i < 4; i++) {
			addr_len = sizeof(addr);
			client = accept(sock, &addr, &addr_len);
			if (client >= 0) {
				break;
			}
			if (errno == EAGAIN || errno == EINTR) {
				continue;
			} else {
				break;
			}
		}
		if (client < 0) {
			pthread_mutex_lock(&GREE_FASTPROCESSOR_G(mutex_err));
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "accept() failed (%s)", strerror(errno));
			pthread_mutex_unlock(&GREE_FASTPROCESSOR_G(mutex_err));
			goto cleanup;
		}

		// handle client
        gree_fast_processor_thread_ctx* ctx = malloc(sizeof(gree_fast_processor_thread_ctx));
		ctx->sock = client;
		TSRMLS_SET_CTX(ctx->ctx);
		pthread_t thr_id;
		pthread_attr_t thr_attr;
		pthread_attr_init(&thr_attr);
		pthread_attr_setdetachstate(&thr_attr, PTHREAD_CREATE_DETACHED);
		if (pthread_create(&thr_id, &thr_attr, _gree_fastprocessor_thread, (void*)ctx) != 0) {
			pthread_mutex_lock(&GREE_FASTPROCESSOR_G(mutex_err));
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "pthread_create() failed (%s)", strerror(errno));
			pthread_mutex_unlock(&GREE_FASTPROCESSOR_G(mutex_err));
			goto cleanup;
		}
	}

	retval = 1;

cleanup:
	close(sock);
	close(sock_epoll);
	unlink(sock_path);
	_gree_fastprocessor_unset_sigaction(TSRMLS_C);
	_gree_fastprocessor_kill(GREE_FASTPROCESSOR_G(list) TSRMLS_CC);
	free(GREE_FASTPROCESSOR_G(list));

	RETURN_BOOL(retval);
}
/* }}} */

/* {{{ proto bool gree_fastprocessor_startup() */
PHP_FUNCTION(gree_fastprocessor_startup) {
	// following operation should be allowed only if sapi == cli, check this?
	SG(headers_sent) = 0;

	RETURN_BOOL(1);
}
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
