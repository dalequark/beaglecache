#!/usr/bin/perl
$mq = "~shihm/mq/multiquery";
$command = "\"killall waprox cache\"";
`$mq $command`;
`ssh sihm\@spring.cs.princeton.edu $command`;
