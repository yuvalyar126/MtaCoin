#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sched.h>
#include <zlib.h>
#include <stdbool.h>

#define NUM_MINERS 5
int difficulty;
typedef struct BLOCK_T {
    int height; // Incremental ID of the block in the chain
    int timestamp; // Time of the mine in seconds since epoch
    unsigned int hash; // Current block hash value
    unsigned int prev_hash; // Hash value of the previous block
    int difficulty; // Amount of preceding zeros in the hash
    int nonce; // Incremental integer to change the hash value
    int relayed_by; // Miner ID
    struct BLOCK_T* prev; // Pointer to the previous block
} BLOCK_T;

BLOCK_T* blockchain = NULL; // Pointer to the head of the blockchain
BLOCK_T* mined_block = NULL; // New block from miners
int block_count = 0;
pthread_mutex_t block_chain_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t block_chain_cond = PTHREAD_COND_INITIALIZER;
bool new_block_available = false;


unsigned int calculate_crc32(BLOCK_T block) {
    unsigned int crc = crc32(0L, Z_NULL, 0);
    // Calculate CRC32 on the height
    crc = crc32(crc, (const unsigned char*)&block.height, sizeof(block.height));
    // Calculate CRC32 on the timestamp
    crc = crc32(crc, (const unsigned char*)&block.timestamp, sizeof(block.timestamp));
    // Calculate CRC32 on the prev_hash
    crc = crc32(crc, (const unsigned char*)&block.prev_hash, sizeof(block.prev_hash));
    // Calculate CRC32 on the nonce
    crc = crc32(crc, (const unsigned char*)&block.nonce, sizeof(block.nonce));
    // Calculate CRC32 on the relayed_by
    crc = crc32(crc, (const unsigned char*)&block.relayed_by, sizeof(block.relayed_by));
    return crc;
}

// Function to verify the hash difficulty
bool verify_difficulty(unsigned int hash) {
    return (hash >> (32 - difficulty)) == 0;
}

// Server thread function
void* server_thread(void* arg) {
    // Create genesis block
    BLOCK_T* genesis_block = malloc(sizeof(BLOCK_T));

    *genesis_block = (BLOCK_T){0, (int)time(NULL), 0, 0, difficulty, 0, -1, NULL};
    genesis_block->hash = calculate_crc32(*genesis_block);
    blockchain = genesis_block;
    block_count++;
    printf("Genesis block created: {height: %d, hash: 0x%x}\n", genesis_block->height, genesis_block->hash);

    while (1) {
        pthread_mutex_lock(&block_chain_mutex);
        while (!new_block_available) {
            pthread_cond_wait(&block_chain_cond, &block_chain_mutex); // waits until a new block is mined
        }

        BLOCK_T* new_block = mined_block;
        // verify new block proof of work
        if (!verify_difficulty(calculate_crc32(*mined_block))) {
            printf("Check 1: Wrong hash for block #%d by miner %d, received 0x%x but calculated 0x%x\n",
                   new_block->height, new_block->relayed_by, new_block->hash, calculate_crc32(*new_block));
            free(new_block);
        }
            // verify new block height is correct
        else if (new_block->height != block_count) {
            printf("Check 2: Wrong height for block by miner %d, height is %d but should be %d\n",
                   new_block->relayed_by, new_block->height, block_count);
            free(new_block);
        }
            // verify new block prev hash matches the last block mined hash
        else if (new_block->prev_hash != blockchain->hash) {
            printf("Check 3: Wrong prev hash for block #%d by miner %d, new block prev hash: 0x%x current hash 0x%x\n",
                   new_block->height, new_block->relayed_by, new_block->prev_hash, blockchain->hash);
            free(new_block);
        }
        else {
            new_block->prev = blockchain;
            blockchain = new_block;
            block_count++;
            printf("Server: New block added by %d, attributes: height: %d, timestamp: %d, hash: 0x%x, prev_hash: 0x%x, difficulty: %d, nonce: %d\n",
                   new_block->relayed_by, new_block->height, new_block->timestamp, new_block->hash, new_block->prev_hash, new_block->difficulty, new_block->nonce);
        }
        new_block_available = false;
        pthread_cond_broadcast(&block_chain_cond);
        pthread_mutex_unlock(&block_chain_mutex);
    }
}


