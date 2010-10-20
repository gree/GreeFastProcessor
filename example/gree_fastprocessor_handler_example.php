#!/usr/local/bin/php
<?php
require_once 'gree_fastprocessor.php';

$gfp = new Gree_FastProcessor();
$gfp->initialize();

// pre-requires (anything you need)
// from app/webroot/index.php
if (!defined('DS')) {
	define('DS', DIRECTORY_SEPARATOR);
}
if (!defined('ROOT')) {
	define('ROOT', '/home/cake');
}
if (!defined('APP_DIR')) {
	define('APP_DIR', 'app');
}
if (!defined('CAKE_CORE_INCLUDE_PATH')) {
	define('CAKE_CORE_INCLUDE_PATH', ROOT);
}
if (!defined('WEBROOT_DIR')) {
	define('WEBROOT_DIR', 'webroot');
}
if (!defined('WWW_ROOT')) {
	define('WWW_ROOT', ROOT . DS . APP_DIR . DS . WEBROOT_DIR . DS);
}
if (!defined('CORE_PATH')) {
	if (function_exists('ini_set')) {
		ini_set('include_path', CAKE_CORE_INCLUDE_PATH . PATH_SEPARATOR . ROOT . DS . APP_DIR . DS . PATH_SEPARATOR . ini_get('include_path'));
		define('APP_PATH', null);
		define('CORE_PATH', null);
	} else {
		define('APP_PATH', ROOT . DS . APP_DIR . DS);
		define('CORE_PATH', CAKE_CORE_INCLUDE_PATH . DS);
	}
}
if (!include(CORE_PATH . 'cake' . DS . 'bootstrap.php')) {
	trigger_error("Can't find CakePHP core.  Check the value of CAKE_CORE_INCLUDE_PATH in app/webroot/index.php.  It should point to the directory containing your " . DS . "cake core directory and your " . DS . "vendors root directory.", E_USER_ERROR);
}

for (;;) {
    $gfp->startup();
    $request = $gfp->getRequest();
    ob_start(null);

	// add your code here (followings are example)
	if (isset($_GET['url']) && $_GET['url'] === 'favicon.ico') {
		return;
	} else {
		$Dispatcher = new Dispatcher();
		$Dispatcher->dispatch($url);
	}
	if (Configure::read() > 0) {
		echo "<!-- " . round(getMicrotime() - $TIME_START, 4) . "s -->"; 
	}

	$content = ob_get_contents();
    ob_end_clean();
    $gfp->setResponse($content, strlen($content));
    $gfp->shutdown();
}
