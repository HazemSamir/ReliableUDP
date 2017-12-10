S_ROOT=./server_root
C_ROOT=./client_root

S_FILE=$(S_ROOT)/server.in
C_FILE=$(C_ROOT)/client.in

S_LOG=server.log
C_LOG=client.log

S_PORT='4444'
C_PORT='2222'

# Window size
S_W_SIZE=100000
C_W_SIZE=2500

# Drop packet random seed for server
RAND_SEED=-1.0

# Files names
LARGE=large.jpg
MED=medium.jpg
SMALL=small.txt

build_server:
	gcc 

snw_server:
	echo $(S_PORT) > $(S_FILE)
	# window_size=0 for stop-and-wait
	echo 0 >> $(S_FILE)
	echo $(RAND_SEED) >> $(S_FILE)
	# plp from command line
	echo $(plp) >> $(S_FILE)
	
	./bin/server server.in 2>&1 | tee $(S_LOG)

sr_server:
	echo $(S_PORT) > $(S_FILE)
	echo $(S_W_SIZE) >> $(S_FILE)
	echo $(RAND_SEED) >> $(S_FILE)
	# plp from command line
	echo $(plp) >> $(S_FILE)
	
	./bin/server server.in 2>&1 | tee $(S_LOG)

snw_client_l:
	echo $(S_PORT) > $(C_FILE)
	echo $(C_PORT) >> $(C_FILE)
	echo $(LARGE) >> $(C_FILE)
	# window_size=0 for stop-and-wait
	echo 0 >> $(C_FILE)
	
	./bin/client client.in 2>&1 | tee $(C_LOG)

sr_client_l:
	echo $(S_PORT) > $(C_FILE)
	echo $(C_PORT) >> $(C_FILE)
	echo $(LARGE) >> $(C_FILE)
	# window_size=0 for stop-and-wait
	echo $(C_W_SIZE) >> $(C_FILE)
	
	./bin/client client.in 2>&1 | tee $(C_LOG)

snw_client_m:
	echo $(S_PORT) > $(C_FILE)
	echo $(C_PORT) >> $(C_FILE)
	echo $(MED) >> $(C_FILE)
	# window_size=0 for stop-and-wait
	echo 0 >> $(C_FILE)
	
	./bin/client client.in 2>&1 | tee $(C_LOG)

