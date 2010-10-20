#!/usr/local/bin/php
<?php
require_once('Console/Getopt.php');
require_once 'gree_fastprocessor.php';

// {{{ main
/**
 *	main
 *
 *	@access	public
 */
function main($arg_list) {
	array_shift($arg_list);
	list($mode, $ident, $concurrency, $max_request) = parse_arg_list($arg_list);

	if ($mode == "start") {
		$r = _gree_fastprocessor_bootstrap($ident, $concurrency, $max_request);
	} else if ($mode == "kill") {
		$r = _gree_fastprocessor_kill($ident);
	} else if ($mode =="restart") {
		$r = _gree_fastprocessor_restart($ident);
	} else if ($mode == "daemon") {
		$r = _gree_fastprocessor_daemon($ident, $concurrency, $max_request);
	} else {
		usage();
	}

	return 0;
}
// }}}

// {{{ _gree_fastprocessor_bootstrap
function _gree_fastprocessor_bootstrap($ident, $concurrency, $max_request) {
	if (defined(STDIN)) {
		fclose(STDIN);
	}

	if (defined(STDOUT)) {
		fclose(STDOUT);
	}

	if (defined(STDERR)) {
		fclose(STDERR);
	}

	$fdspec = array(
		array('file', '/dev/null', 'r'),
		array('file', '/dev/null', 'w'),
		array('file', '/dev/null', 'w'),
	);

	$option = "--daemon";
	if ($ident) {
		$option .= " --ident=" . escapeshellarg($ident);
	}
	if ($concurrency) {
		$option .= " --concurrency=" . escapeshellarg($concurrency);
	}
	if ($max_request) {
		$option .= " --max-request=" . escapeshellarg($max_request);
	}

	proc_open(__FILE__ . " $option &", $fdspec, $dummy);

	return 0;
}
// }}}

// {{{ _gree_fastprocessor_kill
function _gree_fastprocessor_kill($ident, $kill_for_restart = false) {
	$pid_file = sprintf("%s/_gree_fastprocessor.%s.pid", GREE_FASTPROCESSOR_PID_DIR, $ident);
	if (file_exists($pid_file) == false) {
		return false;
	}
	$pid = trim(file_get_contents($pid_file));
	posix_kill($pid, SIGTERM);
	usleep(0.1 * 1000 * 1000);
	_gree_fastprocessor_clear_pid($ident);

	if ($kill_for_restart == false) {
		$option_file = sprintf("%s/_gree_fastprocessor.%s.option", GREE_FASTPROCESSOR_PID_DIR, $ident);
		if (is_file($option_file)) {
			unlink($option_file);
		}
	}

	return $pid;
}
// }}}

// {{{ _gree_fastprocessor_restart
function _gree_fastprocessor_restart($ident) {
	$pid = _gree_fastprocessor_kill($ident, true);

	// wait for termination
	$n = 2;
	$proc = "/proc/$pid";
	do {
		if (is_dir($proc) == false) {
			break;
		}
		sleep(1);
		$n--;
	} while ($n > 0);

	$option_file = sprintf("%s/_gree_fastprocessor.%s.option", GREE_FASTPROCESSOR_PID_DIR, $ident);
	$option = array();
	if (is_file($option_file)) {
		$option = unserialize(file_get_contents($option_file));
	}
	if (isset($option['ident']) == false) {
		// no way
		return false;
	}
	if (isset($option['concurrency']) == false) {
		$option['concurrency'] = GREE_FASTPROCESSOR_DEFAULT_CONCURRENCY;
	}
	if (isset($option['max-request']) == false) {
		$option['max-request'] = GREE_FASTPROCESSOR_DEFAULT_MAX_REQUEST;
	}

	$r = _gree_async_bootstrap($option['ident'], $option['concurrency'], $option['max-request']);

	return $r;
}
// }}}

// {{{ _gree_fastprocessor_daemon
function _gree_fastprocessor_daemon($ident, $concurrency, $max_request) {
	posix_setsid();

	// check pid
	$r = _gree_fastprocessor_check_pid("gree_fastprocessor.$ident", getmypid(), GREE_FASTPROCESSOR_PID_DIR);
	if ($r == false) {
		print "another gree_fastprocessor has been already started...exiting\n";
		return $r;
	}

	// save option (for restarting)
	$option_file = sprintf("%s/_gree_fastprocessor.%s.option", GREE_FASTPROCESSOR_PID_DIR, $ident);
	$fp = fopen($option_file, "w");
	fwrite($fp, serialize(array('ident' => $ident, 'concurrency' => $concurrency, 'max-request' => $max_request)));
	fclose($fp);

	$gfp = new Gree_FastProcessor();
	$gfp->listen($ident, $concurrency, $max_request);

	// skip clearing pid file here (_gree_async_kill() will do this)
	return 0;
}
// }}}

