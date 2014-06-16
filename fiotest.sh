if [ $# = 2 ]
then 
  if [ -f $1 ];
  then
  	if [ -f $2 ];
	then
	  echo "Cannot overwrite file $2"
	else
	  fio $1 --minimal > $2
	  echo "\n\n" >> $2
	  cat $1 >> $2	
	fi
  else
    echo "Config file does not exist"
  fi
else
  echo "Usage: $0 configfilepath resultsfilename"
fi


