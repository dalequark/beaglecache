#!/usr/bin/perl
$mc = "~shihm/mq/multicopy";
$mq = "~shihm/mq/multiquery";
$image = "waprox_image.tar.bz2";
#$command = "\"killall waprox cache; rm dbg_waprox* stat_waprox* exlog_waprox*; tar jxf $image; ./waprox; ./cache -N 1000000 -f 1 -P . &> /dev/null &\"";
$command = "\"killall waprox; rm dbg_waprox* stat_waprox* exlog_waprox*; tar jxf $image; ./waprox;\"";
print "deploying...\n";
`$mc $image \@:`;
`scp $image sihm\@spring.cs.princeton.edu:`;
print "running...\n";
`$mq $command`;
`ssh sihm\@spring.cs.princeton.edu $command`;
