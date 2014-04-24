ts=`date`
result="result.$ts.txt"
echo $ts > $result

while true
do
	sh test-stability.sh | tee -a "$result"
done