void* miner_thread(void* arg) {
    int miner_id = *(int*)arg;

    while (1) {
        pthread_mutex_lock(&block_chain_mutex);
        while (new_block_available) {
            pthread_cond_wait(&block_chain_cond, &block_chain_mutex);
        }

        BLOCK_T* new_block = malloc(sizeof(BLOCK_T));
        if (new_block == NULL) {
            perror("Failed to allocate memory for new block");
            exit(EXIT_FAILURE);
        }
        // Set new block attributes
        new_block->height = blockchain->height + 1;
        new_block->prev_hash = blockchain->hash;
        new_block->difficulty = difficulty;
        new_block->relayed_by = miner_id;
        new_block->nonce = 0;
        new_block->prev = NULL;
        new_block->timestamp = (int)time(NULL);
        new_block->hash = calculate_crc32(*new_block);
        pthread_mutex_unlock(&block_chain_mutex);



        // Keep updating the nonce and recalculating the hash until the difficulty is met
        while (!verify_difficulty(new_block->hash)) {
            new_block->nonce++;
            new_block->timestamp = (int)time(NULL);
            new_block->hash = calculate_crc32(*new_block);
        }

        pthread_mutex_lock(&block_chain_mutex);
        // checks if a new block haven't been mined if there is a new block start over
        if (new_block->height==blockchain->height+1) {
            mined_block = new_block;
            new_block_available = true;
            printf("Miner #%d: Mined a new block #%d, with the hash 0x%x\n", miner_id, new_block->height, new_block->hash);
            pthread_cond_signal(&block_chain_cond);
        }
        else {
            free(new_block); // Free the block if another miner has already mined the block
        }

        pthread_mutex_unlock(&block_chain_mutex);


    }
}
void* broken_miner(void* arg) {
    int miner_id = *(int*)arg;

    while (1) {
    pthread_mutex_lock(&block_chain_mutex);
        while (new_block_available) {
            pthread_cond_wait(&block_chain_cond, &block_chain_mutex);
        }

        BLOCK_T* new_block = malloc(sizeof(BLOCK_T));
        if (new_block == NULL) {
            perror("Failed to allocate memory for new block");
            exit(EXIT_FAILURE);
        }
        // Set new block attributes
        new_block->height = blockchain->height + 1;
        new_block->prev_hash = blockchain->hash;
        new_block->difficulty = difficulty;
        new_block->relayed_by = miner_id;
        new_block->nonce = 0;
        new_block->prev = NULL;
        new_block->timestamp = (int)time(NULL);
        new_block->hash = calculate_crc32(*new_block);



            mined_block = new_block;
            new_block_available = true;
            printf("Broken miner #%d: Mined a new block #%d, with the hash 0x%x\n", miner_id, new_block->height, new_block->hash);
            pthread_cond_signal(&block_chain_cond);

        pthread_mutex_unlock(&block_chain_mutex);
sleep(1);

    }
}
int main(int argc, char* argv[]) {

pthread_t server;
    pthread_attr_t attr;
    struct sched_param param;

    if(argc != 2) {
        printf("Usage: %s <number>\n", argv[0]);
        return 1;
    }

    // Convert the argument to an integer
    difficulty = atoi(argv[1]);


    // Initialize thread attributes
    pthread_attr_init(&attr);
    // Set the scheduling policy to FIFO
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    // Set the priority of the thread
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_attr_setschedparam(&attr, &param);


// Create the server thread with the specified attributes
    if (pthread_create(&server, &attr, server_thread, NULL) != 0) {
        perror("Failed to create server thread");
        return 1;
    }

    pthread_t miners[NUM_MINERS];
    int miner_ids[NUM_MINERS];

    // Create miner threads
    for (int i = 0; i < NUM_MINERS; i++) {
        miner_ids[i] = i + 1;
        if (i ==0) { // create broken miner thread
        pthread_create(&miners[i], NULL, broken_miner, &miner_ids[i]);
        } else {
                pthread_create(&miners[i], NULL, miner_thread, &miner_ids[i]);
        }
    }

    pthread_join(server, NULL);
    for (int i = 0; i < NUM_MINERS; i++) {
        pthread_join(miners[i], NULL);
    }

    return 0;
}

