--TEST--
Tideways: Symfony Support
--FILE--
<?php

include __DIR__ . '/common.php';
include __DIR__ . '/tideways_symfony.php';

tideways_enable();

$kernel = new \Symfony\Component\HttpKernel\Kernel();
$kernel->boot();

print_spans(tideways_get_spans());
tideways_disable();
--EXPECTF--
php.symfony: 1 timers - title=boot