#!/usr/bin/perl
#########################################################
# [setting]
# - dummyClient + HashCache [sea.cs.princeton.edu]
# - client-side waprox [spring.cs.princeton.edu]
# - server-side waprox [csplanetlab4.kaist.ac.kr]
# - HashCache + server [nclab.kaist.ac.kr]
#########################################################

$clientAddr = "sea.cs.princeton.edu";
$clientID = "sihm";
$clientWaproxAddr = "spring.cs.princeton.edu";
$clientWaproxID = "sihm";
$serverWaproxAddr = "csplanetlab4.kaist.ac.kr";
$serverWaproxID = "princeton_wa";
$serverAddr = "nclab.kaist.ac.kr";
$serverID = "shihm";
$serverIP = "143.248.135.88";
#$serverAddr = "nclab.kaist.ac.kr";
#$serverIP = "143.248.135.88";

# hashcache setting
$numObjects = 10000;
$diskSize = 2;

# dummy client setting
#$URLfilename = "url20071231.out";
$URLfilename = "10000.txt";
$interval = 1;
$numClients = 50;
$numURLs = 100;

sub runWaprox
{
	# run waprox
	`./run.pl`;
}

sub runHashCache
{
	# run server side HashCache (-w: no caching)
	$command = "\"killall cache; ./cache $_[0] -N $numObjects -s $diskSize -P . &> cache_log &\"";
	`ssh $serverID\@$serverAddr $command`;

	# run client side HashCache
	$command = "\"killall cache; ./cache -N $numObjects -s $diskSize -P . -G $serverIP &> cache_log &\"";
	`ssh $clientID\@$clientAddr $command`;

	# wait for finishing setting up
	sleep(5);
	print "HashCache started\n";
}

sub oneDummyClient
{
	# run dummyClient
	#$arg = "\"./dum.pl $URLfilename $interval $numClients $numURLs\"";
	#$arg = "\"./ev_dummy $numClients $URLfilename 127.0.0.1 33333 5\"";
	$arg = "\"./alexa.sh; ./alexa.sh; ./alexa.sh; ./alexa.sh; ./alexa.sh\"";
	$command = "ssh $clientID\@$clientAddr $arg";
	print "$command\n";
	system $command;

	$now = time;
	getWaproxLog($now);
	getHashCacheLog($now);
}

sub getHashCacheLog
{
	# save hashcache log
	print "getting HashCache log...$_[0]\n";
	sleep(5);

	# from client
	$filename = "hashcache.$clientAddr.$_[0].tar";
	$tar = "\"tar zcf $filename dat/log*\"";
	$command = "ssh $clientID\@$clientAddr $tar";
	system $command;
	$command = "scp $clientID\@$clientAddr:$filename logs/";
	system $command;
	$rm = "\"rm $filename\"";
	$command = "ssh $clientID\@$clientAddr $rm";
	system $command;

	# from server
	$filename = "hashcache.$serverAddr.$_[0].tar";
	$tar = "\"tar zcf $filename dat/log*\"";
	$command = "ssh $serverID\@$serverAddr $tar";
	system $command;
	$command = "scp $serverID\@$serverAddr:$filename logs/";
	system $command;
	$rm = "\"rm $filename\"";
	$command = "ssh $serverID\@$serverAddr $rm";
	system $command;
}

sub getWaproxLog
{
	# save waprox log
	print "getting Waprox log...$_[0]\n";
	sleep(5);

	# from client waprox
	$filename = "waprox.$clientWaproxAddr.$_[0].tar";
	$tar = "\"tar zcf $filename stat* exlog* dbg* waprox.conf peer.conf\"";
	$command = "ssh $clientWaproxID\@$clientWaproxAddr $tar";
	system $command;
	$command = "scp $clientWaproxID\@$clientWaproxAddr:$filename logs/";
	system $command;
	$rm = "\"rm $filename\"";
	$command = "ssh $clientWaproxID\@$clientWaproxAddr $rm";
	system $command;

	# from server waprox
	$filename = "waprox.$serverWaproxAddr.$_[0].tar";
	$tar = "\"tar zcf $filename stat* exlog* dbg* waprox.conf peer.conf\"";
	$command = "ssh $serverWaproxID\@$serverWaproxAddr $tar";
	system $command;
	$command = "scp $serverWaproxID\@$serverWaproxAddr:$filename logs/";
	system $command;
	$rm = "\"rm $filename\"";
	$command = "ssh $serverWaproxID\@$serverWaproxAddr $rm";
	system $command;
}

sub oneSingleGet
{
	$arg = "\"wget --progress=bar:force -O /dev/null http://nclab.kaist.ac.kr/~shihm/BMQ.tar 2>&1 | grep saved\"";
	$command = "ssh $clientID\@$clientAddr $arg";
	print "$command\n";
	system $command;
	getWaproxLog(time);
}

sub expSingleFile
{
	for ($start = 1; $start <= 5; $start++) {
		print "$start trial =========================================\n";
		runWaprox();
		oneSingleGet();
		oneSingleGet();
		oneSingleGet();
		oneSingleGet();
	}
}

sub expDummyClient
{
	if ($_[0] == "-w") {
		print "### NO caching on the server-side HashCache\n";
	}
	else {
		print "### WITH caching on the server-side HashCache\n";
	}

	# 1. cold waprox + cold cache
	print "1. cold waprox + cold cache =====================================\n";
	runWaprox();
	runHashCache($_[0]);
	oneDummyClient();

	# 2. warm waprox + warm cache 
	print "2. warm waprox + warm cache =====================================\n";
	oneDummyClient();

	# 3. cold waprox + warm cache
	print "3. cold waprox + warm cache =====================================\n";
	runWaprox();
	oneDummyClient();

	# 4. warm waprox + cold cache
	print "4. warm waprox + cold cache =====================================\n";
	runHashCache($_[0]);
	oneDummyClient();
}

sub expBase
{
	# kill waprox first
	`./killwa.pl`;
	print "1. wget result =====================================\n";
	oneSingleGet();
	runHashCache();
	print "2. cold cache =====================================\n";
	oneDummyClient();
	print "3. warm cache =====================================\n";
	oneDummyClient();
}

#expBase();
#expSingleFile();
#expDummyClient("-w"); # no caching on server-side HashCache
#expDummyClient();
#oneSingleGet();
#getWaproxLog(time);
#getHashCacheLog(time);
#runHashCache();
#oneDummyClient();

expDummyClient("-w"); # no caching on server-side HashCache
