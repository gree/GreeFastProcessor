<?php
define('GREE_FASTPROCESSOR_DEFAULT_CONCURRENCY',	16);
define('GREE_FASTPROCESSOR_DEFAULT_MAX_REQUEST',	256);
define('GREE_FASTPROCESSOR_PID_DIR',				'/tmp');

class Gree_FastProcessor {
	/**#@+	@access	private	*/

	var	$ident_list = array(
		// sample
		'sample'	=> '/path/to/handler.php',
	);

	/**#@-*/

	/**
	 *	[handler] initialize
	 */
	function initialize() {
		// startup
		define('GREE_FASTPROCESSOR', true);
	}

	/**
	 *	[handler] startup
	 */
	function startup() {
		// header
		$GLOBALS['__GREE_FASTPROCESSOR_HEADER__'] = array();
	}

	/**
	 *	[handler] startup (session)
	 */
	function startupSession() {
		session_cache_limiter(false);
	}

	/**
	 *	[handler] shutdown
	 */
	function shutdown() {
        session_commit();
		session_id(null);

		clearstatcache();
	}

	/**
	 *	[handler] read and unserialize given request
	 */
	function getRequest() {
		$n = trim(fgets(STDIN));
		$request = fread(STDIN, $n);
		$request = unserialize($request);

		if (isset($request['get']) == false) {
			return false;
		}

		$_GET = $request['get'];
		$_POST = $request['post'];
		$_REQUEST = $request['request'];
		$_SERVER = $request['server'];
		$_SERVER['__HEADER__'] = $request['header'];

		return $request;
	}

	/**
	 *	[handler] set response
	 */
	function setResponse($content, $content_length) {
		$response = "";
		foreach ($GLOBALS['__GREE_FASTPROCESSOR_HEADER__'] as $header) {
			$response .= "$header\n";
		}
		$response .= "\n$content_length\n$content";

		print "\x01\x02";	// magic
		print pack('L', strlen($response));
		print $response;

		return true;
	}

	/**
	 *	handle end user request
	 */
	function run($ident) {
		$sock_path = "/tmp/gree_fastprocessor.$ident.sock";
		if (file_exists($sock_path) == false) {
			return false;
		}
		$sock = socket_create(AF_UNIX, SOCK_STREAM, 0);
		if ($sock == false) {
			return false;
		}
		if (socket_connect($sock, $sock_path) == false) {
			return false;
		}

		$request = array();
		$request['get'] = $_GET;
		$request['post'] = $_POST;
		$request['request'] = $_REQUEST;
		$request['server'] = $_SERVER;
		$request['header'] = getallheaders();
		$request = serialize($request);

		if (socket_write($sock, pack('L', strlen($request))) === false) {
			return false;
		}
		if (socket_write($sock, $request, strlen($request)) === false) {
			return false;
		}

		$n = 0;
		do {
			$s = socket_read($sock, 0xffff, PHP_NORMAL_READ);
			if ($s === false || $s === "") {
				// connection reset by peer (handler caused fatal error or called exit())
				return false;
			}
			if ($s == "\n") {
				break;
			}
			header(trim($s));
			$n++;
		} while ($n < 16);
		if ($n == 16) {
			return false;
		}
		$n = trim(socket_read($sock, 0xff, PHP_NORMAL_READ));
        $content = "";
		while (strlen($content) < $n) {
			$s = socket_read($sock, $n);
			if ($s === false || $s === "") {
				// connection reset by peer (handler caused fatal error or called exit())
				return false;
			}
			$content .= $s;
		}
		print $content;

		return true;
	}

	/**
	 *	listen requests via uds
	 */
	function listen($ident, $concurrency, $max_request) {
		if (isset($this->ident_list[$ident]) == false) {
			return false;
		}
		$handler = $this->ident_list[$ident];
		var_dump($handler);
		$sock_path = "/tmp/gree_fastprocessor.$ident.sock";
		return gree_fastprocessor_listen($sock_path, $handler, $concurrency, $max_request);
	}

	/**
	 *	alternative function for getallheaders()
	 */
	function getAllHeaders() {
		if (defined('GREE_FASTPROCESSOR')) {
			if (isset($_SERVER['__HEADER__'])) {
				return $_SERVER['__HEADER__'];
			} else {
				return array();
			}
		} else {
			return getallheaders();
		}
	}

	/**
	 *	alternative function for header()
	 */
	function header($header) {
		if (defined('GREE_FASTPROCESSOR')) {
			$GLOBALS['__GREE_FASTPROCESSOR_HEADER__'][] = $header;
		} else {
			header($header);
		}
	}
}
// vim: foldmethod=marker tabstop=4 shiftwidth=4 autoindent
