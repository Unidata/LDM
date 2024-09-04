
for procNum in `ps -ef | grep noaaportIngester | grep -v grep | awk '{print $2}'`; do
	echo $procNum
	kill -9 $procNum
done