// {{{ _gree_fastprocessor_check_pid
function _gree_fastprocessor_check_pid($ident, $pid, $dir = "/tmp") {
	$pid_file = sprintf("%s/_%s.pid", $dir, $ident);

	if (is_file($pid_file)) {
		$fp = fopen($pid_file, "r");
		$tmp_pid = fread($fp, 1024);
		fclose($fp);

		// posix_kill($pid, 0)...
		// ps ax | grep...
		if (is_dir("/proc/$tmp_pid")) {
			return false;
		}
	}

	$fp = fopen($pid_file, "w");
	fwrite($fp, $pid);
	fclose($fp);

	return true;
}
// }}}

// {{{ _gree_fastprocessor_clear_pid
function _gree_fastprocessor_clear_pid($ident, $dir = "/tmp") {
	$pid_file = sprintf("%s/_%s.pid", $dir, $ident);
	if (is_file($pid_file)) {
		unlink($pid_file);
	}
}
// }}}

// {{{ parse_arg_list
function parse_arg_list($arg_list) {
	list($opt_list, $additional) = Console_Getopt::getopt2($arg_list, "skri:c:m:dh", array("start", "kill", "restart", "ident=", "concurrency=", "max-request=", "daemon", "help"));

	$mode = null;
	$ident = null;
	$concurrency = GREE_FASTPROCESSOR_DEFAULT_CONCURRENCY;
	$max_request = GREE_FASTPROCESSOR_DEFAULT_MAX_REQUEST;
	if (is_null($opt_list)) {
		$opt_list = array();
	}
	foreach ($opt_list as $opt) {
		if (is_array($opt) && isset($opt[0])) {
			switch($opt[0]) {
			case 's':
			case '--start':
				if (is_null($mode) == false) {
					printf("-s, -k, -r and -d are exclusive\n\n");
					usage();
				}
				$mode = 'start';
				break;
			case 'k':
			case '--kill':
				if (is_null($mode) == false) {
					printf("-s, -k, -r and -d are exclusive\n\n");
					usage();
				}
				$mode = 'kill';
				break;
			case 'r':
			case '--restart':
				if (is_null($mode) == false) {
					printf("-s, -k, -r and -d are exclusive\n\n");
					usage();
				}
				$mode = 'restart';
				break;
			case 'i':
			case '--ident':
				$ident= $opt[1];
				break;
			case 'c':
			case '--concurrency':
				$concurrency = intval($opt[1]);
				if ($concurrency <= 0) {
					printf("invalid concurrency: $concurrency\n\n");
					usage();
				}
				break;
			case 'm':
			case '--max-request':
				$max_request = intval($opt[1]);
				if ($max_request <= 0) {
					printf("invalid max request parameter: $max_request\n\n");
					usage();
				}
				break;
			case 'd':
			case '--daemon':
				if (is_null($mode) == false) {
					printf("-s, -k, -r, -d, -w and -g are exclusive\n\n");
					usage();
				}
				$mode = 'daemon';
				break;
			case 'h':
			case '--help':
				usage();
				break;
			default:
				printf("unkown option [%s]\n\n", $opt[0]);
				usage();
				break;
			}
		}
	}

	if (is_null($ident)) {
		printf("option [--ident] is required\n\n");
		usage();
	}

	return array($mode, $ident, $concurrency, $max_request);
}
// }}}

// {{{ usage
function usage() {
	$usage = <<<EOD
gree_fastprocessor_listener.php [options]

options:
  -s, --start           start daemon
  -k, --kill            stop daemon
  -r, --restart         stop and start daemon

  -i, --ident           identifier
  -c, --concurrency     concurrency of worker processes
  -m, --max-request     max requests per worker process

internal-options:
  -d, --daemon          run as daemon process

and something else:
  -h, --help            show this message

EOD;
	print $usage;

	// bail out:)
	exit();
}
// }}}

exit(main($_SERVER['argv']));
// vim: foldmethod=marker tabstop=4 shiftwidth=4 autoindent
