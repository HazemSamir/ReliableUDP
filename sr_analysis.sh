#!/bin/bash

analysis_file=sr_analysis.txt

echo 'selective repeat analysis' | tee $analysis_file

for plp in 0.01 0.05 0.1 0.3
do
	make sr_server plp=$plp > /dev/null &
	pid=$!
	
	printf '\n\n' | tee -a $analysis_file
	echo "plp=$plp" | tee -a $analysis_file

	for i in {1..5}
	do
		echo $i | tee -a $analysis_file
		make sr_client_l > /dev/null
		tail -3 client.log | tee -a $analysis_file
	done
	
	bash ./tkill.sh $pid
done
